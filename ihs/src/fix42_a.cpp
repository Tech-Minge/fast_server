#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

// FIX 4.2 协议常量
const char SOH = '\x01';
const std::string FIX_VERSION = "FIX.4.2";

// FIX 消息类型
namespace MsgType {
    const std::string LOGON = "A";
    const std::string HEARTBEAT = "0";
    const std::string LOGOUT = "5";
    const std::string NEW_ORDER = "D";
    const std::string ORDER_CANCEL = "F";
    const std::string EXECUTION_REPORT = "8";
    const std::string ORDER_CANCEL_REJECT = "9";
    const std::string REJECT = "3";
}

// FIX 域标签
namespace Tag {
    const int BeginString = 8;
    const int BodyLength = 9;
    const int MsgType = 35;
    const int SenderCompID = 49;
    const int TargetCompID = 56;
    const int MsgSeqNum = 34;
    const int SendingTime = 52;
    const int EncryptMethod = 98;
    const int HeartBtInt = 108;
    const int Password = 554;
    const int CheckSum = 10;
    const int TestReqID = 112;
    const int ClOrdID = 11;
    const int HandlInst = 21;
    const int Symbol = 55;
    const int Side = 54;
    const int OrderQty = 38;
    const int OrdType = 40;
    const int Price = 44;
    const int TimeInForce = 59;
    const int OrigClOrdID = 41;
    const int OrderID = 37;
    const int ExecID = 17;
    const int ExecType = 150;
    const int OrdStatus = 39;
    const int LeavesQty = 151;
    const int CumQty = 14;
    const int AvgPx = 6;
    const int CxlRejResponseTo = 434;
    const int CxlRejReason = 102;
    const int Text = 58;
}

// FIX 会话状态
enum class SessionState {
    DISCONNECTED,
    LOGGING_IN,
    LOGGED_IN,
    LOGGING_OUT
};

class FixSession {
public:
    FixSession(const std::string& senderCompID, 
               const std::string& targetCompID,
               const std::string& password,
               int heartbeatInterval = 30)
        : senderCompID_(senderCompID),
          targetCompID_(targetCompID),
          password_(password),
          heartbeatInterval_(heartbeatInterval),
          state_(SessionState::DISCONNECTED),
          msgSeqNum_(1),
          sockfd_(-1) {}
    
    virtual ~FixSession() {
        disconnect();
    }

    // 连接到FIX服务器
    bool connect(const std::string& host, int port) {
        if (state_ != SessionState::DISCONNECTED) {
            std::cerr << "Already connected or connecting" << std::endl;
            return false;
        }

        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            perror("socket creation failed");
            return false;
        }

        // 设置非阻塞连接
        int flags = fcntl(sockfd_, F_GETFL, 0);
        fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address/Address not supported" << std::endl;
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        int conn_result = ::connect(sockfd_, (sockaddr*)&serv_addr, sizeof(serv_addr));
        if (conn_result < 0 && errno != EINPROGRESS) {
            perror("connect failed");
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        // 等待连接完成
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sockfd_, &set);
        timeval timeout{5, 0}; // 5秒超时

        if (select(sockfd_ + 1, nullptr, &set, nullptr, &timeout) <= 0) {
            std::cerr << "Connection timeout" << std::endl;
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        // 设置阻塞模式
        fcntl(sockfd_, F_SETFL, flags & ~O_NONBLOCK);

        state_ = SessionState::LOGGING_IN;
        startMessageProcessing();
        return sendLogon();
    }

    // 断开连接
    void disconnect() {
        if (sockfd_ >= 0) {
            if (state_ == SessionState::LOGGED_IN) {
                sendLogout("User requested logout");
            }
            close(sockfd_);
            sockfd_ = -1;
        }
        state_ = SessionState::DISCONNECTED;
        running_ = false;
        if (recvThread_.joinable()) recvThread_.join();
        if (processThread_.joinable()) processThread_.join();
    }

