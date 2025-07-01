#include "non_blocking_unix_socket_server.h"
#include <gtest/gtest.h>
#include <thread>
#include <semaphore>

namespace InterProcessCommunication::Test
{
class NonBlockingUnixSocketServerTest : public ::testing::Test
{
protected:
    const std::string m_unix_socket_path = "server.sock";

    void SetUp() {}
    void TearDown() {}

public:
    void ConnectorClient(std::string unix_socket_path, std::binary_semaphore& close_condition, std::function<void()> end_callback)
    {
        const int client_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client_socket_fd == -1) 
        {
            return;
        }

        // Set up the server address
        sockaddr_un server_address{};
        server_address.sun_family = AF_UNIX;
        strncpy(server_address.sun_path, unix_socket_path.c_str(), sizeof(server_address.sun_path) - 1);

        // Connect to the server
        if (connect(client_socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) 
        {
            perror("CLIENT -> Connect failed");
            close(client_socket_fd);
            return;
        }

        // block the thread here until told to resume
        close_condition.acquire();

        /*while(not close_condition)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            std::cout << "CLIENT -> Waiting for close condition\n";
        }*/

        // disconnect from the server
        close(client_socket_fd);
        end_callback();
    };
};

TEST_F(NonBlockingUnixSocketServerTest, Constructor)
{
    NonBlockingUnixSocketServer server(m_unix_socket_path);
}

/*
    This test checks that one client can connect and disconnect correctly
*/
TEST_F(NonBlockingUnixSocketServerTest, MonitorConnection)
{
    bool client_disconnected = false;
    bool client_connected = false;
    
    // start in locked state
    std::binary_semaphore client_close_condition(0);

    NonBlockingUnixSocketServer server(m_unix_socket_path);

    server.SetConnectCallback([&](int client_fd)
    {
        (void)client_fd;
        client_connected = true;
        // order the mock client thread to begin to disconnect
        client_close_condition.release();
    });

    server.SetDisconnectCallback([&](int client_fd)
    {
        (void)client_fd;
        client_disconnected = true;
    });

    server.Start();

    std::thread client_thread(&NonBlockingUnixSocketServerTest::ConnectorClient,this,m_unix_socket_path,std::ref(client_close_condition),[&](){
        std::cout << "CLIENT -> Done\n";
    });

    while(not client_disconnected)
    {
        server.Run();
    }

    client_thread.join();

    EXPECT_TRUE(client_connected);
    EXPECT_TRUE(client_disconnected);
}

}

