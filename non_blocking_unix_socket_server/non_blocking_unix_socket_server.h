#pragma once
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
class NonBlockingUnixSocketServer
{
public:

    enum class ServerState
    {
        CLOSED,
        RUNNING,
        CLOSING
    };

    NonBlockingUnixSocketServer(const std::string& unix_socket_path, size_t client_limit = DEFAULT_CLIENT_LIMIT, std::chrono::milliseconds blocking_timeout = DEFAULT_BLOCKING_TIMEOUT);
    
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

private:

    static constexpr std::chrono::milliseconds DEFAULT_BLOCKING_TIMEOUT { 10 };
    static constexpr int MAXIMUM_EPOLL_EVENTS = 10;
    static constexpr size_t DEFAULT_CLIENT_LIMIT = 1;
    static constexpr size_t READ_BUFFER_SIZE = 1024;

    const std::string m_unix_socket_path;
    const size_t m_client_limit;
    const std::chrono::milliseconds m_blocking_timeout;
    std::deque<int> m_client_file_descriptors;
    ServerState m_server_state { ServerState::CLOSED };

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

};
} // namespace InterProcessCommunication