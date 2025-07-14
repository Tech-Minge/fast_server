#pragma once

#include <memory>
#include "Connection.hpp"
#include "Epoll.hpp"
#include "TcpSpi.hpp"
#include "MainReactor.hpp"
#include "Signal.hpp"
#include "spdlog/spdlog.h"
class TcpApi {
public:
    TcpApi() {
    }
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