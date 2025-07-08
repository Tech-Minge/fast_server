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

class Reactor: public std::enable_shared_from_this<Reactor> {
public:
    Reactor() {
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }

        // 创建 pipe 用于通知新连接
        if (pipe(pipe_fds_) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        // 设置非阻塞
        fcntl(pipe_fds_[0], F_SETFL, O_NONBLOCK);
        fcntl(pipe_fds_[1], F_SETFL, O_NONBLOCK);

        // 将 pipe 读端加入 epoll
        struct epoll_event ev = {};
        ev.data.fd = pipe_fds_[0];
        ev.events = EPOLLIN | EPOLLET;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, pipe_fds_[0], &ev) < 0) {
            perror("epoll_ctl add pipe");
            exit(EXIT_FAILURE);
        }
    }

    virtual ~Reactor() {
        close(epoll_fd_);
        close(pipe_fds_[0]);
        close(pipe_fds_[1]);
    }

    // 将新连接加入队列
    void enqueueNewConnection(int fd) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        new_connections_.push(fd);
        // 写入 pipe 通知
        char ch = 1;
        write(pipe_fds_[1], &ch, 1);
    }

    // 主事件循环
    virtual void run() {
        while (true) {
            struct epoll_event events[1024];
            int nfds = epoll_wait(epoll_fd_, events, 1024, -1);
            for (int i = 0; i < nfds; ++i) {
                handleEvent(events[i]);
            }
        }
    }

protected:
    // 处理事件
    virtual void handleEvent(const struct epoll_event& event) = 0;

    // 处理 pipe 通知
    void handlePipe() {
        char buffer[1024];
        read(pipe_fds_[0], buffer, sizeof(buffer)); // 读取所有数据
        processNewConnections();
    }

    // 处理新连接
    void processNewConnections() {
        std::vector<int> connections;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!new_connections_.empty()) {
                connections.push_back(new_connections_.front());
                new_connections_.pop();
            }
        }

        for (int fd : connections) {
            addEvent(fd, EPOLLIN | EPOLLET);
        }
    }

    // 注册事件
    void addEvent(int fd, uint32_t events) {
        struct epoll_event ev = {};
        ev.data.fd = fd;
        ev.events = events;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            perror("epoll_ctl add");
        }
    }

    // 修改事件
    void modEvent(int fd, uint32_t events) {
        struct epoll_event ev = {};
        ev.data.fd = fd;
        ev.events = events;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
            perror("epoll_ctl mod");
        }
    }

    // 删除事件
    void delEvent(int fd) {
        struct epoll_event ev = {};
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ev) < 0) {
            perror("epoll_ctl del");
        }
    }

protected:
    int epoll_fd_;
    int pipe_fds_[2]; // pipe 用于通知新连接
    std::queue<int> new_connections_;
    std::mutex queue_mutex_;
};

class SubReactor;

class MainReactor : public Reactor {
public:
    MainReactor(int port, int sub_reactors_count)
        : listen_fd_(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)),
          sub_reactors_(sub_reactors_count),
          next_sub_idx_(0) {
        for (int i = 0; i < sub_reactors_count; ++i) {
            sub_reactors_[i] = std::make_shared<SubReactor>();
        }
        if (listen_fd_ < 0) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        int optval = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("bind");
            exit(EXIT_FAILURE);
        }

        if (listen(listen_fd_, SOMAXCONN) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        addEvent(listen_fd_, EPOLLIN);
    }

    virtual ~MainReactor() {
        close(listen_fd_);
    }

protected:
    void handleEvent(const struct epoll_event& event) override {
        if (event.data.fd == listen_fd_) {
            handleAccept();
        }
    }

    void handleAccept() {
        sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) {
            perror("accept");
            return;
        }

        // 设置非阻塞
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        // 分发到子线程
        auto sub_reactor = sub_reactors_[next_sub_idx_++ % sub_reactors_.size()];
        sub_reactor->enqueueNewConnection(client_fd);
    }

private:
    int listen_fd_;
    std::vector<std::shared_ptr<SubReactor>> sub_reactors_;
    std::atomic<int> next_sub_idx_;
};


class SubReactor : public Reactor {
protected:
    void start() {
        thread_ = std::thread([thisPtr = shared_from_this()]() {
            std::cout << thisPtr.use_count() << " SubReactor thread started." << std::endl;
        });
    }
    void handleEvent(const struct epoll_event& event) override {
        if (event.data.fd == pipe_fds_[0]) {
            handlePipe();
        } else {
            if (event.events & EPOLLIN) {
                handleRead(event.data.fd);
            }
            if (event.events & EPOLLOUT) {
                handleWrite(event.data.fd);
            }
        }
    }

    void handleRead(int fd) {
        char buffer[1024];
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            delEvent(fd);
            close(fd);
        } else {
            std::cout << "Received data: " << std::string(buffer, n) << std::endl;
            modEvent(fd, EPOLLOUT); // 注册写事件
        }
    }

    void handleWrite(int fd) {
        const char* response = "Hello from server!\n";
        ssize_t n = write(fd, response, strlen(response));
        if (n < 0) {
            perror("write");
        } else {
            modEvent(fd, EPOLLIN); // 恢复读事件
        }
    }
private:
    std::thread thread_;
};