    // 发送限价单
    bool sendNewOrderSingle(const std::string& clOrdID, 
                           const std::string& symbol, 
                           char side, 
                           int quantity, 
                           double price,
                           char timeInForce = '1') { // '1' = GTC, '3' = IOC
        if (state_ != SessionState::LOGGED_IN) {
            std::cerr << "Not logged in" << std::endl;
            return false;
        }

        std::map<int, std::string> fields;
        fields[Tag::ClOrdID] = clOrdID;
        fields[Tag::HandlInst] = "1"; // Automated execution
        fields[Tag::Symbol] = symbol;
        fields[Tag::Side] = std::string(1, side); // '1'=Buy, '2'=Sell
        fields[Tag::OrderQty] = std::to_string(quantity);
        fields[Tag::OrdType] = "2"; // Limit order
        fields[Tag::Price] = formatPrice(price);
        fields[Tag::TimeInForce] = std::string(1, timeInForce);

        pendingOrders_[clOrdID] = {symbol, side, quantity, price};
        return sendMessage(MsgType::NEW_ORDER, fields);
    }

    // 发送撤单请求
    bool sendOrderCancelRequest(const std::string& origClOrdID, 
                               const std::string& clOrdID, 
                               const std::string& symbol, 
                               char side) {
        if (state_ != SessionState::LOGGED_IN) {
            std::cerr << "Not logged in" << std::endl;
            return false;
        }

        std::map<int, std::string> fields;
        fields[Tag::OrigClOrdID] = origClOrdID;
        fields[Tag::ClOrdID] = clOrdID;
        fields[Tag::Symbol] = symbol;
        fields[Tag::Side] = std::string(1, side);

        return sendMessage(MsgType::ORDER_CANCEL, fields);
    }

protected:
    // 纯虚函数 - 需要派生类实现业务逻辑
    virtual void onExecutionReport(const std::map<int, std::string>& fields) = 0;
    virtual void onOrderCancelReject(const std::map<int, std::string>& fields) = 0;
    virtual void onReject(const std::map<int, std::string>& fields) = 0;
    virtual void onLogout(const std::string& text) = 0;
    virtual void onHeartbeat() {} // 可选实现

private:
    // 发送登录消息
    bool sendLogon() {
        std::map<int, std::string> fields;
        fields[Tag::EncryptMethod] = "0"; // None
        fields[Tag::HeartBtInt] = std::to_string(heartbeatInterval_);
        fields[Tag::Password] = password_;

        return sendMessage(MsgType::LOGON, fields);
    }

    // 发送登出消息
    bool sendLogout(const std::string& text = "") {
        std::map<int, std::string> fields;
        if (!text.empty()) {
            fields[Tag::Text] = text;
        }
        return sendMessage(MsgType::LOGOUT, fields);
    }

    // 发送心跳消息
    bool sendHeartbeat(const std::string& testReqID = "") {
        std::map<int, std::string> fields;
        if (!testReqID.empty()) {
            fields[Tag::TestReqID] = testReqID;
        }
        return sendMessage(MsgType::HEARTBEAT, fields);
    }

    // 发送FIX消息
    bool sendMessage(const std::string& msgType, const std::map<int, std::string>& fields) {
        std::lock_guard<std::mutex> lock(socketMutex_);
        
        if (sockfd_ < 0) {
            std::cerr << "Socket not connected" << std::endl;
            return false;
        }

        // 构建消息体（不包含头部和尾部）
        std::ostringstream body;
        body << Tag::MsgType << "=" << msgType << SOH;
        for (const auto& [tag, value] : fields) {
            body << tag << "=" << value << SOH;
        }
        
        // 添加标准头部
        std::ostringstream message;
        message << Tag::BeginString << "=" << FIX_VERSION << SOH
                << Tag::BodyLength << "=000000" << SOH // 占位符，后面会替换
                << Tag::MsgSeqNum << "=" << msgSeqNum_ << SOH
                << Tag::SenderCompID << "=" << senderCompID_ << SOH
                << Tag::TargetCompID << "=" << targetCompID_ << SOH
                << Tag::SendingTime << "=" << getCurrentTimestamp() << SOH
                << body.str();

        // 添加校验和
        std::string fullMsg = message.str();
        int bodyLength = fullMsg.length() - std::to_string(Tag::BodyLength).length() - 3; // 减去"9=000000"部分
        
        // 替换BodyLength
        std::string bodyLengthStr = std::to_string(bodyLength);
        size_t pos = fullMsg.find("000000");
        if (pos != std::string::npos) {
            fullMsg.replace(pos, 6, std::string(6 - bodyLengthStr.length(), '0') + bodyLengthStr);
        }

        // 计算校验和
        unsigned char sum = 0;
        for (char c : fullMsg) {
            sum += static_cast<unsigned char>(c);
        }
        sum %= 256;
        
        char checksumStr[4];
        snprintf(checksumStr, sizeof(checksumStr), "%03d", static_cast<int>(sum));
        fullMsg += std::to_string(::Tag::CheckSum) + "=" + checksumStr + SOH;

        // 发送消息
        ssize_t sent = ::send(sockfd_, fullMsg.c_str(), fullMsg.length(), 0);
        if (sent < 0) {
            perror("send failed");
            return false;
        }

        if (static_cast<size_t>(sent) != fullMsg.length()) {
            std::cerr << "Partial message sent" << std::endl;
            return false;
        }

        std::cout << "Sent: " << fullMsg << std::endl;
        msgSeqNum_++;
        return true;
    }

