#include "SubReactor.hpp"
#include "spdlog/spdlog.h"
#include <sys/timerfd.h>
#include "Utils.hpp"
#include <chrono>

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

    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ == -1) {
        throw std::runtime_error("Failed to create timerfd");
    }
    
    // 将 timerfd 添加到 epoll 监听
    
    addEpollFd({ timer_fd_, EPOLLIN | EPOLLET });
}

SubReactor::~SubReactor() {
    close(pipeFds_[0]);
    close(pipeFds_[1]);
    if (timer_fd_ != -1) {
        close(timer_fd_);
        timer_fd_ = -1;
    }
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
    connSpinlock_.lock();        
    newConnections_.push_back(fd);
    connSpinlock_.unlock();
    // 写入 pipe 通知
    char ch = 1;
    int n = write(pipeFds_[1], &ch, 1);
    if (n < 0) {
        spdlog::error("write pipe error: {}", strerror(errno));
    }
}

void SubReactor::enqueueSend(int fd) {
    sendSpinlock_.lock();
    sendQueue_.push_back(fd);
    sendSpinlock_.unlock();
    // spdlog::info("Enqueued send for fd={}", fd);
    // 写入 pipe 通知
    char ch = 1;
    int n = write(pipeFds_[1], &ch, 1);
    if (n < 0) {
        spdlog::error("write pipe error: {}", strerror(errno));
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
    processSendQueue();
}

void SubReactor::processSendQueue() {
    ScopedTimer timer(__func__);
    std::vector<int> sendQueue;
    {
        sendSpinlock_.lock();
        sendQueue = std::move(sendQueue_);
        sendSpinlock_.unlock();
    }

    while (!sendQueue.empty()) {
        auto fd = sendQueue.back();
        sendQueue.pop_back();
        handleWrite(FdWrapper(fd, EPOLLOUT | EPOLLHUP | EPOLLET));
    }
}

void SubReactor::processNewConnections() {
    ScopedTimer timer(__func__);
    connSpinlock_.lock();
    auto newConnections = std::move(newConnections_);
    connSpinlock_.unlock();

    while (!newConnections.empty()) {
        auto fd = newConnections.back();
        newConnections.pop_back();
        
        addEpollFd(FdWrapper(fd, EPOLLIN | EPOLLHUP | EPOLLET));
        {
            ScopedTimer timer("CreateConnection");
            connectionMap_[fd] = std::make_shared<Connection>(FdWrapper(fd, EPOLLIN | EPOLLHUP | EPOLLET), this);
            spi_->onAccepted(connectionMap_[fd]);
        }
    }
}

void SubReactor::handleEvent(FdWrapper& fdw) {
    if (fdw.fd() == pipeFds_[0]) {
        handlePipe();
    } else if (fdw.fd() == timer_fd_) {
        handleTimerEvents();
    } 
    else {
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
    constexpr size_t chunkSize = 1024 * 8;
    char buffer[chunkSize];

    ssize_t totalRead = 0;
    ssize_t n = 0;
    ScopedTimer timer(__func__);
    
    auto it = connectionMap_.find(fdw.fd());
    if (it == connectionMap_.end()) {
        spdlog::warn("No connection found for fd={}", fdw.fd());
        return;
    }
    {
        ScopedTimer timer("ReadLoop");
        do {
            n = read(fdw.fd(), buffer, chunkSize);
            if (n > 0) {
                totalRead += n;
                spi_->onMessage(it->second, buffer, n);
            }
        } while (n > 0);
    }
    

    // 处理读取结果
    if (totalRead <= 0) {
        // 处理错误或连接关闭
        spdlog::info("Connection closed or read error on fd={}, n = {}", fdw.fd(), n);
        spi_->onDisconnected(connectionMap_[fdw.fd()], 1, "what can I say");
        removeConnection(fdw);
    }
    
}

void SubReactor::handleWrite(FdWrapper fdw) {
    auto conn = connectionMap_[fdw.fd()];
    if (conn->sendBuffer().noData()) {
        spdlog::debug("No data to write on fd={}", conn->fdWrapper().fd());
        return;
    }

    ScopedTimer timer(__func__);
    {
        ScopedTimer timer("Purewrite");

        do {
            auto writeChunkSize = std::min(conn->sendBuffer().size(), static_cast<size_t>(1024 * 8)); // 每次写入4096字节
            ssize_t n;
            {
                n = write(conn->fdWrapper().fd(), conn->sendBuffer().data(), writeChunkSize);
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // spdlog::debug("Write would block on fd={}", conn->fdWrapper().fd());
                    break; // 非阻塞模式下，退出循环
                } else {
                    spi_->onDisconnected(conn, 3, "write error");
                    removeConnection(conn->fdWrapper());
                    return;
                }
            } else if (n == 0) {
                break; // 连接可能已经关闭
            } else {
                conn->sendBuffer().advance(n); // 更新发送缓冲区
            }
        } while (conn->sendBuffer().size() > 0);
    }

}


int64_t SubReactor::registerTimer(int64_t interval_ms, std::function<void()> callback, bool recurring) {
    if (interval_ms <= 0 || !callback) {
        return -1; // 无效参数
    }
    
    auto now = std::chrono::steady_clock::now();
    auto expiration = now + std::chrono::milliseconds(interval_ms);
    
    TimerInfo timer{
        .id = next_timer_id_.fetch_add(1, std::memory_order_relaxed),
        .interval_ms = interval_ms,
        .callback = std::move(callback),
        .recurring = recurring,
        .expiration = expiration
    };
    spdlog::info("Registering timer with ID: {}, interval: {} ms, recurring: {}", timer.id, timer.interval_ms, timer.recurring);
    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        auto [it, inserted] = timers_.insert(std::move(timer));
        if (it == timers_.begin()) {
            updateNextTimer(); // 如果是最近到期的定时器，更新 timerfd
        }
    }
    
    return timer.id;
}


