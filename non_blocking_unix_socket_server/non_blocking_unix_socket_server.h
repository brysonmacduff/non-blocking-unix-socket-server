#pragma once
#include "i_non_blocking_server.h"
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <deque>
#include <chrono>

namespace InterProcessCommunication
{
class NonBlockingUnixSocketServer : public INonBlockingServer
{
public:

    enum class ServerState
    {
        CLOSED,
        RUNNING,
        CLOSING
    };

    ~NonBlockingUnixSocketServer() = default;
    NonBlockingUnixSocketServer(const std::string& unix_socket_path, size_t client_limit = DEFAULT_CLIENT_LIMIT, std::chrono::milliseconds blocking_timeout = DEFAULT_BLOCKING_TIMEOUT);
    NonBlockingUnixSocketServer(std::string&& unix_socket_path, size_t client_limit = DEFAULT_CLIENT_LIMIT, std::chrono::milliseconds blocking_timeout = DEFAULT_BLOCKING_TIMEOUT);

    /*
        Tell the server to start and listen for client connection attempts.
    */
    bool Start();

    /*
        Allows the server to do work. While waiting for events, blocks the calling thread for a duration equal to the constructor argument "blocking_timeout".
    */
    void Run();

    /*
        Order the server to begin shutting down.
    */
    bool RequestStop();

    /*
        Get the current operational state of the server. Is the server running or not, for example.
    */
    ServerState GetServerState() const;

    void EnqueueSend(int client_file_descriptor, std::vector<char>&& bytes) override;
    void EnqueueBroadcast(std::vector<char>&& bytes) override;
    void SetRxCallback(RxCallback callback) override;
    const std::deque<int>& GetClientFileDescriptors() const override;

private:

    struct TxMessage
    {
        int client_file_descriptor;
        std::vector<char> payload;
    };

    static constexpr std::chrono::milliseconds DEFAULT_BLOCKING_TIMEOUT { 10 };
    static constexpr int MAXIMUM_EPOLL_EVENTS = 10;
    static constexpr size_t DEFAULT_CLIENT_LIMIT = 1;
    static constexpr size_t READ_BUFFER_SIZE = 1024;

    const std::string m_unix_socket_path;
    const size_t m_client_limit;
    const std::chrono::milliseconds m_blocking_timeout;
    std::deque<int> m_client_file_descriptors;
    ServerState m_server_state { ServerState::CLOSED };
    RxCallback m_rx_callback = [](int client_file_descriptor, std::vector<char>&& bytes){
        (void)client_file_descriptor;
        (void)bytes;
    };
    std::deque<TxMessage> m_tx_messages;

    int m_server_socket_file_descriptor = -1; // server file descriptor
    int m_server_epoll_file_descriptor = -1; // server epoll file descriptor
    
    bool CreateSocket();
    bool Bind();
    bool Listen();
    bool Accept();
    bool MakeFileDescriptorNonBlocking(int file_descriptor);
    bool ConfigureServerFileDescriptorForEpoll();
    bool ConfigureClientFileDescriptorForEpoll(int client_file_descriptor);
    void ProcessEpollEvent();
    void CloseServer();
    void DisconnectClient(int client_file_descriptor);
    void HandleNonBlockingRead(int client_file_descriptor);
    void ProcessTxMessages();
    void SendToClient(const TxMessage& tx_message);
};
} // namespace InterProcessCommunication