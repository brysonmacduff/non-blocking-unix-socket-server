#include "non_blocking_unix_socket_server.h"

namespace InterProcessCommunication
{
NonBlockingUnixSocketServer::NonBlockingUnixSocketServer(const std::string& unix_socket_path, size_t client_limit, std::chrono::milliseconds blocking_timeout) 
: m_unix_socket_path(unix_socket_path)
, m_client_limit(client_limit)
, m_blocking_timeout(blocking_timeout)
{
}

NonBlockingUnixSocketServer::NonBlockingUnixSocketServer(std::string &&unix_socket_path, size_t client_limit, std::chrono::milliseconds blocking_timeout)
: m_unix_socket_path(unix_socket_path)
, m_client_limit(client_limit)
, m_blocking_timeout(blocking_timeout)
{
}

bool NonBlockingUnixSocketServer::Start()
{
    // Remove the socket file if it already exists
    unlink(m_unix_socket_path.c_str());

    if(not CreateSocket())
    {
        return false;
    }

    if(not Bind())
    {
        return false;
    }

    if(not Listen())
    {
        return false;
    }

    if(not MakeFileDescriptorNonBlocking(m_server_socket_file_descriptor))
    {
        return false;
    }

    if(not ConfigureServerFileDescriptorForEpoll())
    {
        return false;
    }

    m_server_state = ServerState::RUNNING;

    std::cout << "NonBlockingUnixSocketServer::Start() -> Server has started on: " << m_unix_socket_path << "\n";
    
    return true;
}

bool NonBlockingUnixSocketServer::RequestStop()
{
    if(m_server_state!= ServerState::RUNNING)
    {
        return false;
    }

    m_server_state = ServerState::CLOSING;

    return true;
}

NonBlockingUnixSocketServer::ServerState NonBlockingUnixSocketServer::GetServerState() const
{
    return m_server_state;
}

void NonBlockingUnixSocketServer::EnqueueSend(int client_file_descriptor, const std::span<char>& bytes)
{
    const std::vector<char> tx_bytes (bytes.begin(),bytes.end());
    m_tx_messages.emplace_back(TxMessage{client_file_descriptor,tx_bytes});
}

void NonBlockingUnixSocketServer::EnqueueBroadcast(const std::span<char>& bytes)
{
    for(const int& client_file_descriptor : m_client_file_descriptors)
    {
        EnqueueSend(client_file_descriptor,std::move(bytes));
    }
}

void NonBlockingUnixSocketServer::SetRxCallback(RxCallback callback)
{
    m_rx_callback = std::move(callback);
}

void NonBlockingUnixSocketServer::SetConnectCallback(ConnectCallback callback)
{
    m_connect_callback = std::move(callback);
}

void NonBlockingUnixSocketServer::SetDisconnectCallback(DisconnectCallback callback)
{
    m_disconnect_callback = std::move(callback);
}

const std::deque<int> &NonBlockingUnixSocketServer::GetClientFileDescriptors() const
{
    return m_client_file_descriptors;
}

bool NonBlockingUnixSocketServer::CreateSocket()
{
    // Create a socket
    const int server_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket_fd == -1) 
    {
        perror("NonBlockingUnixSocketServer::Start() -> Socket creation failed");
        return false;
    }

    m_server_socket_file_descriptor = server_socket_fd;
    return true;
}

bool NonBlockingUnixSocketServer::Bind()
{
    // Bind the socket to the specified path
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, m_unix_socket_path.c_str(), sizeof(address.sun_path) - 1);

    if (bind(m_server_socket_file_descriptor, (struct sockaddr*)&address, sizeof(address)) == -1) 
    {
        perror("UnixSocketServer::Bind() -> Bind failed");
        close(m_server_socket_file_descriptor);
        return false;
    }

    return true;
}

bool NonBlockingUnixSocketServer::Listen()
{
     // Start listening for incoming connections
    if (listen(m_server_socket_file_descriptor, m_client_limit) == -1) 
    {
        perror("NonBlockingUnixSocketServer::Start() -> Listen failed");
        close(m_server_socket_file_descriptor);
        return false;
    }

    return true;
}

