#include "fix42_protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <iostream>
#include <algorithm>

// 字段标签定义
namespace FIXField {
    const int BeginString = 8;
    const int BodyLength = 9;
    const int MsgType = 35;
    const int SenderCompID = 49;
    const int TargetCompID = 56;
    const int MsgSeqNum = 34;
    const int SendingTime = 52;
    const int CheckSum = 10;
    const int HeartBtInt = 108;
    const int ResetSeqNumFlag = 141;
    const int TestReqID = 112;
    const int Text = 58;
    const int ClOrdID = 11;
    const int Symbol = 55;
    const int Side = 54;
    const int OrdType = 40;
    const int Price = 44;
    const int OrderQty = 38;
    const int TimeInForce = 59;
    const int OrigClOrdID = 41;
    const int ExecType = 150;
    const int ExecID = 17;
    const int OrdStatus = 39;
    const int LeavesQty = 151;
    const int CumQty = 14;
    const int AvgPx = 6;
}

FIX42Protocol::FIX42Protocol(const std::string& sender_comp_id, 
                             const std::string& target_comp_id,
                             int heartbt_int)
    : sender_comp_id_(sender_comp_id),
      target_comp_id_(target_comp_id),
      heartbt_int_(heartbt_int),
      sender_msg_seq_num_(1),
      target_msg_seq_num_(0),
      socket_fd_(-1),
      is_connected_(false),
      is_logged_on_(false),
      heartbeat_running_(false) {}

FIX42Protocol::~FIX42Protocol() {
    disconnect();
}

bool FIX42Protocol::connect(const std::string& host, int port) {
    if (is_connected_) {
        return true;
    }

    host_ = host;
    port_ = port;

    // 创建套接字
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    // 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // 转换IP地址
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // 连接服务器
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    is_connected_ = true;

    // 启动接收线程
    receive_thread_ = std::thread(&FIX42Protocol::receiveLoop, this);

    return true;
}

void FIX42Protocol::disconnect() {
    if (!is_connected_) {
        return;
    }

    // 如果已登录，先发送登出消息
    if (is_logged_on_) {
        logout("Disconnecting");
    }

    // 停止心跳线程
    stopHeartbeatTimer();

    // 关闭套接字
    is_connected_ = false;
    close(socket_fd_);
    socket_fd_ = -1;

    // 等待接收线程结束
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

void FIX42Protocol::logon(OnLogonHandler handler) {
    if (!is_connected_) {
        if (handler) {
            handler(false, "Not connected to server");
        }
        return;
    }

    on_logon_handler_ = handler;

    // 创建登录消息
    Message msg = createBaseMessage(MessageType::LOGON);
    msg.fields[FIXField::HeartBtInt] = std::to_string(heartbt_int_);
    msg.fields[FIXField::ResetSeqNumFlag] = "Y"; // 重置序列号

    // 发送登录消息
    sendMessage(msg);
}

void FIX42Protocol::logout(const std::string& reason) {
    if (!is_connected_ || !is_logged_on_) {
        return;
    }

    // 创建登出消息
    Message msg = createBaseMessage(MessageType::LOGOUT);
    if (!reason.empty()) {
        msg.fields[FIXField::Text] = reason;
    }

    // 发送登出消息
    sendMessage(msg);
}

std::string FIX42Protocol::sendLimitOrder(const std::string& symbol, 
                                         Side side, 
                                         double price, 
                                         int quantity,
                                         TimeInForce time_in_force) {
    if (!is_connected_ || !is_logged_on_) {
        return "";
    }

    // 生成唯一的订单ID
    std::string cl_ord_id = "ORD" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());

    // 创建新订单消息
    Message msg = createBaseMessage(MessageType::NEW_ORDER_SINGLE);
    msg.fields[FIXField::ClOrdID] = cl_ord_id;
    msg.fields[FIXField::Symbol] = symbol;
    msg.fields[FIXField::Side] = sideToString(side);
    msg.fields[FIXField::OrdType] = ordTypeToString(OrdType::LIMIT);
    msg.fields[FIXField::Price] = std::to_string(price);
    msg.fields[FIXField::OrderQty] = std::to_string(quantity);
    msg.fields[FIXField::TimeInForce] = timeInForceToString(time_in_force);

    // 发送订单消息
    if (sendMessage(msg)) {
        return cl_ord_id;
    }

    return "";
}

