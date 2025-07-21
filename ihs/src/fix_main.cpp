#include "fix42.hpp"
#include <iostream>
#include <thread>
#include <chrono>

// 全局变量存储订单ID，用于撤单示例
std::string g_order_id;

int main() {
    // 创建FIX42协议实例
    FIX42Protocol fix("CLIENT1", "SERVER1", 30); // 心跳间隔30秒

    // 设置回调函数
    fix.setOnLogonHandler([&](bool success, const std::string& reason) {
        if (success) {
            std::cout << "Logon successful: " << reason << std::endl;
            
            // 登录成功后发送一个限价单
            g_order_id = fix.sendLimitOrder("AAPL", 
                                           FIX42Protocol::Side::BUY, 
                                           150.50, 
                                           100);
            std::cout << "Sent limit order, ID: " << g_order_id << std::endl;
        } else {
            std::cout << "Logon failed: " << reason << std::endl;
        }
    });

    fix.setOnExecutionReportHandler([&](const std::string& order_id, 
                                       FIX42Protocol::ExecType exec_type, 
                                       const std::string& exec_id,
                                       double price, 
                                       int quantity, 
                                       const std::string& reason) {
        std::cout << "Execution report - Order ID: " << order_id 
                  << ", Exec Type: " << static_cast<int>(exec_type)
                  << ", Exec ID: " << exec_id
                  << ", Price: " << price
                  << ", Quantity: " << quantity
                  << ", Reason: " << reason << std::endl;

        // 如果订单被接受，5秒后尝试撤单
        if (exec_type == FIX42Protocol::ExecType::NEW && !order_id.empty()) {
            std::thread([&, order_id]() {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                std::cout << "Cancelling order: " << order_id << std::endl;
                fix.cancelOrder(order_id, "AAPL", FIX42Protocol::Side::BUY, 150.50);
            }).detach();
        }
    });

    fix.setOnCancelRejectHandler([](const std::string& order_id, const std::string& reason) {
        std::cout << "Cancel rejected - Order ID: " << order_id << ", Reason: " << reason << std::endl;
    });

    // 连接到FIX服务器
    if (fix.connect("127.0.0.1", 1234)) {
        std::cout << "Connected to FIX server, sending logon..." << std::endl;
        fix.logon([](bool success, const std::string& reason) {});

        // 运行30秒后登出
        std::this_thread::sleep_for(std::chrono::seconds(30));
        fix.logout("Normal exit");
    } else {
        std::cerr << "Failed to connect to FIX server" << std::endl;
    }

    // 断开连接
    fix.disconnect();
    std::cout << "Exiting..." << std::endl;

    return 0;
}