bool NonBlockingUnixSocketServer::AcceptClient()
{
    if(m_client_file_descriptors.size() == m_client_limit)
    {
        std::cout << "NonBlockingUnixSocketServer::AcceptClient() -> Rejected client connection due to connection limit.\n";
        return false;
    }

    const int client_fd = accept(m_server_socket_file_descriptor, nullptr, nullptr);

    if(client_fd == -1)
    {
        perror("NonBlockingUnixSocketServer::AcceptClient() -> Failed to accept client");
        return false;
    }

    if(not MakeFileDescriptorNonBlocking(client_fd))
    {
        return false;
    }

    // configure the accepted client file descriptor with epoll events

    epoll_event client_ev{};
    client_ev.events = EPOLLIN | EPOLLET;
    client_ev.data.fd = client_fd;
    const bool epoll_ctl_result = epoll_ctl(m_server_epoll_file_descriptor, EPOLL_CTL_ADD, client_fd, &client_ev) == 0;
    
    if(not epoll_ctl_result)
    {
        perror("NonBlockingUnixSocketServer::AcceptClient() -> Failed to confugure client file descriptor for epoll events");
    }

    // save the client file descriptor, because the client has been accepted
    m_client_file_descriptors.emplace_back(client_fd);

    std::cout << "NonBlockingUnixSocketServer::AcceptClient() -> Accepted client connection with file descriptor: " << std::to_string(client_fd) << "\n";

    m_connect_callback(client_fd);

    return epoll_ctl_result;
}

bool NonBlockingUnixSocketServer::MakeFileDescriptorNonBlocking(int file_descriptor)
{
    const bool result = fcntl(file_descriptor, F_SETFL, O_NONBLOCK) != -1;

    if(not result)
    {
        perror("UnixSocketServer::MakeFileDescriptorNonBlocking() -> Failed to make file descriptor non-blocking");
    }

    return result;
}

bool NonBlockingUnixSocketServer::ConfigureServerFileDescriptorForEpoll()
{
    m_server_epoll_file_descriptor = epoll_create1(0);
    // define epoll event conditions for the server socket file descriptor
    epoll_event server_epoll_events{}, events[MAXIMUM_EPOLL_EVENTS];
    server_epoll_events.events = EPOLLIN;
    server_epoll_events.data.fd = m_server_socket_file_descriptor;
    // applu the epoll event conditions to the server socket file descriptor
    const bool epoll_ctl_result = epoll_ctl(m_server_epoll_file_descriptor, EPOLL_CTL_ADD, m_server_socket_file_descriptor, &server_epoll_events) == 0;

    if(not epoll_ctl_result)
    {
        perror("UnixSocketServer::ConfigureServerFileDescriptorForEpoll() -> Failed to configure epoll for file descriptor");
    }

    return epoll_ctl_result;
}

bool NonBlockingUnixSocketServer::ConfigureClientFileDescriptorForEpoll(int client_file_descriptor)
{
    epoll_event client_epoll_events{};
    client_epoll_events.events = EPOLLIN | EPOLLET;
    client_epoll_events.data.fd = client_file_descriptor;
    const bool epoll_ctl_result = epoll_ctl(m_server_epoll_file_descriptor, EPOLL_CTL_ADD, client_file_descriptor, &client_epoll_events) == 0;
    
    if(not epoll_ctl_result)
    {
        perror("UnixSocketServer::Accept() -> Failed to confugure client file descriptor for epoll events");
    }

    return epoll_ctl_result;
}

void NonBlockingUnixSocketServer::Run()
{
    ProcessEpollEvent();
    ProcessTxMessages();
}

