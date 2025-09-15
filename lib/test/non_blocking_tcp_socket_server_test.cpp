#include "non_blocking_socket_server.h"
#include <gtest/gtest.h>
#include <thread>
#include <semaphore>
#include <memory>

namespace InterProcessCommunication::Test
{

using TcpEndpoint = NonBlockingSocketServer::TcpEndpoint;

class NonBlockingTcpSocketServerTest : public ::testing::Test
{
protected:
    static constexpr size_t CLIENT_RX_BUFFER_SIZE = 1024;
    const TcpEndpoint m_tcp_endpoint { .ip_address = "127.0.0.1", .port = 20000 };
    
    void SetUp() {}
    void TearDown() {}

    bool ArePayloadsEqual(const std::vector<char>& expected_payload, const std::vector<char>& received_payload)
    {
        EXPECT_EQ(expected_payload.size(),received_payload.size());

        if(expected_payload.size() != received_payload.size())
        {
            return false;
        }

        for(size_t index = 0; index < expected_payload.size(); ++index)
        {
            EXPECT_EQ(expected_payload[index],received_payload[index]);

            if(expected_payload[index] != received_payload[index])
            {
                return false;
            }
        }

        return true;
    }

public:
    void ConnectorClient(TcpEndpoint tcp_endpoint, std::binary_semaphore& close_condition, std::function<void()> end_callback)
    {
        const int client_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

        if (client_socket_fd == -1) 
        {
            return;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(tcp_endpoint.port);
        address.sin_addr.s_addr = inet_addr(tcp_endpoint.ip_address.c_str());

        // Connect to the server
        if (connect(client_socket_fd, (struct sockaddr*)&address, sizeof(address)) == -1) 
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

    void ReaderSenderClient(TcpEndpoint tcp_endpoint, std::function<void()> end_callback, std::vector<char> scheduled_tx_payload, const std::vector<char> expected_rx_payload, std::string client_name)
    {
        bool is_expected_rx_payload_valid = false;

        // create the socket file descriptor
        const int client_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

        if (client_socket_fd == -1) 
        {
             perror("CLIENT -> Failed to create scoket");
            return;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(tcp_endpoint.port);
        address.sin_addr.s_addr = inet_addr(tcp_endpoint.ip_address.c_str());

        // Connect to the server
        if (connect(client_socket_fd, (struct sockaddr*)&address, sizeof(address)) == -1) 
        {
            perror("CLIENT -> Connection attempt failed");
            close(client_socket_fd);
            return;
        }

        // receive payload from server

        // This container holds an accumulation of bytes that are received from one or more socket readings
        std::vector<char> accumulated_rx_payload;
        accumulated_rx_payload.reserve(CLIENT_RX_BUFFER_SIZE);

        // This is used for simply reading from the socket
        std::vector<char> rx_buffer(CLIENT_RX_BUFFER_SIZE);
        
        while(true)
        {
            if(accumulated_rx_payload.size() == expected_rx_payload.size())
            {
                is_expected_rx_payload_valid = ArePayloadsEqual(expected_rx_payload,accumulated_rx_payload);
                break;
            }

            std::cout << "CLIENT " << client_name << " -> Reading...\n";
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
                accumulated_rx_payload.emplace_back(rx_buffer[index]);
            }
        }

        EXPECT_TRUE(is_expected_rx_payload_valid);

        // send scheduled tx payload to server

        ssize_t total_sent_bytes = 0;

        while(total_sent_bytes < scheduled_tx_payload.size())
        {
            std::cout << "CLIENT " << client_name << " -> Sending...\n";
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
TEST_F(NonBlockingTcpSocketServerTest, Constructor)
{
    NonBlockingSocketServer server(m_tcp_endpoint);
}

/*
    This test checks that one client can connect and disconnect correctly
*/
TEST_F(NonBlockingTcpSocketServerTest, MonitorConnection)
{
    bool client_disconnected = false;
    bool client_connected = false;
    
    // start in locked state
    std::binary_semaphore client_close_condition(0);

    NonBlockingSocketServer server(m_tcp_endpoint);

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

    std::thread client_thread(&NonBlockingTcpSocketServerTest::ConnectorClient,this,m_tcp_endpoint,std::ref(client_close_condition),[&](){
        std::cout << "CLIENT -> Done\n";
    });

    while(not client_disconnected)
    {
        server.Run();
    }

    // shutdown the server to free the port and not interfere with other tests

    server.RequestStop();

    while(server.GetServerState() != NonBlockingSocketServer::ServerState::CLOSED)
    {
        server.Run();
    }

    client_thread.join();

    EXPECT_TRUE(client_connected);
    EXPECT_TRUE(client_disconnected);
}

/*
    This test validates that the server can send and receive payloads with one client
*/
TEST_F(NonBlockingTcpSocketServerTest, SendAndReceivePayload_OneClient)
{
    NonBlockingSocketServer server(m_tcp_endpoint);

    const std::string server_tx_string = "hello from server";
    const std::string client_tx_string = "hello from client";
    std::vector<char> server_tx_payload(server_tx_string.begin(), server_tx_string.end());
    const std::vector<char> client_tx_payload(client_tx_string.begin(), client_tx_string.end());

    std::vector<char> rx_buffer;
    rx_buffer.reserve(CLIENT_RX_BUFFER_SIZE);

    server.SetRxCallback([&](int client_fd, const std::span<char>& rx_payload)
    {
        for(const char& byte : rx_payload)
        {
            rx_buffer.emplace_back(byte);
        }
    });

    // qeueue up a tx payload to the newly connected client
    server.SetConnectCallback([&](int client_fd)
    {
        const std::span<char> server_tx_payload_view (server_tx_payload.begin(),server_tx_payload.end());
        server.EnqueueSend(client_fd,server_tx_payload_view);
    });

    server.Start();

    std::thread client_thread(&NonBlockingTcpSocketServerTest::ReaderSenderClient
    ,this
    ,m_tcp_endpoint
    ,[]()
    {
        std::cout << "CLIENT -> Done\n";
    }
    , client_tx_payload
    , server_tx_payload
    , "1"
    );

    while(rx_buffer.size() < client_tx_payload.size())
    {
        server.Run();
    }

    // shutdown the server to free the port and not interfere with other tests

    server.RequestStop();

    while(server.GetServerState() != NonBlockingSocketServer::ServerState::CLOSED)
    {
        server.Run();
    }

    client_thread.join();
    ASSERT_TRUE(ArePayloadsEqual(client_tx_payload,rx_buffer));
}

/*
    This test validates that the server can send and receive payloads with two clients
*/
TEST_F(NonBlockingTcpSocketServerTest, SendAndReceivePayload_TwoClients)
{
    const uint client_count = 2;
    NonBlockingSocketServer server(m_tcp_endpoint,client_count);

    const std::string server_tx_string = "hello from server";
    const std::string client_tx_string = "hello from client";
    std::vector<char> server_tx_payload(server_tx_string.begin(), server_tx_string.end());
    const std::vector<char> client_tx_payload(client_tx_string.begin(), client_tx_string.end());

    std::map<int,std::vector<char>> rx_buffers;

    server.SetRxCallback([&](int client_fd, const std::span<char>& rx_payload)
    {
        EXPECT_TRUE(rx_buffers.contains(client_fd));

        for(const char& byte : rx_payload)
        {
            rx_buffers[client_fd].emplace_back(byte);
        }
    });

    // qeueue up a tx payload to the newly connected client
    server.SetConnectCallback([&](int client_fd)
    {
        // create an rx buffer associated to this client
        rx_buffers.insert({client_fd,std::vector<char>()});
        rx_buffers[client_fd].reserve(CLIENT_RX_BUFFER_SIZE);

        // send a message to this client
        server.EnqueueSend(client_fd,server_tx_payload);
    });

    server.Start();

    std::thread client_thread1(&NonBlockingTcpSocketServerTest::ReaderSenderClient
    ,this
    ,m_tcp_endpoint
    ,[]()
    {
        std::cout << "CLIENT 1 -> Done\n";
    }
    , client_tx_payload
    , server_tx_payload
    , "1"
    );

    std::thread client_thread2(&NonBlockingTcpSocketServerTest::ReaderSenderClient
    ,this
    ,m_tcp_endpoint
    ,[]()
    {
        std::cout << "CLIENT 2 -> Done\n";
    }
    , client_tx_payload
    , server_tx_payload
    , "2"
    );

    uint rx_payloads_received = 0;

    while(rx_payloads_received < client_count)
    {
        server.Run();

        for(const auto& pair : rx_buffers)
        {
            if(pair.second.size() == client_tx_payload.size())
            {
                ++rx_payloads_received;
            }
        }
    }

    // shutdown the server to free the port and not interfere with other tests

    server.RequestStop();

    while(server.GetServerState() != NonBlockingSocketServer::ServerState::CLOSED)
    {
        server.Run();
    }

    client_thread1.join();
    client_thread2.join();

    for(const auto& pair : rx_buffers)
    {
         EXPECT_TRUE(ArePayloadsEqual(client_tx_payload,pair.second));
    }
}


} // InterProcessCommunication::Test