void FIX42Protocol::cancelOrder(const std::string& order_id, 
                               const std::string& symbol,
                               Side side,
                               double price) {
    if (!is_connected_ || !is_logged_on_) {
        return;
    }

    // 创建撤单消息
    Message msg = createBaseMessage(MessageType::ORDER_CANCEL_REQUEST);
    msg.fields[FIXField::OrigClOrdID] = order_id;
    msg.fields[FIXField::ClOrdID] = "CAN" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    msg.fields[FIXField::Symbol] = symbol;
    msg.fields[FIXField::Side] = sideToString(side);
    msg.fields[FIXField::Price] = std::to_string(price);

    // 发送撤单消息
    sendMessage(msg);
}

void FIX42Protocol::setOnExecutionReportHandler(OnExecutionReportHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_execution_report_handler_ = handler;
}

void FIX42Protocol::setOnCancelRejectHandler(OnCancelRejectHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_cancel_reject_handler_ = handler;
}

uint32_t FIX42Protocol::getNextSenderMsgSeqNum() {
    std::lock_guard<std::mutex> lock(mutex_);
    return sender_msg_seq_num_++;
}

uint32_t FIX42Protocol::getNextTargetMsgSeqNum() {
    std::lock_guard<std::mutex> lock(mutex_);
    return target_msg_seq_num_++;
}

std::string FIX42Protocol::serialize(const Message& msg) {
    // 构建消息体
    std::string body;
    for (const auto& field : msg.fields) {
        if (field.first != FIXField::BeginString && 
            field.first != FIXField::BodyLength && 
            field.first != FIXField::CheckSum) {
            body += std::to_string(field.first) + "=" + field.second + "\001";
        }
    }

    // 添加头部字段
    std::string header;
    header += std::to_string(FIXField::BeginString) + "=FIX.4.2\001";
    header += std::to_string(FIXField::BodyLength) + "=" + std::to_string(body.length()) + "\001";
    header += body;

    // 计算校验和
    std::string checksum = calculateChecksum(header);

    // 组合完整消息
    std::string full_msg = header + std::to_string(FIXField::CheckSum) + "=" + checksum + "\001";

    return full_msg;
}

bool FIX42Protocol::deserialize(const std::string& raw_msg, Message& msg) {
    msg.raw_data = raw_msg;
    msg.fields.clear();

    size_t pos = 0;
    size_t next_pos = 0;

    while (pos < raw_msg.length()) {
        // 查找字段分隔符
        next_pos = raw_msg.find('\001', pos);
        if (next_pos == std::string::npos) {
            break;
        }

        // 提取字段
        std::string field_str = raw_msg.substr(pos, next_pos - pos);
        size_t eq_pos = field_str.find('=');
        if (eq_pos == std::string::npos) {
            pos = next_pos + 1;
            continue;
        }

        try {
            int tag = std::stoi(field_str.substr(0, eq_pos));
            std::string value = field_str.substr(eq_pos + 1);
            msg.fields[tag] = value;
        } catch (...) {
            // 忽略无效字段
        }

        pos = next_pos + 1;
    }

    return !msg.fields.empty();
}

