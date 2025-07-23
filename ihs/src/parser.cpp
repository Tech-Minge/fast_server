#include <quickfix/Application.h>
#include <quickfix/SocketAcceptor.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Message.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <iostream>

// 自定义Application处理逻辑
class FixServerApp : public FIX::Application {
public:
    void onCreate(const FIX::SessionID&) override {}
    void onLogon(const FIX::SessionID&) override {}
    void onLogout(const FIX::SessionID&) override {}

    void toApp(FIX::Message&, const FIX::SessionID&) override {}
    void fromApp(const FIX::Message& msg, const FIX::SessionID& sid) override {
        std::cout << "Received: " << msg.toString() << std::endl;
    }
};

// TCP消息解析器
class FixMessageParser {
public:
    void appendData(const char* data, size_t len) {
        buffer_.insert(buffer_.end(), data, data + len);
    }

    bool extractNextMessage(std::string& msg) {
        // 状态1: 检查消息头 "8=FIX.4.2"
        const std::string header = "8=FIX.4.2";
        auto headerPos = findInBuffer(header);
        if (headerPos == buffer_.end()) return false;

        // 状态2: 查找BodyLength字段 (Tag 9)
        auto bodyLenStart = findTag("9=");
        if (bodyLenStart == buffer_.end()) return false;

        // 状态3: 提取消息长度
        size_t bodyLength = 0;
        auto bodyLenEnd = std::find(bodyLenStart, buffer_.end(), '\x01');
        if (bodyLenEnd != buffer_.end()) {
            bodyLength = std::stoi(std::string(bodyLenStart + 2, bodyLenEnd));
        } else return false;

        // 计算完整消息长度 = 头长度 + BodyLength + 校验和长度
        const size_t totalLen = (bodyLenEnd - buffer_.begin()) + bodyLength + 7; // 7=校验和长度(10=XXX\x01)
        if (buffer_.size() < totalLen) return false; // 消息不完整

        // 提取完整消息并移除缓冲区
        msg.assign(buffer_.begin(), buffer_.begin() + totalLen);
        buffer_.erase(buffer_.begin(), buffer_.begin() + totalLen);
        return true;
    }

private:
    std::vector<char> buffer_;

    std::vector<char>::iterator findTag(const std::string& tag) {
        return std::search(buffer_.begin(), buffer_.end(), tag.begin(), tag.end());
    }
};

int main() {
    // 1. 初始化QuickFIX引擎
    FIX::SessionSettings settings("fix_server.cfg");
    FixServerApp app;
    FIX::FileStoreFactory storeFactory(settings);
    FIX::SocketAcceptor acceptor(app, storeFactory, settings);

    // 2. 启动服务端
    acceptor.start();
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{.sin_family=AF_INET, .sin_port=htons(5001)};
    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);

    // 3. 主循环处理连接
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        FixMessageParser parser;
        char recvBuf[4096];

        while (true) {
            ssize_t bytesRead = recv(client_fd, recvBuf, sizeof(recvBuf), 0);
            if (bytesRead <= 0) break; // 连接断开

            // 4. 处理拆包/粘包
            parser.appendData(recvBuf, bytesRead);
            std::string fixMsg;
            while (parser.extractNextMessage(fixMsg)) {
                FIX::Message message(fixMsg, false); // 解析FIX消息
                FIX::Session::sendToTarget(message); // 转发至QuickFIX
            }
        }
        close(client_fd);
    }
    acceptor.stop();
    return 0;
}