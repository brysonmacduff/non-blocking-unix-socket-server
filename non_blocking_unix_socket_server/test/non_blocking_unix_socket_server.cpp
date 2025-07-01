#include "non_blocking_unix_socket_server.h"
#include <gtest/gtest.h>
#include <thread>

namespace InterProcessCommunication::Test
{
class NonBlockingUnixSocketServerTest : public ::testing::Test
{
protected:
    const std::string m_unix_socket_path = "server.sock";

    void SetUp() {}
    void TearDown() {}

public:
    void ConnectorClient(std::string unix_socket_path, bool& close_condition, std::function<void()> end_callback)
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
            perror("MOCK_CLIENT -> Connect failed");
            close(client_socket_fd);
            return;
        }

        while(not close_condition)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            std::cout << "MOCK_CLIENT -> Waiting for close condition\n";
        }

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
    bool client_close_condition = false;

    NonBlockingUnixSocketServer server(m_unix_socket_path);

    server.SetConnectCallback([&](int client_fd)
    {
        (void)client_fd;
        client_connected = true;
        client_close_condition = true;
    });

    server.SetDisconnectCallback([&](int client_fd)
    {
        (void)client_fd;
        client_disconnected = true;
    });

    server.Start();

    std::thread client_thread(&NonBlockingUnixSocketServerTest::ConnectorClient,this,m_unix_socket_path,std::ref(client_close_condition),[&](){});

    while(not client_disconnected)
    {
        server.Run();
    }

    client_thread.join();

    EXPECT_TRUE(client_connected);
    EXPECT_TRUE(client_disconnected);
}

}

