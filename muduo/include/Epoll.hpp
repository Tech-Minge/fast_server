#pragma once
#include <vector>
#include <sys/epoll.h>

class FdWrapper {
public:
    FdWrapper(int fd, uint32_t events) : fd_(fd), events_(events) {}
    int fd() const { return fd_; }
    int events() const { return events_; }
    void setEvents(int events) { events_ = events; }
private:
    int fd_;
    uint32_t events_;
};

class Epoll {
public:
    Epoll(): epollFd_(::epoll_create1(EPOLL_CLOEXEC)), events_(16) {
        if (epollFd_ < 0) {
        }
    }
    ~Epoll() = default;

    // void addFd(int fd, int events);
    // void modifyFd(int fd, int events);
    // void removeFd(int fd);

    int doEpoll(int timeoutMs, std::vector<FdWrapper>& fds);

    int operateFd(FdWrapper fw, int operation) {
        epoll_event event;
        event.data.fd = fw.fd();
        event.events = fw.events();
        int ret = epoll_ctl(epollFd_, operation, fw.fd(), &event);
        return ret;
    }

private:
    int epollFd_;
    std::vector<epoll_event> events_; // 存储发生事件的文件描述符
};