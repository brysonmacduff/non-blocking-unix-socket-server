#pragma once
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <list>
#include <chrono>
#include <functional>
#include <span>

namespace InterProcessCommunication
{
class NonBlockingSocketServer
{
public:

    enum class ServerState
    {
        CLOSED,
        RUNNING,
        CLOSING
    };

    struct TcpEndpoint
    {
        std::string ip_address;
        uint16_t port;
    };

    using RxCallback = std::function<void(int client_file_descriptor, const std::span<char>& bytes)>;
    using ConnectCallback = std::function<void(int client_file_descriptor)>;
    using DisconnectCallback = std::function<void(int client_file_descriptor)>;

    ~NonBlockingSocketServer() = default;
    NonBlockingSocketServer(const std::string& unix_socket_path, size_t client_limit = DEFAULT_CLIENT_LIMIT, std::chrono::milliseconds blocking_timeout = DEFAULT_BLOCKING_TIMEOUT, bool is_verbose = false);
    NonBlockingSocketServer(const TcpEndpoint& tcp_endpoint, size_t client_limit = DEFAULT_CLIENT_LIMIT, std::chrono::milliseconds blocking_timeout = DEFAULT_BLOCKING_TIMEOUT, bool is_verbose = false);

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

    void EnqueueSend(int client_file_descriptor, const std::span<char>& bytes);
    void EnqueueBroadcast(const std::span<char>& bytes);
    void SetRxCallback(RxCallback callback);
    void SetConnectCallback(ConnectCallback callback);
    void SetDisconnectCallback(DisconnectCallback callback);
    const std::vector<int>& GetClientFileDescriptors() const;

private:

    enum EndpointMode
    {
        TCP,
        UNIX_DOMAIN,
        UNDEFINED
    };

    struct Endpoint
    {
        EndpointMode mode = EndpointMode::UNDEFINED;
        std::string unix_socket_path {};
        uint16_t tcp_port {};
        std::string tcp_ip_address {};
    };

    struct TxMessage
    {
        int client_file_descriptor;
        std::vector<char> payload;
    };

    static constexpr std::chrono::milliseconds DEFAULT_BLOCKING_TIMEOUT { 10 };
    static constexpr int MAXIMUM_EPOLL_EVENTS = 10;
    static constexpr size_t DEFAULT_CLIENT_LIMIT = 1;
    static constexpr size_t READ_BUFFER_SIZE = 1024;

    Endpoint m_endpoint {};
    const size_t m_client_limit;
    const std::chrono::milliseconds m_blocking_timeout;
    std::vector<int> m_client_file_descriptors;
    ServerState m_server_state { ServerState::CLOSED };
    RxCallback m_rx_callback = [](int client_file_descriptor, const std::span<char>& bytes){
        (void)client_file_descriptor;
        (void)bytes;
    };
    ConnectCallback m_connect_callback = [](int client_file_descriptor){(void)client_file_descriptor;};
    DisconnectCallback m_disconnect_callback = [](int client_file_descriptor){(void)client_file_descriptor;};
    std::list<TxMessage> m_tx_messages;
    bool m_is_verbose;

    int m_server_socket_file_descriptor = -1; // server file descriptor
    int m_server_epoll_file_descriptor = -1; // server epoll file descriptor
    
    bool CreateSocket();
    bool BindToEndpoint();
    bool BindToUnixDomainSocket();
    bool BindToTcpSocket();
    bool Bind(const sockaddr* address, socklen_t size);
    bool Listen();
    bool AcceptClient();
    bool MakeFileDescriptorNonBlocking(int file_descriptor);
    bool ConfigureServerFileDescriptorForEpoll();
    bool ConfigureClientFileDescriptorForEpoll(int client_file_descriptor);
    /*
        This function processes events that are returned from epoll_wait, such as client connects, disconnects, and payloads
    */
    void ProcessEpollEvent();
    void CloseServer();
    void DisconnectClient(int client_file_descriptor);
    void HandleNonBlockingRead(int client_file_descriptor);
    /*
        This function sends messages to clients. Messages are queued by end-users of this server.
    */
    void ProcessTxMessages();
    void SendToClient(const TxMessage& tx_message);
    void Print(const std::string& log);
};
} // namespace InterProcessCommunication