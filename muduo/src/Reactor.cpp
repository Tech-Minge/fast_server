#include "Reactor.hpp"
#include "spdlog/spdlog.h"

void Reactor::modifyEpollFd(const FdWrapper& fdw) {
    if (epoll_.operateFd(fdw, EPOLL_CTL_MOD) < 0) {
        spdlog::error("Failed to modify fd {} in epoll", fdw.fd());
    }
}

void Reactor::addEpollFd(const FdWrapper& fdw) {
    if (epoll_.operateFd(fdw, EPOLL_CTL_ADD) < 0) {
        spdlog::error("Failed to add fd {} to epoll", fdw.fd());
    }
}

void Reactor::deleteEpollFd(FdWrapper& fdw) {
    fdw.setEvents(0);
    if (epoll_.operateFd(fdw, EPOLL_CTL_DEL) < 0) {
        spdlog::error("Failed to delete fd {} from epoll", fdw.fd());
    }
}