FIX42Protocol::Message FIX42Protocol::createBaseMessage(MessageType msg_type) {
    Message msg;

    // 设置基础字段
    msg.fields[FIXField::BeginString] = "FIX.4.2";
    msg.fields[FIXField::SenderCompID] = sender_comp_id_;
    msg.fields[FIXField::TargetCompID] = target_comp_id_;
    msg.fields[FIXField::MsgSeqNum] = std::to_string(getNextSenderMsgSeqNum());
    
    // 设置发送时间 (YYYYMMDD-HH:MM:SS.sss)
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::gmtime(&now_time);
    
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y%m%d-%H:%M:%S", tm);
    
    // 添加毫秒
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch() % std::chrono::seconds(1)
    );
    
    std::string sending_time = std::string(time_buf) + "." + std::to_string(ms.count());
    msg.fields[FIXField::SendingTime] = sending_time;

    // 设置消息类型
    msg.fields[FIXField::MsgType] = messageTypeToString(msg_type);

    return msg;
}

bool FIX42Protocol::sendMessage(const Message& msg) {
    if (!is_connected_) {
        return false;
    }

    // 序列化消息
    std::string raw_msg = serialize(msg);
    
    // 发送消息
    ssize_t bytes_sent = send(socket_fd_, raw_msg.c_str(), raw_msg.length(), 0);
    if (bytes_sent != (ssize_t)raw_msg.length()) {
        std::cerr << "Failed to send message" << std::endl;
        return false;
    }

    std::cout << "Sent: " << raw_msg << std::endl;
    return true;
}

void FIX42Protocol::processReceivedMessage(const Message& msg) {
    // 检查必要字段
    if (msg.fields.find(FIXField::MsgType) == msg.fields.end()) {
        return;
    }

    // 更新目标消息序列号
    if (msg.fields.find(FIXField::MsgSeqNum) != msg.fields.end()) {
        try {
            uint32_t seq_num = std::stoul(msg.fields.at(FIXField::MsgSeqNum));
            std::lock_guard<std::mutex> lock(mutex_);
            if (seq_num > target_msg_seq_num_) {
                target_msg_seq_num_ = seq_num;
            }
        } catch (...) {
            // 忽略无效的序列号
        }
    }

    // 处理不同类型的消息
    MessageType msg_type = stringToMessageType(msg.fields.at(FIXField::MsgType));
    
    switch (msg_type) {
        case MessageType::LOGON: {
            std::lock_guard<std::mutex> lock(mutex_);
            is_logged_on_ = true;
            startHeartbeatTimer();
            
            if (on_logon_handler_) {
                on_logon_handler_(true, "Logon successful");
            }
            break;
        }
        
        case MessageType::LOGOUT: {
            std::string reason = "Logout requested by counterparty";
            if (msg.fields.find(FIXField::Text) != msg.fields.end()) {
                reason = msg.fields.at(FIXField::Text);
            }
            
            std::lock_guard<std::mutex> lock(mutex_);
            is_logged_on_ = false;
            stopHeartbeatTimer();
            
            if (on_logout_handler_) {
                on_logout_handler_(reason);
            }
            break;
        }
        
        case MessageType::HEARTBEAT: {
            // 收到心跳，无需特别处理
            break;
        }
        
        case MessageType::TEST_REQUEST: {
            // 收到测试请求，回复心跳
            if (msg.fields.find(FIXField::TestReqID) != msg.fields.end()) {
                last_test_request_id_ = msg.fields.at(FIXField::TestReqID);
                
                Message heartbeat_msg = createBaseMessage(MessageType::HEARTBEAT);
                heartbeat_msg.fields[FIXField::TestReqID] = last_test_request_id_;
                sendMessage(heartbeat_msg);
            }
            break;
        }
        
        case MessageType::EXECUTION_REPORT: {
            if (msg.fields.find(FIXField::ClOrdID) == msg.fields.end() ||
                msg.fields.find(FIXField::ExecType) == msg.fields.end() ||
                msg.fields.find(FIXField::ExecID) == msg.fields.end()) {
                break;
            }
            
            std::string order_id = msg.fields.at(FIXField::ClOrdID);
            std::string exec_id = msg.fields.at(FIXField::ExecID);
            ExecType exec_type = stringToExecType(msg.fields.at(FIXField::ExecType));
            
            double price = 0.0;
            if (msg.fields.find(FIXField::Price) != msg.fields.end()) {
                try {
                    price = std::stod(msg.fields.at(FIXField::Price));
                } catch (...) {}
            }
            
            int quantity = 0;
            if (msg.fields.find(FIXField::CumQty) != msg.fields.end()) {
                try {
                    quantity = std::stoi(msg.fields.at(FIXField::CumQty));
                } catch (...) {}
            }
            
            std::string reason;
            if (msg.fields.find(FIXField::Text) != msg.fields.end()) {
                reason = msg.fields.at(FIXField::Text);
            }
            
            std::lock_guard<std::mutex> lock(mutex_);
            if (on_execution_report_handler_) {
                on_execution_report_handler_(order_id, exec_type, exec_id, price, quantity, reason);
            }
            break;
        }
        
        case MessageType::ORDER_CANCEL_REJECT: {
            if (msg.fields.find(FIXField::OrigClOrdID) == msg.fields.end()) {
                break;
            }
            
            std::string order_id = msg.fields.at(FIXField::OrigClOrdID);
            std::string reason;
            if (msg.fields.find(FIXField::Text) != msg.fields.end()) {
                reason = msg.fields.at(FIXField::Text);
            }
            
            std::lock_guard<std::mutex> lock(mutex_);
            if (on_cancel_reject_handler_) {
                on_cancel_reject_handler_(order_id, reason);
            }
            break;
        }
        
        default:
            break;
    }
}

