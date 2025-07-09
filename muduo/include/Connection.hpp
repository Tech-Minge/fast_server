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
    SimpleBuffer& sendBuffer() { return sendBuffer_; }
    bool isClosed() const {
        return closed_;
    }
    void checkNeedClose();
    FdWrapper& fdWrapper() { return fdWrapper_; }
private:
    std::atomic<bool> tryClose_ {false};
    bool closed_ = false;
    FdWrapper fdWrapper_; // 文件描述符包装器
    SubReactor* subReactor_;

    SimpleBuffer sendBuffer_;
    // std::vector<char> recvBuffer_;
};

