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
    if (tryClose_) {
        return;
    }
    // buffer into sendBuffer_
    sendBuffer_.write(data, len);
    subReactor_->addSendEvent(fdWrapper_);
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