void FIX42Protocol::sendHeartbeat() {
    if (!is_connected_ || !is_logged_on_) {
        return;
    }

    Message msg = createBaseMessage(MessageType::HEARTBEAT);
    if (!last_test_request_id_.empty()) {
        msg.fields[FIXField::TestReqID] = last_test_request_id_;
        last_test_request_id_.clear();
    }

    sendMessage(msg);
}

void FIX42Protocol::startHeartbeatTimer() {
    if (heartbeat_running_ || heartbt_int_ <= 0) {
        return;
    }

    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread(&FIX42Protocol::heartbeatLoop, this);
}

void FIX42Protocol::stopHeartbeatTimer() {
    if (!heartbeat_running_) {
        return;
    }

    heartbeat_running_ = false;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

void FIX42Protocol::heartbeatLoop() {
    while (heartbeat_running_) {
        // 等待心跳间隔时间
        std::this_thread::sleep_for(std::chrono::seconds(heartbt_int_));
        
        // 发送心跳
        if (is_connected_ && is_logged_on_) {
            sendHeartbeat();
        }
    }
}

bool FIX42Protocol::validateMessage(const Message& msg) {
    // 检查必要字段
    if (msg.fields.find(FIXField::BeginString) == msg.fields.end() ||
        msg.fields.find(FIXField::BodyLength) == msg.fields.end() ||
        msg.fields.find(FIXField::MsgType) == msg.fields.end() ||
        msg.fields.find(FIXField::SenderCompID) == msg.fields.end() ||
        msg.fields.find(FIXField::TargetCompID) == msg.fields.end() ||
        msg.fields.find(FIXField::MsgSeqNum) == msg.fields.end() ||
        msg.fields.find(FIXField::SendingTime) == msg.fields.end() ||
        msg.fields.find(FIXField::CheckSum) == msg.fields.end()) {
        return false;
    }

    // 检查协议版本
    if (msg.fields.at(FIXField::BeginString) != "FIX.4.2") {
        return false;
    }

    // 检查目标和发送方ID
    if (msg.fields.at(FIXField::SenderCompID) != target_comp_id_ ||
        msg.fields.at(FIXField::TargetCompID) != sender_comp_id_) {
        return false;
    }

    // 校验和验证
    std::string header_without_checksum = msg.raw_data.substr(0, msg.raw_data.find(std::to_string(FIXField::CheckSum) + "="));
    std::string calculated_checksum = calculateChecksum(header_without_checksum);
    
    if (calculated_checksum != msg.fields.at(FIXField::CheckSum)) {
        return false;
    }

    return true;
}

std::string FIX42Protocol::calculateChecksum(const std::string& msg) {
    uint32_t sum = 0;
    for (char c : msg) {
        sum += static_cast<uint8_t>(c);
    }
    sum %= 256;

    // 格式化为3位数字
    std::stringstream ss;
    ss << std::setw(3) << std::setfill('0') << sum;
    return ss.str();
}

std::string FIX42Protocol::messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::LOGON: return "A";
        case MessageType::LOGOUT: return "5";
        case MessageType::HEARTBEAT: return "0";
        case MessageType::TEST_REQUEST: return "1";
        case MessageType::RESEND_REQUEST: return "2";
        case MessageType::REJECT: return "3";
        case MessageType::SEQUENCE_RESET: return "4";
        case MessageType::NEW_ORDER_SINGLE: return "D";
        case MessageType::ORDER_CANCEL_REQUEST: return "F";
        case MessageType::EXECUTION_REPORT: return "8";
        case MessageType::ORDER_CANCEL_REJECT: return "9";
        default: return "";
    }
}

