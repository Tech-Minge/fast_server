#pragma once

#include <memory>
#include "Connection.hpp"
#include "Epoll.hpp"
#include "TcpSpi.hpp"
#include "MainReactor.hpp"
#include "Signal.hpp"
class TcpApi {
public:
    void bindAddress(const char* ip, int port) {
        mainReactor_.bindAddress(ip, port);
    }

    void registerSpi(TcpSpi* spi) {
        mainReactor_.setSpi(spi);
    }

    void run() {
        mainReactor_.run();
    }
private:
    MainReactor mainReactor_;
    Signal signal_;
};