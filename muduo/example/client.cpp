#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cmath>

class LatencyClient {
private:
    std::string server_ip_;
    int server_port_;
    int packet_size_;
    int packet_count_;
    std::vector<double> latencies_;

public:
    LatencyClient(const std::string& ip, int port, int size, int count)
        : server_ip_(ip), server_port_(port), packet_size_(size), packet_count_(count) {}

    bool run() {
        // 创建TCP Socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }

        // 配置服务器地址
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port_);
        if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid server IP" << std::endl;
            close(sock);
            return false;
        }

        // 连接服务器
        if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            close(sock);
            return false;
        }

        // 生成测试数据包
        std::vector<char> packet(packet_size_, 'A'); // 填充'A'的二进制数据

        // 测量时延
        for (int i = 0; i < packet_count_; ++i) {
            auto start = std::chrono::high_resolution_clock::now(); // 开始计时
            
            // 发送数据包
            if (send(sock, packet.data(), packet_size_, 0) != packet_size_) {
                std::cerr << "Send failed" << std::endl;
                break;
            }
            
            // 接收响应（假设服务器回显相同数据）
            std::vector<char> buffer(packet_size_);
            if (recv(sock, buffer.data(), packet_size_, MSG_WAITALL) != packet_size_) {
                std::cerr << "Receive failed" << std::endl;
                break;
            }
            
            auto end = std::chrono::high_resolution_clock::now(); // 结束计时
            double latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            latencies_.push_back(latency_us);
        }

        close(sock);
        return true;
    }

    void print_statistics() {
        if (latencies_.empty()) {
            std::cout << "No latency data collected" << std::endl;
            return;
        }

        // 排序以计算百分位数
        std::sort(latencies_.begin(), latencies_.end());
        
        // 计算统计值
        double total = 0;
        double min_val = latencies_[0];
        double max_val = latencies_[0];
        for (double lat : latencies_) {
            total += lat;
            if (lat < min_val) min_val = lat;
            if (lat > max_val) max_val = lat;
        }
        double avg_val = total / latencies_.size();
        double p50 = latencies_[latencies_.size() * 0.5];
        double p99 = latencies_[latencies_.size() * 0.99];

        // 输出结果
        std::cout << "===== Latency Statistics (μs) =====" << std::endl;
        std::cout << "Packets Sent: " << packet_count_ << " | Size: " << packet_size_ << " bytes" << std::endl;
        std::cout << "Min: " << min_val << " μs" << std::endl;
        std::cout << "Max: " << max_val << " μs" << std::endl;
        std::cout << "Avg: " << avg_val << " μs" << std::endl;
        std::cout << "P50: " << p50 << " μs" << std::endl;
        std::cout << "P99: " << p99 << " μs" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <port> <packet_size> <packet_count>" << std::endl;
        return 1;
    }

    // 解析参数
    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    int size = std::stoi(argv[3]);
    int count = std::stoi(argv[4]);

    // 运行客户端
    LatencyClient client(ip, port, size, count);
    if (!client.run()) {
        std::cerr << "Test failed" << std::endl;
        return 1;
    }
    
    client.print_statistics();
    return 0;
}