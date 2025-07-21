#ifndef FIX42_PROTOCOL_H
#define FIX42_PROTOCOL_H

#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

// FIX4.2协议处理类
class FIX42Protocol {
public:
    // 消息类型枚举
    enum class MessageType {
        LOGON,          // A
        LOGOUT,         // 5
        HEARTBEAT,      // 0
        TEST_REQUEST,   // 1
        RESEND_REQUEST, // 2
        REJECT,         // 3
        SEQUENCE_RESET, // 4
        NEW_ORDER_SINGLE, // D
        ORDER_CANCEL_REQUEST, // F
        EXECUTION_REPORT, // 8
        ORDER_CANCEL_REJECT // 9
    };

    // 订单方向
    enum class Side {
        BUY,    // 1
        SELL    // 2
    };

    // 订单类型
    enum class OrdType {
        MARKET, // 1
        LIMIT   // 2
    };

    // 时间在 force 后的格式
    enum class TimeInForce {
        DAY,        // 0
        IMMEDIATE_OR_CANCEL, // 3
        FILL_OR_KILL // 4
    };

    // 执行类型
    enum class ExecType {
        NEW,            // 0
        PARTIAL_FILL,   // 1
        FILL,           // 2
        CANCELLED,      // 4
        REJECTED        // 8
    };

    // 消息结构
    struct Message {
        std::map<int, std::string> fields;
        std::string raw_data;
    };

    // 回调函数类型定义
    using OnLogonHandler = std::function<void(bool success, const std::string& reason)>;
    using OnLogoutHandler = std::function<void(const std::string& reason)>;
    using OnExecutionReportHandler = std::function<void(
        const std::string& order_id, 
        ExecType exec_type, 
        const std::string& exec_id,
        double price, 
        int quantity, 
        const std::string& reason)>;
    using OnCancelRejectHandler = std::function<void(
        const std::string& order_id, 
        const std::string& reason)>;

    // 构造函数
    FIX42Protocol(const std::string& sender_comp_id, 
                  const std::string& target_comp_id,
                  int heartbt_int = 30);

    // 析构函数
    ~FIX42Protocol();

    // 连接到FIX服务器
    bool connect(const std::string& host, int port);

    // 断开连接
    void disconnect();

    // 登录到FIX服务器
    void logon(OnLogonHandler handler);

    // 登出
    void logout(const std::string& reason = "");

    // 发送限价单
    std::string sendLimitOrder(const std::string& symbol, 
                              Side side, 
                              double price, 
                              int quantity,
                              TimeInForce time_in_force = TimeInForce::DAY);

    // 撤单
    void cancelOrder(const std::string& order_id, 
                    const std::string& symbol,
                    Side side,
                    double price);

    // 设置回调函数
    void setOnExecutionReportHandler(OnExecutionReportHandler handler);
    void setOnCancelRejectHandler(OnCancelRejectHandler handler);

private:
    // 生成消息序号
    uint32_t getNextSenderMsgSeqNum();
    uint32_t getNextTargetMsgSeqNum();
    
    // 序列化消息
    std::string serialize(const Message& msg);
    
    // 反序列化消息
    bool deserialize(const std::string& raw_msg, Message& msg);
    
    // 创建基础消息
    Message createBaseMessage(MessageType msg_type);
    
    // 发送消息
    bool sendMessage(const Message& msg);
    
    // 处理接收到的消息
    void processReceivedMessage(const Message& msg);
    
    // 处理心跳
    void sendHeartbeat();
    void startHeartbeatTimer();
    void stopHeartbeatTimer();
    void heartbeatLoop();
    
    // 校验消息
    bool validateMessage(const Message& msg);
    
    // 生成校验和
    std::string calculateChecksum(const std::string& msg);
    
    // 转换枚举到字符串
    std::string messageTypeToString(MessageType type);
    std::string sideToString(Side side);
    std::string ordTypeToString(OrdType type);
    std::string timeInForceToString(TimeInForce tif);
    
    // 转换字符串到枚举
    MessageType stringToMessageType(const std::string& str);
    ExecType stringToExecType(const std::string& str);

    // 网络相关
    int socket_fd_;
    std::string host_;
    int port_;
    std::thread receive_thread_;
    std::atomic<bool> is_connected_;
    std::atomic<bool> is_logged_on_;

    // FIX协议相关
    std::string sender_comp_id_;
    std::string target_comp_id_;
    int heartbt_int_;
    uint32_t sender_msg_seq_num_;
    uint32_t target_msg_seq_num_;
    std::string last_test_request_id_;
    
    // 线程同步
    std::mutex mutex_;
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_;
    
    // 回调函数
    OnLogonHandler on_logon_handler_;
    OnLogoutHandler on_logout_handler_;
    OnExecutionReportHandler on_execution_report_handler_;
    OnCancelRejectHandler on_cancel_reject_handler_;

    // 接收消息循环
    void receiveLoop();
};

#endif // FIX42_PROTOCOL_H