// 取消定时器
bool SubReactor::cancelTimer(int64_t timer_id) {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    
    auto it = std::find_if(timers_.begin(), timers_.end(), 
        [timer_id](const TimerInfo& t) { return t.id == timer_id; });
    
    if (it == timers_.end()) {
        return false; // 未找到
    }
    
    bool was_first = (it == timers_.begin());
    timers_.erase(it);
    
    if (was_first && !timers_.empty()) {
        updateNextTimer(); // 如果删除的是最近到期的定时器，更新下一个
    } else if (timers_.empty()) {
        disableTimer(); // 没有定时器时禁用
    }
    
    return true;
}

void SubReactor::updateNextTimer() {
    // std::lock_guard<std::mutex> lock(timers_mutex_);
    if (timers_.empty()) {
        disableTimer();
        return;
    }
    
    const auto& next_timer = *timers_.begin();
    auto now = std::chrono::steady_clock::now();
    auto duration = next_timer.expiration - now;
    
    // 计算超时时间（毫秒转纳秒）
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    if (ns < 0) ns = 0; // 确保非负
    
    // 设置 timerfd
    struct itimerspec new_value;
    new_value.it_value.tv_sec = ns / 1000000000;
    new_value.it_value.tv_nsec = ns % 1000000000;
    new_value.it_interval = {0, 0}; // 单次触发
    spdlog::info("Setting timerfd with expiration in {} ns", ns);
    if (timerfd_settime(timer_fd_, 0, &new_value, nullptr) == -1) {
        spdlog::error("timerfd_settime failed");
        exit(-1);
    }
}

// 禁用定时器
void SubReactor::disableTimer() {
     struct itimerspec new_value = {};
     timerfd_settime(timer_fd_, 0, &new_value, nullptr);
}



// 处理定时器事件
void SubReactor::handleTimerEvents() {
    // 读取 timerfd 事件计数
    spdlog::info("Handling timer events");
    uint64_t expirations;
    ssize_t n = read(timer_fd_, &expirations, sizeof(expirations));
    if (n != sizeof(expirations)) {
        // 处理读取错误
        return;
    }
    
    // 获取当前时间
    auto now = std::chrono::steady_clock::now();
    std::vector<TimerInfo> expired_timers;
    std::vector<TimerInfo> recurring_timers;
    
    {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        
        // 收集所有过期定时器
        for (auto it = timers_.begin(); it != timers_.end() && it->expiration <= now;) {
            expired_timers.push_back(*it);
            it = timers_.erase(it);
        }
        
        // 处理周期性定时器
        for (auto& timer : expired_timers) {
            if (timer.recurring) {
                TimerInfo new_timer = timer;
                new_timer.expiration = now + std::chrono::milliseconds(timer.interval_ms);
                recurring_timers.push_back(new_timer);
            }
        }
        
        // 重新插入周期性定时器
        for (auto& timer : recurring_timers) {
            timers_.insert(std::move(timer));
        }
        
        // 更新下一个定时器
        if (!timers_.empty()) {
            updateNextTimer();
        } else {
            disableTimer();
        }
    }
    
    // 执行过期定时器的回调（在锁外执行）
    for (auto& timer : expired_timers) {
        try {
            timer.callback();
        } catch (...) {
            // 处理回调异常
        }
    }
}



/*
int main() {
    auto reactor = std::make_shared<SubReactor>();
    reactor->start();
    
    // 注册一次性定时器
    reactor->registerTimer(1000, [] {
        std::cout << "Timer fired after 1 second\n";
    });
    
    // 注册周期性定时器
    int64_t periodic_id = reactor->registerTimer(500, [] {
        std::cout << "Periodic timer every 500ms\n";
    }, true);
    
    // 10秒后取消周期性定时器
    reactor->registerTimer(10000, [reactor, periodic_id] {
        reactor->cancelTimer(periodic_id);
        std::cout << "Periodic timer cancelled\n";
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(15));
    reactor->stop();
    reactor->join();
    
    return 0;
}
*/