#pragma once

#include "Reactor.hpp"
#include <arpa/inet.h>

class SubReactor;

class MainReactor : public Reactor {
public:
    MainReactor(int sub_reactors_count = 2);

    virtual void setSpi(TcpSpi *spi) override;
    virtual void run() override;

    void bindAddress(const char* address, int port);

    virtual ~MainReactor();

protected:
    void handleEvent(FdWrapper &fdw) override {
        if (fdw.fd() == listen_fd_) {
            handleAccept();
        }
    }

    void handleAccept();

private:
    int listen_fd_;
    std::vector<std::shared_ptr<SubReactor>> sub_reactors_;
    std::atomic<int> next_sub_idx_;
};