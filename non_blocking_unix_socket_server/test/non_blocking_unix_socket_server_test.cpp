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
    static constexpr size_t CLIENT_RX_BUFFER_SIZE = 1024;

    void SetUp() {}
    void TearDown() {}

    bool ArePayloadsEqual(std::vector<char> expected_payload, std::deque<char> received_payload)
    {
        EXPECT_EQ(expected_payload.size(),received_payload.size());

        if(expected_payload.size() != received_payload.size())
        {
            return false;
        }

        for(size_t index = 0; index < expected_payload.size(); ++index)
        {
            if(expected_payload[index] != received_payload[index])
            {
                return false;
            }
        }

        return true;
    }

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

        // disconnect from the server
        close(client_socket_fd);
        end_callback();
    };

    void ReaderSenderClient(std::string unix_socket_path, std::function<void()> end_callback, std::vector<char> scheduled_tx_payload, const std::vector<char> expected_rx_payload)
    {
        bool is_expected_rx_payload_valid = false;

        // create the socket file descriptor
        const int client_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client_socket_fd == -1) 
        {
             perror("CLIENT -> Failed to create scoket");
            return;
        }

        // Set up the server address
        sockaddr_un server_address{};
        server_address.sun_family = AF_UNIX;
        strncpy(server_address.sun_path, unix_socket_path.c_str(), sizeof(server_address.sun_path) - 1);

        // Connect to the server
        if (connect(client_socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) 
        {
            perror("CLIENT -> Connection attempt failed");
            close(client_socket_fd);
            return;
        }

        // receive payload from server

        std::deque<char> rx_queue;
        std::vector<char> rx_buffer(CLIENT_RX_BUFFER_SIZE);
        
        while(true)
        {
            if(rx_queue.size() == expected_rx_payload.size())
            {
                is_expected_rx_payload_valid = ArePayloadsEqual(expected_rx_payload,rx_queue);
                break;
            }

            std::cout << "CLIENT -> Reading...\n";
            const ssize_t read_result = read(client_socket_fd,rx_buffer.data(),rx_buffer.size());

            EXPECT_NE(read_result,-1);

            if(read_result == -1)
            {
                perror("CLIENT -> Received EOF");
                break;
            }

            // copy received bytes into the rx buffer
            for(size_t index = 0; index < read_result; ++index)
            {
                rx_queue.emplace_back(rx_buffer[index]);
            }
        }

        EXPECT_TRUE(is_expected_rx_payload_valid);

        // send scheduled tx payload to server

        ssize_t total_sent_bytes = 0;

        while(total_sent_bytes < scheduled_tx_payload.size())
        {
            std::cout << "CLIENT -> Sending...\n";
            const ssize_t send_result = send(client_socket_fd, scheduled_tx_payload.data() + total_sent_bytes, scheduled_tx_payload.size() - total_sent_bytes, 0);

            EXPECT_NE(send_result,-1);

            if(send_result == -1)
            {
                perror("CLIENT -> Received EOF");
                break;
            }
            
            total_sent_bytes += send_result;
        }

        EXPECT_EQ(total_sent_bytes, scheduled_tx_payload.size());

        // disconnect from the server
        close(client_socket_fd);
        end_callback();
    }
};

/*
    Basic test to show instance construction works
*/
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

/*
    This test validates that the server and send and receives payloads with one client
*/
TEST_F(NonBlockingUnixSocketServerTest, SendAndReceivePayload)
{
    NonBlockingUnixSocketServer server(m_unix_socket_path);

    const std::string server_tx_string = "hello from server";
    const std::string client_tx_string = "hello from client";
    const std::vector<char> server_tx_payload(server_tx_string.begin(), server_tx_string.end());
    const std::vector<char> client_tx_payload(client_tx_string.begin(), client_tx_string.end());

    std::deque<char> rx_buffer;

    server.SetRxCallback([&](int client_fd, const std::vector<char>& rx_payload)
    {
        for(const char& byte : rx_payload)
        {
            rx_buffer.emplace_back(byte);
        }
    });

    // qeueue up a tx payload to the newly connected client
    server.SetConnectCallback([&](int client_fd)
    {
        server.EnqueueSend(client_fd,server_tx_payload);
    });

    // when the client disconnects, evaluate that server rx buffer matches expected client tx
    server.SetDisconnectCallback([&](int client_fd)
    {
        (void)client_fd;
    });

    server.Start();

    std::thread client_thread(&NonBlockingUnixSocketServerTest::ReaderSenderClient
    ,this
    ,m_unix_socket_path
    ,[]()
    {
        std::cout << "CLIENT -> Done\n";
    }
    , client_tx_payload
    , server_tx_payload
    );

    while(rx_buffer.size() < client_tx_payload.size())
    {
        server.Run();
    }

    client_thread.join();
    ASSERT_TRUE(ArePayloadsEqual(client_tx_payload,rx_buffer));
}

} // InterProcessCommunication::Test