void NonBlockingUnixSocketServer::ProcessEpollEvent()
{
    if(m_server_state == ServerState::CLOSING)
    {
        CloseServer();
        return;
    }

    epoll_event events[MAXIMUM_EPOLL_EVENTS];

    const int event_count = epoll_wait(m_server_epoll_file_descriptor, events, MAXIMUM_EPOLL_EVENTS, m_blocking_timeout.count());

    if(event_count == -1)
    {
        perror("NonBlockingUnixSocketServer::ProcessEpollEvent() -> Triggered events were erroneous.");
        return;
    }
    
    for (int i = 0; i < event_count; ++i) 
    {
        // if the event file descriptor is the server's, then a client has connected
        if (events[i].data.fd == m_server_socket_file_descriptor) 
        {
            AcceptClient();
        } 
        // if the event is for a client file descriptor, then handle it here
        else 
        {
            HandleNonBlockingRead(events[i].data.fd);
        }
    }
}

void NonBlockingUnixSocketServer::CloseServer()
{
    for(const auto& client_fd : m_client_file_descriptors)
    {
        DisconnectClient(client_fd);
    }

    close(m_server_socket_file_descriptor);

    m_client_file_descriptors.clear();

    m_server_state = ServerState::CLOSED;
}

void NonBlockingUnixSocketServer::DisconnectClient(int client_file_descriptor)
{
    // remove the client's file descriptor from epoll to avoid dead file descriptor issues
    epoll_ctl(m_server_epoll_file_descriptor, EPOLL_CTL_DEL, client_file_descriptor, nullptr);
    // close the client file descriptor
    close(client_file_descriptor);
    
    for(auto it = m_client_file_descriptors.begin(); it != m_client_file_descriptors.end(); ++it)
    {
        if(*it == client_file_descriptor)
        {
            m_client_file_descriptors.erase(it);
            break;
        }
    }

    m_disconnect_callback(client_file_descriptor);
    std::cout << "NonBlockingUnixSocketServer::DisconnectClient() -> Disconnected client with file descriptor: " << std::to_string(client_file_descriptor) << "\n";
}

void NonBlockingUnixSocketServer::HandleNonBlockingRead(int client_file_descriptor)
{
    std::vector<char> read_buffer(MAXIMUM_EPOLL_EVENTS);

    // loop until there is nothing left to read
    while(true)
    {
        const ssize_t bytes = read(client_file_descriptor, read_buffer.data(), read_buffer.size());

        if(bytes == -1)
        {
            // stop reading if the non-blocking socket reports there is nothing left to read or there is an error
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EBADF)
            {
                perror("NonBlockingUnixSocketServer::ProcessEpollEvent() -> Done reading: ");
                break;
            }
        }

        if(bytes == 0)
        {
            DisconnectClient(client_file_descriptor);
        }
        else if (bytes > 0) 
        {
            std::vector<char> rx_payload(read_buffer.data(), read_buffer.data() + bytes);
            std::span<char> rx_payload_view (rx_payload.begin(), rx_payload.end());
            m_rx_callback(client_file_descriptor,rx_payload_view);
            std::cout << "NonBlockingUnixSocketServer::ProcessEpollEvent() -> RX PAYLOAD: " << std::string(rx_payload.data(), rx_payload.size()) << "\n";
            continue;
        } 
    }
}

void NonBlockingUnixSocketServer::ProcessTxMessages()
{
    if(m_tx_messages.empty())
    {
        return;
    }

    const TxMessage& next_tx_message = m_tx_messages.front();

    for(const int& client_file_descriptor : m_client_file_descriptors)
    {
        if(next_tx_message.client_file_descriptor == client_file_descriptor)
        {
            SendToClient(next_tx_message);
            m_tx_messages.pop_front();
            break;
        }
    }
}

void NonBlockingUnixSocketServer::SendToClient(const TxMessage &tx_message)
{
    ssize_t total_bytes_sent = 0;

    while(total_bytes_sent < tx_message.payload.size())
    {
        const ssize_t sent_bytes = send(tx_message.client_file_descriptor, tx_message.payload.data() + total_bytes_sent, tx_message.payload.size() - total_bytes_sent,0);

        // if sent_bytes is -1, ether consider it an error and exit or wait for the socket to be ready to send, depending on errno
        if(sent_bytes == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            else if(errno == EBADF)
            {
                DisconnectClient(tx_message.client_file_descriptor);
                return;
            }
        }
        else if(sent_bytes > 0)
        {
            total_bytes_sent += sent_bytes;
        }
    }
}

} // namespace InterProcessCommunication