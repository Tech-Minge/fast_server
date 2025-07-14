#pragma once

#include "spdlog/spdlog.h"
#include "Reactor.hpp"
#include <memory>
#include <unordered_map>
#include "Connection.hpp"
#include <functional>
#include <chrono>
#include <set>
class SubReactor: public Reactor, public std::enable_shared_from_this<SubReactor> {
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    using ConnectionMap = std::unordered_map<int, ConnectionPtr>;
    SubReactor();

    virtual ~SubReactor();

    void start();
    void stop(); // 停止子反应器
    void join(); // 等待子线程结束
     // 将新连接加入队列
    void enqueueNewConnection(int fd);

    void enqueueSend(int fd);
    
    void disableReadEventAndShutdown(FdWrapper& fdw);
    void removeConnection(FdWrapper& fdw);

    int64_t registerTimer(int64_t interval_ms, std::function<void()> callback, bool recurring = false);
    bool cancelTimer(int64_t timer_id);
    void handleTimerEvents();

    void handlePipe();

    void processSendQueue(); // 处理发送队列中的事件

    // 处理新连接
    void processNewConnections();

    void handleEvent(FdWrapper& event) override;

    void handleRead(FdWrapper& fdw);

    void handleWrite(FdWrapper fdw);


private:
    int pipeFds_[2]; // pipe 用于通知新连接
    std::thread thread_;
    ConnectionMap connectionMap_; // 管理连接的映射
    std::queue<int> newConnections_;
    std::queue<int> sendQueue_;
    std::mutex queueMutex_;

    int timer_fd_ = -1; // timerfd 文件描述符
    std::atomic<int64_t> next_timer_id_{0}; // 定时器ID生成器

    struct TimerInfo {
        int64_t id;
        int64_t interval_ms;
        std::function<void()> callback;
        bool recurring;
        std::chrono::steady_clock::time_point expiration;
        
        bool operator<(const TimerInfo& other) const {
            return expiration < other.expiration;
        }
    };

    std::set<TimerInfo> timers_;
    std::mutex timers_mutex_;
};