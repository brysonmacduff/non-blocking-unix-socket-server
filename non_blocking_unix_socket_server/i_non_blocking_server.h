#pragma once

#include <vector>
#include <functional>
#include <deque>

namespace InterProcessCommunication
{
class INonBlockingServer
{
public:
    using RxCallback = std::function<void(int client_file_descriptor, std::vector<char>&& bytes)>;
    using ConnectCallback = std::function<void(int client_file_descriptor)>;
    using DisconnectCallback = std::function<void(int client_file_descriptor)>;

    virtual ~INonBlockingServer() = default;
    virtual void EnqueueSend(int client_file_descriptor, std::vector<char>&& bytes) = 0;
    virtual void EnqueueBroadcast(std::vector<char>&& bytes) = 0;
    virtual void SetRxCallback(RxCallback callback) = 0;
    virtual void SetConnectCallback(ConnectCallback callback) = 0;
    virtual void SetDisconnectCallback(DisconnectCallback callback) = 0;
    virtual const std::deque<int>& GetClientFileDescriptors() const = 0;
};
} // namespace InterProcessCommunication
