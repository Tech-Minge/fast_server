#include "Epoll.hpp"
#include "spdlog/spdlog.h"


int Epoll::doEpoll(int timeoutMs, std::vector<FdWrapper>& fds) {
    spdlog::debug("Epoll::doEpoll called with timeout: {}", timeoutMs);
    int numEvents = ::epoll_wait(epollFd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    if (numEvents < 0) {
        // Handle error
    }
    for (int i = 0; i < numEvents; ++i) {
        fds.push_back({
            events_[i].data.fd,
            events_[i].events
        });
    }
    return 0;
}
