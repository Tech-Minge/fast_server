#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <snappy.h>
#include <mutex>
#include <queue>
#include <atomic>

// 自定义结构体（需与客户端一致）
struct LoginRequest {
    char user[5];
    char password[5];
};

// 全局变量
std::ofstream logFile;
std::mutex logMutex;
std::atomic<uint32_t> lastSeqNo{0};
std::atomic<bool> isAuthenticated{false};

// 解析行情数据
void processMarketData(const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        uint32_t compressedSize = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += 4;

        std::string compressedData(data.substr(offset, compressedSize));
        offset += compressedSize;

        std::string decompressedData;
        snappy::RawUncompress(compressedData.data(), compressedSize, &decompressedData);

        // 解析结构体（示例）
        std::cout << "Received " << decompressedData.size() << " bytes of decompressed data\n";
    }
}

// 接收线程
void receiveThread(int sockfd, const sockaddr_in& clientAddr) {
    char buffer[65536];  // 64KB 缓冲区
    socklen_t addrLen = sizeof(clientAddr);

    while (true) {
        ssize_t bytesRecv = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &addrLen);
        if (bytesRecv <= 0) continue;

        if (!isAuthenticated) {
            // 处理登录请求
            LoginRequest* req = reinterpret_cast<LoginRequest*>(buffer);
            if (strncmp(req->user, "user", 5) == 0 && strncmp(req->password, "pass", 5) == 0) {
                isAuthenticated = true;
                std::cout << "Authentication successful\n";
            } else {
                std::cout << "Authentication failed\n";
            }
            continue;
        }

        // 处理行情数据
        uint32_t seqNo = *reinterpret_cast<uint32_t*>(buffer);
        std::lock_guard<std::mutex> lock(logMutex);
        logFile << seqNo << "\n";
        logFile.flush();  // 实时写入

        std::string dataPacket(buffer + 4, bytesRecv - 4);
        processMarketData(dataPacket);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <IP> <Port>\n";
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // 设置接收缓冲区大小（调整为 1GB）
    int buffer_size = 1024 * 1024 * 1024;  // 1GB
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) < 0) {
        perror("Failed to set SO_RCVBUF");
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(argv[1]);
    serverAddr.sin_port = htons(atoi(argv[2]));

    if (bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return 1;
    }

    logFile.open("seqnos.log");
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file\n";
        return 1;
    }

    sockaddr_in clientAddr{};
    std::thread receiver(receiveThread, sockfd, clientAddr);

    receiver.join();
    logFile.close();
    close(sockfd);
    return 0;
}