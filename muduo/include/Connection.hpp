#pragma once
#include <cstddef>
#include "Epoll.hpp"
#include <memory>
#include <atomic>
#include "SimpleBuffer.hpp"
class SubReactor;

class Connection {
public:
    Connection(FdWrapper fdWrapper, SubReactor* subReactor);
    // async send thread-safe
    void send(const char* data, size_t len);

    void close(bool force = false);
    bool isClosed() const {
        return closed_;
    }
    void sendBufferedData(); 
    void checkNeedClose();
    FdWrapper& fdWrapper() { return fdWrapper_; }
    int64_t registerTimer(int64_t interval_ms, std::function<void()> callback, bool recurring = false);
    bool cancelTimer(int64_t timer_id);

private:
    std::atomic<bool> tryClose_ {false};
    bool closed_ = false;
    FdWrapper fdWrapper_; // 文件描述符包装器
    SubReactor* subReactor_;
    std::mutex sendMutex_; // 保护sendBuffer_
    SimpleBuffer sendBuffer_;
    // std::vector<char> recvBuffer_;
};

