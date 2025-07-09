#pragma once


#include "Reactor.hpp"
#include <memory>
#include <unordered_map>
#include "Connection.hpp"

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

    void addSendEvent(FdWrapper& fdw);
    void disableSendEvent(FdWrapper& fdw);
    void disableReadEventAndShutdown(FdWrapper& fdw);
    void removeConnection(FdWrapper& fdw);
     // 处理 pipe 通知
    void handlePipe();

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
    std::mutex queueMutex_;
};