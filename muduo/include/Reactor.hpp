#pragma once

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <cstring>
#include <atomic>
#include "Epoll.hpp"
#include "TcpSpi.hpp"

class Reactor {
public:
    Reactor() {
    }

    virtual ~Reactor() {
        
    }

    // 主事件循环
    virtual void run() {
        loop();
    }
    void loop() {
        while (true) {
            std::vector<FdWrapper> events;
            epoll_.doEpoll(-1, events);
            for (auto& event : events) {
                handleEvent(event);
            }
            if (!isRunning_) {
                break;
            }
        }
    }

    virtual void setSpi(TcpSpi *spi) {
        spi_ = spi;
    }

protected:
    // 处理事件
    virtual void handleEvent(FdWrapper& event) = 0;

   
    // 注册事件
    void addEpollFd(const FdWrapper& fdw);

    // 修改事件
    void modifyEpollFd(const FdWrapper& fdw);

    // 删除事件
    void deleteEpollFd(FdWrapper& fdw);

protected:
    Epoll epoll_;
    TcpSpi *spi_ = nullptr;
    std::atomic<bool> isRunning_ {false};
};