    // 启动消息处理线程
    void startMessageProcessing() {
        running_ = true;
        recvThread_ = std::thread([this] { receiveThreadFunc(); });
        processThread_ = std::thread([this] { processThreadFunc(); });
    }

    // 接收线程函数
    void receiveThreadFunc() {
        std::vector<char> buffer(4096);
        std::string incompleteMessage;

        while (running_) {
            ssize_t bytesRead = recv(sockfd_, buffer.data(), buffer.size(), 0);
            if (bytesRead <= 0) {
                if (bytesRead == 0) {
                    std::cerr << "Connection closed by server" << std::endl;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("recv failed");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::lock_guard<std::mutex> lock(queueMutex_);
            std::string data(buffer.data(), bytesRead);
            incompleteMessage += data;

            // 分割消息（以<SOH>为分隔符）
            size_t start = 0;
            size_t end = incompleteMessage.find(SOH);
            while (end != std::string::npos) {
                std::string message = incompleteMessage.substr(start, end - start);
                messageQueue_.push(message);
                start = end + 1;
                end = incompleteMessage.find(SOH, start);
            }

            incompleteMessage = incompleteMessage.substr(start);
            queueCV_.notify_one();
        }
    }

    // 处理线程函数
    void processThreadFunc() {
        while (running_) {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] { return !messageQueue_.empty() || !running_; });
            
            if (!running_) break;
            
            while (!messageQueue_.empty()) {
                std::string message = messageQueue_.front();
                messageQueue_.pop();
                lock.unlock();
                
                try {
                    auto fields = parseFixMessage(message);
                    handleMessage(fields);
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing FIX message: " << e.what() << std::endl;
                }
                
                lock.lock();
            }
        }
    }

    // 解析FIX消息
    std::map<int, std::string> parseFixMessage(const std::string& message) {
        std::map<int, std::string> fields;
        std::istringstream ss(message);
        std::string field;
        
        while (std::getline(ss, field, '=')) {
            int tag;
            try {
                tag = std::stoi(field);
            } catch (...) {
                throw std::runtime_error("Invalid tag: " + field);
            }
            
            std::string value;
            if (!std::getline(ss, value, SOH)) {
                throw std::runtime_error("Missing value for tag: " + field);
            }
            
            fields[tag] = value;
        }
        
        // 验证必需字段
        if (fields.find(Tag::BeginString) == fields.end() || 
            fields[Tag::BeginString] != FIX_VERSION) {
            throw std::runtime_error("Invalid FIX version");
        }
        
        if (fields.find(Tag::MsgType) == fields.end()) {
            throw std::runtime_error("Missing MsgType");
        }
        
        return fields;
    }

