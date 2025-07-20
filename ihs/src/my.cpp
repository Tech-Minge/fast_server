#include <vector>
#include <thread>
#include <atomic>
#include <vector>
#include <array>
#include <cstring>

// 无锁环形缓冲区（固定大小）
template <typename T, size_t N>
class LockFreeRingBuffer {
public:
    bool push(T&& item) {
        size_t next_tail = (tail + 1) % N;
        if (next_tail == head) return false; // 队列满
        buffer[tail] = std::move(item);
        tail = next_tail;
        return true;
    }

    bool pop(T& item) {
        if (head == tail) return false; // 队列空
        item = std::move(buffer[head]);
        head = (head + 1) % N;
        return true;
    }

private:
    std::array<T, N> buffer;
    std::atomic<size_t> head{0}, tail{0};
};

// 线程池（低延迟优化）
class LowLatencyThreadPool {
public:
    LowLatencyThreadPool(size_t threads, size_t queue_size) 
        : queues(threads), stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this, i] {
                uint8_t cpu_id = i; // 绑定线程到CPU核心
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_id, &cpuset);
                pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

                Task task;
                while (!stop) {
                    if (queues[i].pop(task)) task();
                }
            });
        }
    }

    ~LowLatencyThreadPool() {
        stop = true;
        for (auto& worker : workers) worker.join();
    }

    // 提交任务（传入预分配内存的指针）
    bool enqueue(void* data, size_t len, size_t thread_id) {
        auto task = [=] { decode(data, len); }; // 避免拷贝
        return queues[thread_id % queues.size()].push(std::move(task));
    }

private:
    void decode(void* data, size_t len) {
        // 解码逻辑（直接操作原始内存）
        // 示例：memcpy(decoded_buf, data, len);
    }

    std::vector<std::thread> workers;
    std::vector<LockFreeRingBuffer<std::function<void()>, 1024>> queues; // 每线程独立队列
    std::atomic<bool> stop;
};

// 使用示例
int main() {
    LowLatencyThreadPool pool(4, 1000); // 4线程，队列深度1000

    // 接收UDP数据并提交任务
    char recv_buf[1000];
    sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    while (true) {
        ssize_t n = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, 
                            (sockaddr*)&src_addr, &addr_len);
        if (n > 0) {
            void* data_buf = malloc(n); // 实际用内存池分配
            memcpy(data_buf, recv_buf, n);
            pool.enqueue(data_buf, n, 0); // 提交到线程0的队列
        }
    }
}