#include "Connection.hpp"
#include "spdlog/spdlog.h"
#include "SubReactor.hpp"
#include <unistd.h>

Connection::Connection(FdWrapper fdWrapper, SubReactor* subReactor)
    : fdWrapper_(fdWrapper), subReactor_(subReactor)  {
        // tryClose_ = false;
    // 初始化连接
    spdlog::info("Connection created with fd: {}", fdWrapper_.fd());
}

void Connection::send(const char* data, size_t len) {
    ScopedTimer timer("ConnectionSend");
    if (tryClose_) {
        return;
    }
    std::lock_guard<std::mutex> lock(sendMutex_);
    // buffer into sendBuffer_
    sendBuffer_.write(data, len);
    subReactor_->enqueueSend(fdWrapper_.fd());
}


void Connection::sendBufferedData() {
    std::lock_guard<std::mutex> lock(sendMutex_);
        ssize_t n;
    if (sendBuffer_.noData()) {
        spdlog::info("No data to send for fd: {}", fdWrapper_.fd());
        return;
    }

    do {
        auto writeChunkSize = std::min(sendBuffer_.size(), static_cast<size_t>(1024 * 8)); // 每次写入4096字节
        n = write(fdWrapper_.fd(), sendBuffer_.data(), writeChunkSize);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // spdlog::debug("Write would block on fd={}", conn->fdWrapper().fd());
                break; // 非阻塞模式下，退出循环
            } else {
                return;
            }
        } else if (n == 0) {
            break; // 连接可能已经关闭
        } else {
            sendBuffer_.advance(n); // 更新发送缓冲区
        }
    } while (n > 0 && sendBuffer_.size() > 0);
    
}

void Connection::close(bool force) {
    tryClose_ = true;
    if (!force && sendBuffer_.size()) {
        spdlog::info("Pending connection close with fd: {} force: {}, sendBuffer size: {}", fdWrapper_.fd(), force, sendBuffer_.size());
        subReactor_->disableReadEventAndShutdown(fdWrapper_);
        return;
    } 
    closed_ = true;
    subReactor_->removeConnection(fdWrapper_);
}

void Connection::checkNeedClose() {
    if (tryClose_ && sendBuffer_.noData()) {
        subReactor_->removeConnection(fdWrapper_);
        closed_ = true;
    }
}
int64_t Connection::registerTimer(int64_t interval_ms, std::function<void()> callback, bool recurring) {
    return subReactor_->registerTimer(interval_ms, callback, recurring);
}

bool Connection::cancelTimer(int64_t timer_id) {
    return subReactor_->cancelTimer(timer_id);
}