    // 处理接收到的消息
    void handleMessage(const std::map<int, std::string>& fields) {
        const std::string& msgType = fields.at(Tag::MsgType);
        
        if (msgType == MsgType::LOGON) {
            handleLogonResponse(fields);
        } else if (msgType == MsgType::HEARTBEAT) {
            onHeartbeat();
        } else if (msgType == MsgType::LOGOUT) {
            std::string text = fields.find(Tag::Text) != fields.end() ? fields.at(Tag::Text) : "";
            onLogout(text);
            state_ = SessionState::LOGGING_OUT;
        } else if (msgType == MsgType::EXECUTION_REPORT) {
            onExecutionReport(fields);
        } else if (msgType == MsgType::ORDER_CANCEL_REJECT) {
            onOrderCancelReject(fields);
        } else if (msgType == MsgType::REJECT) {
            onReject(fields);
        } else {
            std::cerr << "Unhandled message type: " << msgType << std::endl;
        }
    }

    // 处理登录响应
    void handleLogonResponse(const std::map<int, std::string>& fields) {
        if (state_ == SessionState::LOGGING_IN) {
            // 验证登录响应
            if (fields.find(Tag::HeartBtInt) != fields.end()) {
                int serverHeartbeat = std::stoi(fields.at(Tag::HeartBtInt));
                if (serverHeartbeat != heartbeatInterval_) {
                    std::cerr << "Warning: Server requested different heartbeat interval: " 
                              << serverHeartbeat << std::endl;
                }
            }
            
            state_ = SessionState::LOGGED_IN;
            std::cout << "Logon successful" << std::endl;
        }
    }

    // 获取当前时间戳 (YYYYMMDD-HH:MM:SS)
    std::string getCurrentTimestamp() {
        auto now = std::time(nullptr);
        auto tm = *std::gmtime(&now);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d-%H:%M:%S");
        return oss.str();
    }

    // 格式化价格 (保留小数点后4位)
    std::string formatPrice(double price) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4) << price;
        return oss.str();
    }

    // 订单信息结构
    struct OrderInfo {
        std::string symbol;
        char side;
        int quantity;
        double price;
    };

    // 成员变量
    std::string senderCompID_;
    std::string targetCompID_;
    std::string password_;
    int heartbeatInterval_;
    SessionState state_;
    int msgSeqNum_;
    int sockfd_;
    std::atomic<bool> running_{false};
    
    // 网络和消息处理
    std::thread recvThread_;
    std::thread processThread_;
    std::mutex socketMutex_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::queue<std::string> messageQueue_;
    
    // 订单管理
    std::map<std::string, OrderInfo> pendingOrders_;
};

// 示例交易会话实现
class TradingSession : public FixSession {
public:
    TradingSession(const std::string& sender, const std::string& target, const std::string& password)
        : FixSession(sender, target, password) {}
    
protected:
    void onExecutionReport(const std::map<int, std::string>& fields) override {
        std::cout << "Received Execution Report:" << std::endl;
        for (const auto& [tag, value] : fields) {
            std::cout << "  " << tag << " = " << value << std::endl;
        }
        
        // 处理订单状态更新
        if (fields.find(Tag::ClOrdID) != fields.end()) {
            std::string clOrdID = fields.at(Tag::ClOrdID);
            // 更新订单状态...
        }
    }
    
    void onOrderCancelReject(const std::map<int, std::string>& fields) override {
        std::cout << "Received Order Cancel Reject:" << std::endl;
        for (const auto& [tag, value] : fields) {
            std::cout << "  " << tag << " = " << value << std::endl;
        }
    }
    
    void onReject(const std::map<int, std::string>& fields) override {
        std::cout << "Received Reject:" << std::endl;
        for (const auto& [tag, value] : fields) {
            std::cout << "  " << tag << " = " << value << std::endl;
        }
    }
    
    void onLogout(const std::string& text) override {
        std::cout << "Received Logout: " << text << std::endl;
        disconnect();
    }
};

int main() {
    // 配置FIX会话参数
    TradingSession session("CLIENT123", "SERVER456", "securepass", 30);
    
    // 连接到FIX服务器
    if (!session.connect("127.0.0.1", 5001)) {
        std::cerr << "Failed to connect to FIX server" << std::endl;
        return 1;
    }
    
    // 等待登录完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 发送限价单
    session.sendNewOrderSingle("ORD10001", "AAPL", '1', 100, 150.25);
    
    // 等待10秒
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 发送撤单请求
    session.sendOrderCancelRequest("ORD10001", "ORD10002", "AAPL", '1');
    
    // 等待5秒
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 登出
    session.disconnect();
    
    return 0;
}