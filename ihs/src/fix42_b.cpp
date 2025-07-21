#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <iostream>
class FixMessage {
public:
    using FieldMap = std::unordered_map<int, std::string>;
    
    // 添加字段 (Tag=Value)
    void setField(int tag, const std::string& value) {
        _fields[tag] = value;
    }
    
    // 序列化为FIX格式 (带校验和)
    std::string serialize() const {
        std::ostringstream oss;
        oss << "8=FIX.4.2|9="; // 协议版本占位
        
        // 构建Body
        std::string body;
        for (const auto& [tag, value] : _fields) {
            body += std::to_string(tag) + "=" + value + "|";
        }
        
        // 计算长度和校验和
        int body_length = body.size();
        int checksum = 0;
        for (char c : body) checksum = (checksum + c) % 256;
        
        // 组装完整消息
        oss << body_length << "|" << body << "10=";
        oss << std::setfill('0') << std::setw(3) << checksum << "|";
        return oss.str();
    }
    
    // 反序列化 (解析Tag=Value)
    static FieldMap parse(const std::string& fixMsg) {
        FieldMap fields;
        std::istringstream iss(fixMsg);
        std::string token;
        while (std::getline(iss, token, '|')) {
            size_t pos = token.find('=');
            if (pos != std::string::npos) {
                int tag = std::stoi(token.substr(0, pos));
                std::string value = token.substr(pos + 1);
                fields[tag] = value;
            }
        }
        return fields;
    }

protected:
    FieldMap _fields;
};

#include <thread>
#include <chrono>
#include <sys/socket.h>

class FixSession {
public:
    FixSession(const std::string& host, int port, int heartbeatInterval) 
        : _heartbeatInt(heartbeatInterval) {}
    
    void connect() {
        // 创建Socket并连接服务器 (伪代码)
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        // connect(_sockfd, "1.1.1.1");
        
        // 启动心跳线程
        _heartbeatThread = std::thread([this] {
            while (_active) {
                sendHeartbeat();
                std::this_thread::sleep_for(
                    std::chrono::seconds(_heartbeatInt)
                );
            }
        });
    }
    
    void send(const std::string& rawFixMsg) {
        ::send(_sockfd, rawFixMsg.c_str(), rawFixMsg.size(), 0);
    }
    
private:
    void sendHeartbeat() {
        FixMessage msg;
        msg.setField(35, "0"); // MsgType=Heartbeat
        msg.setField(34, std::to_string(_outSeq++)); // 消息序号
        send(msg.serialize());
    }

    int _sockfd;
    int _outSeq = 1; // 发送序列号
    int _heartbeatInt;
    bool _active = true;
    std::thread _heartbeatThread;
};

class LogonMessage : public FixMessage {
public:
    LogonMessage(const std::string& sender, const std::string& target) {
        setField(35, "A");      // MsgType=Logon
        setField(49, sender);   // SenderCompID
        setField(56, target);    // TargetCompID
        setField(98, "0");       // EncryptMethod=None
        setField(108, "30");     // HeartbeatInt=30s
        setField(141, "Y");      // ResetSeqNumFlag=Yes
    }
};

class LogoutMessage : public FixMessage {
public:
    LogoutMessage() {
        setField(35, "5"); // MsgType=Logout
    }
};

class LimitOrderMessage : public FixMessage {
public:
    LimitOrderMessage(
        const std::string& symbol, 
        char side, // '1'=Buy, '2'=Sell
        double price, 
        int quantity
    ) {
        setField(35, "D");             // MsgType=NewOrder
        setField(55, symbol);           // Symbol
        setField(54, std::string(1, side)); // Side
        setField(40, "2");              // OrderType=Limit
        setField(44, std::to_string(price)); // Price
        setField(38, std::to_string(quantity)); // OrderQty
        setField(21, "1");              // HandlInst=Automated
    }
};

class CancelRequestMessage : public FixMessage {
public:
    CancelRequestMessage(
        const std::string& orderID, 
        const std::string& origClOrdID
    ) {
        setField(35, "F");         // MsgType=CancelRequest
        setField(41, orderID);      // OrderID (交易所分配)
        setField(11, origClOrdID);  // OrigClOrdID (原始客户端订单ID)
    }
};

int main() {
    // 初始化会话
    FixSession session("127.0.0.1", 5001, 30);
    // session.connect();

    // 1. 登录
    LogonMessage logon("TRADER1", "BROKER");
    // session.send(logon.serialize());
    std::cout << logon.serialize() << std::endl;
    // 2. 下单
    LimitOrderMessage order("AAPL", '1', 150.25, 100);
    // session.send(order.serialize());
    std::cout << order.serialize() << std::endl;

    // 3. 撤单 (假设收到响应OrderID=ORD123)
    CancelRequestMessage cancel("ORD123", "CLIENT_ORDER_001");
    // session.send(cancel.serialize());
    std::cout << cancel.serialize() << std::endl;

    // 4. 登出
    LogoutMessage logout;
    // session.send(logout.serialize());
    std::cout << logout.serialize() << std::endl;
}

