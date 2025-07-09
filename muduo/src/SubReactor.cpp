#include "SubReactor.hpp"
#include "spdlog/spdlog.h"

SubReactor::SubReactor() {
    // 创建 pipe 用于通知新连接
    if (pipe(pipeFds_) < 0) {
        spdlog::error("pipe failed: {}", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 设置非阻塞
    fcntl(pipeFds_[0], F_SETFL, O_NONBLOCK);
    fcntl(pipeFds_[1], F_SETFL, O_NONBLOCK);

    
    epoll_.operateFd({pipeFds_[0], EPOLLIN | EPOLLET}, EPOLL_CTL_ADD);
}

SubReactor::~SubReactor() {
    close(pipeFds_[0]);
    close(pipeFds_[1]);
}

void SubReactor::start() {
    thread_ = std::thread([thisPtr = shared_from_this()]() {
        spdlog::info("SubReactor thread started with reference count: {}", thisPtr.use_count());
        thisPtr->isRunning_ = true;
        thisPtr->run();
    });
}

void SubReactor::stop() {
    spdlog::info("SubReactor stop");
    isRunning_ = false;
    // 写入 pipe 通知
    char ch = 1;
    int n = write(pipeFds_[1], &ch, 1);
    if (n < 0) {
        spdlog::error("write pipe error: {}", strerror(errno));
    }
}

void SubReactor::join() {
    if (thread_.joinable()) {
        thread_.join();
    } else {
        spdlog::error("SubReactor thread not joinable");
    }
}

void SubReactor::enqueueNewConnection(int fd) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    newConnections_.push(fd);
    // 写入 pipe 通知
    char ch = 1;
    int n = write(pipeFds_[1], &ch, 1);
    if (n < 0) {
        spdlog::error("write pipe error: {}", strerror(errno));
    }
}

void SubReactor::addSendEvent(FdWrapper& fdw) {
    spdlog::info("add send event on fd={}", fdw.fd());
    fdw.setEvents(fdw.events() | EPOLLOUT);
    int ret = epoll_.operateFd(fdw, EPOLL_CTL_MOD);
    if (ret < 0) {
        spdlog::error("add send event error: {}", strerror(errno));
    }
}

void SubReactor::disableSendEvent(FdWrapper& fdw) {
    spdlog::info("disable send event on fd={}", fdw.fd());
    fdw.setEvents(fdw.events() & ~EPOLLOUT);
    int ret = epoll_.operateFd(fdw, EPOLL_CTL_MOD);
    if (ret < 0) {
        spdlog::error("disable send event error: {}", strerror(errno));
    }
}


void SubReactor::disableReadEventAndShutdown(FdWrapper& fdw) {
    spdlog::info("disable read event and shutdown on fd={}", fdw.fd());
    fdw.setEvents(fdw.events() & ~EPOLLIN);
    int ret = epoll_.operateFd(fdw, EPOLL_CTL_MOD);
    if (ret < 0) {
        spdlog::error("disable read event and shutdown error: {}", strerror(errno));
    }
    shutdown(fdw.fd(), SHUT_RD);
}


void SubReactor::removeConnection(FdWrapper& fdw) {
    spdlog::info("remove connection on fd={}", fdw.fd());
    connectionMap_.erase(fdw.fd());
    deleteEpollFd(fdw);
    close(fdw.fd());
}

void SubReactor::handlePipe() {
    char buffer[1024];
    int n = read(pipeFds_[0], buffer, sizeof(buffer));
    if (n < 0) {
        spdlog::error("read pipe error: {}", strerror(errno));
    }
    processNewConnections();
}

void SubReactor::processNewConnections() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!newConnections_.empty()) {
        auto fd = newConnections_.front();
        FdWrapper fw(fd, EPOLLIN | EPOLLET);
        addEpollFd(fw); // 将新连接的 fd 添加到 epoll 中
        newConnections_.pop();
        auto conn = std::make_shared<Connection>(FdWrapper(fd, EPOLLIN | EPOLLET), this);
        connectionMap_[fd] = conn; // 将连接添加到映射中
        spi_->onAccepted(conn); // 通知 SPI 新连接已建立
        spdlog::info("New connection accepted: fd={}", fd);
    }
}

void SubReactor::handleEvent(FdWrapper& fdw) {
    if (fdw.fd() == pipeFds_[0]) {
        handlePipe();
    } else {
        if (fdw.events() & EPOLLIN) {
            handleRead(fdw);
        }
        if (fdw.events() & EPOLLOUT) {
            handleWrite(fdw);
        }
        if (fdw.events() & EPOLLHUP) {
            spdlog::info("Connection closed on fd={}", fdw.fd());
            spi_->onDisconnected(connectionMap_[fdw.fd()], 2, "manba out");
            removeConnection(fdw);
        }
    }
}

void SubReactor::handleRead(FdWrapper& fdw) {
    std::vector<char> buffer;
    const size_t chunkSize = 4; // 每次读取的块大小
    ssize_t totalRead = 0;
    ssize_t n = 0;

    do {
        buffer.resize(totalRead + chunkSize); // 动态扩容
        n = read(fdw.fd(), buffer.data() + totalRead, chunkSize);
        if (n > 0) totalRead += n;
    } while (n > 0); // 持续读取直到无数据

    // 处理读取结果
    if (totalRead <= 0) {
        // 处理错误或连接关闭
        spdlog::info("Connection closed or read error on fd={}, n = {}", fdw.fd(), n);
        spi_->onDisconnected(connectionMap_[fdw.fd()], 1, "what can I say");
        removeConnection(fdw);
    } else {
        spdlog::info("Received data: {}, len = {}", std::string(buffer.data(), totalRead), totalRead);
        // 传递完整数据给 SPI
        auto it = connectionMap_.find(fdw.fd());
        if (it != connectionMap_.end()) {
            spi_->onMessage(it->second, buffer.data(), totalRead); // 通知 SPI 收到消息
        } else {
            spdlog::warn("No connection found for fd={}", fdw.fd());
        }
    }
}

void SubReactor::handleWrite(FdWrapper fdw) {
    auto conn = connectionMap_[fdw.fd()];
    if (conn->sendBuffer().noData()) {
        spdlog::warn("No data to write on fd={}", conn->fdWrapper().fd());
        return;
    }
    ssize_t n = write(conn->fdWrapper().fd(), conn->sendBuffer().data(), conn->sendBuffer().size());
    if (n < 0) {
        spdlog::error("Write error on fd={}: {}", conn->fdWrapper().fd(), strerror(errno));
        spi_->onDisconnected(conn, 3, "write error");
        removeConnection(conn->fdWrapper());
    } else {
        spdlog::info("Write response length: {}", n);
        conn->sendBuffer().advance(n);
        if (conn->sendBuffer().noData()) {
            spdlog::info("No data to write on fd={}", conn->fdWrapper().fd());
            disableSendEvent(conn->fdWrapper());
            conn->checkNeedClose();
        } else {
            spdlog::info("Data to write on fd={}, left: {}", conn->fdWrapper().fd(), conn->sendBuffer().size());
        }
    }
}
