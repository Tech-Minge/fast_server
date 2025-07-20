#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <memory>
#include <chrono>

// 配置参数
constexpr int PORT = 8888;
constexpr int MAX_PACKET_SIZE = 1000;
constexpr int PACKETS_PER_BURST = 1000;
constexpr int BURST_INTERVAL_MS = 3000;
constexpr int RCVBUF_SIZE = PACKETS_PER_BURST * MAX_PACKET_SIZE * 2;  // 2MB
constexpr int EPOLL_TIMEOUT_MS = 1;
constexpr int WORKER_THREADS = 4;

// 内存池块（缓存行对齐）
struct alignas(64) PacketBuffer {
    char data[MAX_PACKET_SIZE];
    sockaddr_in src_addr;
    uint16_t len;
    uint64_t timestamp;
};

// 线程安全内存池
class MemoryPool {
public:
    MemoryPool(size_t size) : pool_size(size) {
        buffers.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            free_list.push(new PacketBuffer());
        }
    }

    PacketBuffer* acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        if (free_list.empty()) {
            return nullptr;  // 触发告警
        }
        PacketBuffer* buf = free_list.front();
        free_list.pop();
        return buf;
    }

    void release(PacketBuffer* buf) {
        std::lock_guard<std::mutex> lock(mutex);
        free_list.push(buf);
    }

    size_t available() const {
        return free_list.size();
    }

private:
    std::queue<PacketBuffer*> free_list;
    mutable std::mutex mutex;
    size_t pool_size;
    std::vector<PacketBuffer> buffers;
};

// 无锁队列（单生产者单消费者）
template <typename T, size_t Size>
class SPSCQueue {
public:
    bool push(T* item) {
        size_t next_tail = (tail + 1) % Size;
        if (next_tail == head.load(std::memory_order_acquire)) 
            return false;
        
        ring[tail] = item;
        tail = next_tail;
        return true;
    }

    T* pop() {
        if (head.load(std::memory_order_relaxed) == tail)
            return nullptr;
        
        T* item = ring[head];
        head = (head + 1) % Size;
        return item;
    }

private:
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) size_t tail{0};
    T* ring[Size];
};

// 工作线程
void worker_thread(SPSCQueue<PacketBuffer, 1024>& queue, 
                  MemoryPool& pool,
                  std::atomic<bool>& running) {
    while (running) {
        PacketBuffer* packet = queue.pop();
        if (!packet) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            continue;
        }

        // 解码处理（示例）
        // decode_packet(packet->data, packet->len);
        
        // 归还缓冲区
        pool.release(packet);
    }
}

int main() {
    // 创建UDP套接字
    int sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }

    // 设置接收缓冲区
    int recvbuf_size = RCVBUF_SIZE;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, 
                  &recvbuf_size, sizeof(recvbuf_size)) {
        perror("setsockopt SO_RCVBUF failed");
    }

    // 绑定地址
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }

    // 创建epoll实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 failed");
        close(sockfd);
        return 1;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev)) {
        perror("epoll_ctl failed");
        close(sockfd);
        close(epoll_fd);
        return 1;
    }

    // 初始化内存池（2倍突发容量）
    MemoryPool pool(PACKETS_PER_BURST * 2);
    
    // 创建工作线程和无锁队列
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;
    std::vector<SPSCQueue<PacketBuffer, 1024>> queues(WORKER_THREADS);
    
    for (int i = 0; i < WORKER_THREADS; ++i) {
        workers.emplace_back(worker_thread, 
                            std::ref(queues[i]), 
                            std::ref(pool),
                            std::ref(running));
        
        // 设置CPU亲和性和实时优先级
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % std::thread::hardware_concurrency(), &cpuset);
        pthread_setaffinity_np(workers.back().native_handle(), 
                              sizeof(cpu_set_t), &cpuset);
        
        struct sched_param sch{};
        sch.sched_priority = 99;
        pthread_setschedparam(workers.back().native_handle(), 
                             SCHED_FIFO, &sch);
    }

    // 负载均衡计数器
    size_t worker_index = 0;
    uint64_t burst_start = 0;
    int packets_in_burst = 0;
    
    // 主事件循环
    epoll_event events[10];
    while (running) {
        int n = epoll_wait(epoll_fd, events, 10, EPOLL_TIMEOUT_MS);
        if (n < 0) {
            perror("epoll_wait failed");
            break;
        }

        // 处理就绪事件
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == sockfd) {
                // 接收所有可用数据包
                while (true) {
                    // 从内存池获取缓冲区
                    PacketBuffer* buf = pool.acquire();
                    if (!buf) {
                        // 内存耗尽告警
                        std::cerr << "ALERT: Memory pool exhausted!" << std::endl;
                        
                        // 丢弃数据包（避免阻塞）
                        char temp[1024];
                        sockaddr_in src_addr;
                        socklen_t addr_len = sizeof(src_addr);
                        recvfrom(sockfd, temp, sizeof(temp), 
                                 MSG_DONTWAIT, 
                                 (sockaddr*)&src_addr, &addr_len);
                        continue;
                    }

                    // 接收数据
                    socklen_t addr_len = sizeof(buf->src_addr);
                    ssize_t len = recvfrom(sockfd, buf->data, MAX_PACKET_SIZE,
                                         MSG_DONTWAIT,
                                         (sockaddr*)&buf->src_addr, &addr_len);
                    
                    if (len <= 0) {
                        pool.release(buf);
                        if (errno == EAGAIN || errno == EWOULDBLOCK) 
                            break;  // 无更多数据
                        
                        perror("recvfrom failed");
                        continue;
                    }

                    buf->len = len;
                    buf->timestamp = __builtin_ia32_rdtsc();  // 高精度时间戳
                    
                    // 突发检测
                    uint64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
                    if (now - burst_start > BURST_INTERVAL_MS * 1e6) {
                        burst_start = now;
                        packets_in_burst = 0;
                    }
                    
                    if (++packets_in_burst > PACKETS_PER_BURST * 1.2) {
                        std::cerr << "ALERT: Burst overflow detected!" << std::endl;
                    }

                    // 分发到工作线程（轮询负载均衡）
                    while (!queues[worker_index].push(buf)) {
                        // 队列满时尝试下一个线程
                        worker_index = (worker_index + 1) % WORKER_THREADS;
                        std::this_thread::yield();
                    }
                    worker_index = (worker_index + 1) % WORKER_THREADS;
                }
            }
        }
    }

    // 清理资源
    running = false;
    for (auto& w : workers) w.join();
    close(sockfd);
    close(epoll_fd);
    return 0;
}