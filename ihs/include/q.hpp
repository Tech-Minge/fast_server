#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

// 缓存行大小 (通常为64字节)
constexpr size_t CACHE_LINE_SIZE = 64;

// 固定大小无锁队列 (单生产者单消费者)
template <typename T, size_t Capacity>
class LockFreeQueue {
    // 确保容量是2的幂，这样我们可以使用位运算代替取模
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    LockFreeQueue() : head(0), tail(0) {
        // 初始化数组，确保每个元素都缓存行对齐
        for (size_t i = 0; i < Capacity; ++i) {
            buffer[i].value = T();
            buffer[i].ready = false;
        }
    }
    
    // 尝试推入元素 (生产者线程调用)
    bool try_push(const T& value) {
        const size_t current_tail = tail.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Capacity - 1);
        
        // 检查队列是否已满
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; // 队列已满
        }
        
        // 写入数据
        buffer[current_tail].value = value;
        
        // 设置就绪标志 (确保数据在标志设置前对其他线程可见)
        buffer[current_tail].ready.store(true, std::memory_order_release);
        
        // 更新tail指针
        tail.store(next_tail, std::memory_order_release);
        return true;
    }
    
    // 推入元素 (阻塞版本)
    void push(const T& value) {
        while (!try_push(value)) {
            // 队列满时暂停一段时间 (根据实际情况调整策略)
            std::this_thread::yield();
        }
    }
    
    // 尝试弹出元素 (消费者线程调用)
    bool try_pop(T& value) {
        const size_t current_head = head.load(std::memory_order_relaxed);
        
        // 检查队列是否为空
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false; // 队列为空
        }
        
        // 检查元素是否就绪
        if (!buffer[current_head].ready.load(std::memory_order_acquire)) {
            return false; // 元素尚未就绪
        }
        
        // 读取数据
        value = buffer[current_head].value;
        
        // 清除就绪标志 (确保在更新head前完成)
        buffer[current_head].ready.store(false, std::memory_order_release);
        
        // 更新head指针
        head.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
    
    // 弹出元素 (阻塞版本)
    T pop() {
        T value;
        while (!try_pop(value)) {
            // 队列空时暂停一段时间 (根据实际情况调整策略)
            std::this_thread::yield();
        }
        return value;
    }
    
    // 检查队列是否为空
    bool empty() const {
        return head.load(std::memory_order_acquire) == 
               tail.load(std::memory_order_acquire);
    }
    
    // 检查队列是否已满
    bool full() const {
        const size_t next_tail = (tail.load(std::memory_order_relaxed) + 1) & (Capacity - 1);
        return next_tail == head.load(std::memory_order_acquire);
    }
    
    // 获取队列大小
    size_t size() const {
        const size_t t = tail.load(std::memory_order_acquire);
        const size_t h = head.load(std::memory_order_acquire);
        return (t >= h) ? (t - h) : (Capacity - h + t);
    }

private:
    // 队列元素结构 (缓存行对齐)
    struct alignas(CACHE_LINE_SIZE) Element {
        T value;
        std::atomic<bool> ready;
    };
    
    // 队列缓冲区 (确保缓存行对齐)
    alignas(CACHE_LINE_SIZE) Element buffer[Capacity];
    
    // 生产者指针 (确保缓存行对齐)
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head;
    
    // 消费者指针 (确保缓存行对齐)
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail;
};