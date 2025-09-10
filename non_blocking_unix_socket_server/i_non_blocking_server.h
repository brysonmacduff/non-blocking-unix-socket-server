#pragma once

#include <functional>
#include <deque>
#include <span>

namespace InterProcessCommunication
{
class INonBlockingServer
{
public:
    using RxCallback = std::function<void(int client_file_descriptor, const std::span<char>& bytes)>;
    using ConnectCallback = std::function<void(int client_file_descriptor)>;
    using DisconnectCallback = std::function<void(int client_file_descriptor)>;

    virtual ~INonBlockingServer() = default;
    virtual void EnqueueSend(int client_file_descriptor, const std::span<char>& bytes) = 0;
    virtual void EnqueueBroadcast(const std::span<char>& bytes) = 0;
    virtual void SetRxCallback(RxCallback callback) = 0;
    virtual void SetConnectCallback(ConnectCallback callback) = 0;
    virtual void SetDisconnectCallback(DisconnectCallback callback) = 0;
    virtual const std::deque<int>& GetClientFileDescriptors() const = 0;
};
} // namespace InterProcessCommunication
