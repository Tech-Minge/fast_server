#include <atomic>
#include <cstdint>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

// 订单结构（缓存行对齐）
struct alignas(64) Order {
    uint64_t order_id;
    double price;
    int32_t volume;
    char symbol[16];
};

// 共享内存区域（环形缓冲区）
struct SharedMemory {
    std::atomic<size_t> head;  // 生产者写入位置 [c1]
    std::atomic<size_t> tail;  // 消费者读取位置 [c1]
    Order orders[1024];        // 订单缓冲区
};

const char* SHM_NAME = "/order_buffer";
const size_t BUFFER_SIZE = sizeof(SharedMemory);

// 生产者：写入解析完成的订单
void producer() {
    // 创建并映射共享内存
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, BUFFER_SIZE);
    SharedMemory* buffer = static_cast<SharedMemory*>(
        mmap(nullptr, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)
    );
    
    new (&buffer->head) std::atomic<size_t>(0); // 原位构造原子变量
    new (&buffer->tail) std::atomic<size_t>(0);

    while (true) {
        // 1. 解析订单（模拟）
        Order new_order{
            .order_id = 12345,
            .price = 100.25,
            .volume = 200,
            .symbol = "AAPL"
        };

        // 2. 计算写入位置
        size_t head = buffer->head.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) % 1024;

        // 3. 等待空闲槽位（自旋）
        while (next_head == buffer->tail.load(std::memory_order_acquire)) {
            // 缓冲区满时忙等待（可插入pause指令优化）
            asm volatile("pause");
        }

        // 4. 写入订单
        buffer->orders[head] = new_order; // 内存拷贝

        // 5. 更新head（确保写入完成后再移动指针）
        buffer->head.store(next_head, std::memory_order_release);
    }
}

// 消费者（algo模块）：读取订单
void consumer() {
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    SharedMemory* buffer = static_cast<SharedMemory*>(
        mmap(nullptr, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)
    );

    while (true) {
        size_t tail = buffer->tail.load(std::memory_order_relaxed);
        
        // 1. 检查新订单（自旋等待）
        while (tail == buffer->head.load(std::memory_order_acquire)) {
            asm volatile("pause"); // 降低CPU占用
        }

        // 2. 读取订单
        Order order = buffer->orders[tail]; // 内存拷贝
        
        // 3. 处理订单（algo逻辑）
        std::cout << "Processed order: " << order.order_id 
                  << " Symbol: " << order.symbol << std::endl;

        // 4. 更新tail（确保处理完成后再移动指针）
        buffer->tail.store((tail + 1) % 1024, std::memory_order_release);
    }
}



