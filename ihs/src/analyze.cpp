#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <snappy-c.h>

// 认证结构体
struct AuthRequest {
    char user[5];
    char password[5];
};

// 数据包头部 (4字节seqno)
struct PacketHeader {
    uint32_t seqno;
};

const char* SERVER_IP = "192.168.1.100";
const int SERVER_PORT = 6000;
const int FIXED_CLIENT_PORT = 54321; // 固定客户端端口

int create_udp_socket() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return -1;

    // 绑定固定端口
    sockaddr_in client_addr{};
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(FIXED_CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    // 设置非阻塞
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    return sockfd;
}

bool send_auth(int sockfd) {
    AuthRequest auth{"user1", "pass1"}; // 替换实际账号密码
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    ssize_t sent = sendto(sockfd, &auth, sizeof(auth), 0,
                         (sockaddr*)&serv_addr, sizeof(serv_addr));
    return sent == sizeof(auth);
}


// 数据块结构
struct DataBlock {
    uint32_t length;
    char* compressed_data;
};

void process_packet(int sockfd) {
    char recv_buf[65536]; // 最大UDP包大小
    sockaddr_in src_addr{};
    socklen_t addr_len = sizeof(src_addr);

    while (true) {
        ssize_t recv_len = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0,
                                   (sockaddr*)&src_addr, &addr_len);
        if (recv_len <= 0) {
            if (errno == EAGAIN) usleep(1000); // 非阻塞等待
            continue;
        }

        // 记录seqno[12](@ref)
        PacketHeader* header = reinterpret_cast<PacketHeader*>(recv_buf);
        log_seqno(header->seqno); // 写入日志文件

        // 解析数据块数组
        char* ptr = recv_buf + sizeof(PacketHeader);
        size_t remaining = recv_len - sizeof(PacketHeader);

        while (remaining > sizeof(uint32_t)) {
            DataBlock block;
            block.length = *reinterpret_cast<uint32_t*>(ptr);
            ptr += sizeof(uint32_t);
            remaining -= sizeof(uint32_t);

            if (block.length > remaining) break; // 不完整块

            block.compressed_data = ptr;
            ptr += block.length;
            remaining -= block.length;

            // 提交到解压线程池
            decompress_queue.push(block);
        }
    }
}


void decompress_worker() {
    while (auto block = decompress_queue.pop()) {
        size_t uncompressed_len;
        snappy_uncompressed_length(block.compressed_data, 
                                  block.length, &uncompressed_len);

        char* uncompressed = memory_pool.allocate(uncompressed_len);
        snappy_status status = snappy_uncompress(block.compressed_data,
                                               block.length,
                                               uncompressed,
                                               &uncompressed_len);
        if (status == SNAPPY_OK) {
            algo_feed(uncompressed, uncompressed_len); // 喂给算法模块
        }
        memory_pool.release(uncompressed);
    }
}