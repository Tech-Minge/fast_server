#include "MainReactor.hpp"
#include "SubReactor.hpp"
#include "spdlog/spdlog.h"

MainReactor::MainReactor(int sub_reactors_count) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        spdlog::error("socket listen fd failed");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    for (int i = 0; i < sub_reactors_count; ++i) {
        sub_reactors_.push_back(std::make_shared<SubReactor>());
    }
}

MainReactor::~MainReactor() {
    spdlog::info("MainReactor destructor called, closing listen socket.");
    close(listen_fd_);
    for (auto& sub_reactor : sub_reactors_) {
        sub_reactor->stop();
    }
    for (auto& sub_reactor : sub_reactors_) {
        sub_reactor->join();
    }
}

void MainReactor::bindAddress(const char* address, int port) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(address);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("bind failed {}:{}", address, port);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd_, SOMAXCONN) < 0) {
        spdlog::error("listen failed {}:{}", address, port);
        exit(EXIT_FAILURE);
    }
    spdlog::info("MainReactor bind {}:{}", address, port);
    FdWrapper fw(listen_fd_, EPOLLIN | EPOLLET);
    addEpollFd(fw); // 将监听套接字添加到 epoll 中
}

void MainReactor::handleAccept() {
    sockaddr_in client_addr = {};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        spdlog::error("accept failed: {}", strerror(errno));
        return;
    }

    // 设置非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    // 分发到子线程
    auto sub_reactor = sub_reactors_[next_sub_idx_++ % sub_reactors_.size()];
    sub_reactor->enqueueNewConnection(client_fd);
}

void MainReactor::run() {
    for (auto& sub_reactor : sub_reactors_) {
        sub_reactor->start();
    }
    isRunning_ = true;
    spdlog::info("MainReactor start running");
    loop();
}

void MainReactor::setSpi(TcpSpi *spi) {
    spi_ = spi;
    for (auto& sub_reactor : sub_reactors_) {
        sub_reactor->setSpi(spi);
    }
}