std::string FIX42Protocol::sideToString(Side side) {
    switch (side) {
        case Side::BUY: return "1";
        case Side::SELL: return "2";
        default: return "";
    }
}

std::string FIX42Protocol::ordTypeToString(OrdType type) {
    switch (type) {
        case OrdType::MARKET: return "1";
        case OrdType::LIMIT: return "2";
        default: return "";
    }
}

std::string FIX42Protocol::timeInForceToString(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::DAY: return "0";
        case TimeInForce::IMMEDIATE_OR_CANCEL: return "3";
        case TimeInForce::FILL_OR_KILL: return "4";
        default: return "";
    }
}

FIX42Protocol::MessageType FIX42Protocol::stringToMessageType(const std::string& str) {
    if (str == "A") return MessageType::LOGON;
    if (str == "5") return MessageType::LOGOUT;
    if (str == "0") return MessageType::HEARTBEAT;
    if (str == "1") return MessageType::TEST_REQUEST;
    if (str == "2") return MessageType::RESEND_REQUEST;
    if (str == "3") return MessageType::REJECT;
    if (str == "4") return MessageType::SEQUENCE_RESET;
    if (str == "D") return MessageType::NEW_ORDER_SINGLE;
    if (str == "F") return MessageType::ORDER_CANCEL_REQUEST;
    if (str == "8") return MessageType::EXECUTION_REPORT;
    if (str == "9") return MessageType::ORDER_CANCEL_REJECT;
    return static_cast<MessageType>(-1);
}

FIX42Protocol::ExecType FIX42Protocol::stringToExecType(const std::string& str) {
    if (str == "0") return ExecType::NEW;
    if (str == "1") return ExecType::PARTIAL_FILL;
    if (str == "2") return ExecType::FILL;
    if (str == "4") return ExecType::CANCELLED;
    if (str == "8") return ExecType::REJECTED;
    return static_cast<ExecType>(-1);
}

void FIX42Protocol::receiveLoop() {
    char buffer[4096];
    std::string partial_msg;

    while (is_connected_) {
        // 接收数据
        ssize_t bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 没有数据，短暂休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                // 连接关闭或错误
                std::cerr << "Connection closed or error occurred" << std::endl;
                is_connected_ = false;
                is_logged_on_ = false;
                break;
            }
        }

        buffer[bytes_read] = '\0';
        partial_msg += std::string(buffer, bytes_read);

        // 处理接收到的消息（FIX消息以SOH字符'\001'结尾）
        size_t msg_end;
        while ((msg_end = partial_msg.find('\001', 0)) != std::string::npos) {
            // 提取完整消息
            std::string raw_msg = partial_msg.substr(0, msg_end + 1);
            partial_msg = partial_msg.substr(msg_end + 1);

            // 反序列化并处理消息
            Message msg;
            if (deserialize(raw_msg, msg) && validateMessage(msg)) {
                std::cout << "Received: " << raw_msg << std::endl;
                processReceivedMessage(msg);
            }
        }
    }
}
