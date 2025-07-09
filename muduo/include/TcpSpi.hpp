#pragma once
#include <memory>
#include "Connection.hpp"

class TcpSpi {
public:
    virtual void onAccepted(std::shared_ptr<Connection> conn) = 0;
    virtual void onDisconnected(std::shared_ptr<Connection> conn, int r, const char* reason) = 0;
    virtual void onMessage(std::shared_ptr<Connection> conn, const char* data, size_t len) = 0;
};