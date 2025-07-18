#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <fstream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <snappy.h>
#include <zlib.h> // 用于CRC32校验

// 常量定义
const int PORT = 8888;
const int MAX_UDP_SIZE = 65507;
const int USER_LEN = 5;
const int PASS_LEN = 5;
const int SEQNO_SIZE = 4;
const int LENGTH_FIELD_SIZE = 4;

// 认证结构体
#pragma pack(push, 1)
struct AuthPacket {
    char username[USER_LEN];
    char password[PASS_LEN];
};
#pragma pack(pop)

// 全局队列和同步工具
std::queue<std::pair<uint32_t, std::vector<char>>> data_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::atomic<bool> running{true};

// 日志文件
std::ofstream seqno_log("seqno.log", std::ios::binary);

// Algo模块回调函数示例
void algo_callback(const std::string& data) {
    // 这里添加实际处理逻辑
    static int count = 0;
    if (++count % 1000 == 0) {
        std::cout << "Processed " << count << " market data blocks\n";
    }
}

// 数据包处理线程
void process_thread_func() {
    uint32_t expected_seqno = 1; // 起始序列号

    while (running) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cv.wait(lock, []{ return !data_queue.empty() || !running; });
        
        if (!running) break;

        auto [seqno, packet] = data_queue.front();
        data_queue.pop();
        lock.unlock();

        // 序列号验证
        if (seqno != expected_seqno) {
            std::cerr << "Seqno gap detected! Expected: " << expected_seqno 
                      << ", Received: " << seqno << std::endl;
            // 严重错误处理（根据需求调整）
            exit(EXIT_FAILURE);
        }

        // 解析数据块
        size_t offset = 0;
        while (offset < packet.size()) {
            // 提取长度字段
            if (offset + LENGTH_FIELD_SIZE > packet.size()) {
                std::cerr << "Invalid packet structure" << std::endl;
                break;
            }
            
            uint32_t block_len;
            memcpy(&block_len, packet.data() + offset, LENGTH_FIELD_SIZE);
            block_len = ntohl(block_len);
            offset += LENGTH_FIELD_SIZE;

            // 检查数据块完整性
            if (offset + block_len > packet.size()) {
                std::cerr << "Corrupted block at seqno: " << seqno << std::endl;
                break;
            }

            // Snappy解压缩
            std::string uncompressed;
            const char* compressed_data = packet.data() + offset;
            if (!snappy::Uncompress(compressed_data, block_len, &uncompressed)) {
                std::cerr << "Snappy decompression failed at seqno: " << seqno << std::endl;
                offset += block_len;
                continue;
            }

            // 传递给算法模块
            algo_callback(uncompressed);
            offset += block_len;
        }

        // 更新期望序列号
        expected_seqno++;
    }
}

int main() {
    // 创建UDP套接字
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return EXIT_FAILURE;
    }

    // 设置接收缓冲区（提高网络性能）
    int recv_buf_size = 10 * 1024 * 1024; // 10MB
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));

    // 绑定地址
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(sockfd, (const sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return EXIT_FAILURE;
    }

    std::cout << "UDP server listening on port " << PORT << std::endl;

    // 启动处理线程
    std::thread processor(process_thread_func);

    // 主接收循环
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    bool authenticated = false;
    const AuthPacket valid_auth = {"user1", "pass1"}; // 预设凭证

    while (running) {
        std::vector<char> buffer(MAX_UDP_SIZE);
        ssize_t recv_len = recvfrom(
            sockfd, 
            buffer.data(), 
            buffer.size(),
            0, 
            (sockaddr*)&client_addr, 
            &addr_len
        );

        if (recv_len < 0) {
            perror("recvfrom failed");
            continue;
        }

        // 认证处理
        if (!authenticated) {
            if (static_cast<size_t>(recv_len) == sizeof(AuthPacket)) {
                AuthPacket* auth = reinterpret_cast<AuthPacket*>(buffer.data());
                if (memcmp(auth, &valid_auth, sizeof(AuthPacket)) == 0) {
                    authenticated = true;
                    std::cout << "Authentication successful" << std::endl;
                    // 发送确认信号（可选）
                    const char ack = 0x06;
                    sendto(sockfd, &ack, 1, 0, 
                          (sockaddr*)&client_addr, addr_len);
                } else {
                    std::cerr << "Invalid credentials" << std::endl;
                }
            }
            continue;
        }

        // 数据包处理
        if (static_cast<size_t>(recv_len) < SEQNO_SIZE) {
            std::cerr << "Invalid packet size" << std::endl;
            continue;
        }

        // 提取序列号
        uint32_t seqno;
        memcpy(&seqno, buffer.data(), SEQNO_SIZE);
        seqno = ntohl(seqno);
        
        // 记录序列号
        seqno_log.write(reinterpret_cast<const char*>(&seqno), sizeof(seqno));
        
        // 移除非序列号数据
        std::vector<char> payload(
            buffer.begin() + SEQNO_SIZE,
            buffer.begin() + recv_len
        );

        // 添加到处理队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            data_queue.emplace(seqno, std::move(payload));
        }
        queue_cv.notify_one();
    }

    // 清理资源
    running = false;
    queue_cv.notify_one();
    processor.join();
    close(sockfd);
    seqno_log.close();
    return EXIT_SUCCESS;
}