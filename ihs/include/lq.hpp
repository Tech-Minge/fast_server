#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <cstring>

// 用于避免false sharing的缓存行大小（通常为64字节）
constexpr size_t CACHE_LINE_SIZE = 64;

template<typename T, size_t Capacity>
class LockFreeQueue {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of two");

public:
    LockFreeQueue() : head_(0), tail_(0) {
        // 初始化缓冲区
        static_assert(sizeof(buffer_) == Capacity * sizeof(T), "Buffer size mismatch");
    }

    // 禁止拷贝和移动
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    ~LockFreeQueue() = default;

    // 入队操作，成功返回true，队列满返回false
    bool enqueue(const T& element) noexcept {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & mask_;

        // 检查队列是否已满
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        // 复制元素到缓冲区
        std::memcpy(&buffer_[current_tail], &element, sizeof(T));

        // 发布尾指针更新，确保元素已写入
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // 出队操作，成功返回true，队列空返回false
    bool dequeue(T& element) noexcept {
        size_t current_head = head_.load(std::memory_order_relaxed);

        // 检查队列是否为空
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        // 从缓冲区复制元素
        std::memcpy(&element, &buffer_[current_head], sizeof(T));

        // 发布头指针更新，确保元素已读取
        head_.store((current_head + 1) & mask_, std::memory_order_release);
        return true;
    }

    // 检查队列是否为空
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    // 检查队列是否已满
    bool full() const noexcept {
        size_t current_tail = tail_.load(std::memory_order_acquire);
        size_t next_tail = (current_tail + 1) & mask_;
        return next_tail == head_.load(std::memory_order_acquire);
    }

    // 获取当前元素数量
    size_t size() const noexcept {
        size_t current_head = head_.load(std::memory_order_acquire);
        size_t current_tail = tail_.load(std::memory_order_acquire);
        return (current_tail - current_head) & mask_;
    }

    // 获取队列容量
    static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    // 缓冲区掩码，用于快速计算环形索引
    static constexpr size_t mask_ = Capacity - 1;

    // 缓冲区存储元素
    alignas(CACHE_LINE_SIZE) T buffer_[Capacity];

    // 头指针和尾指针，使用缓存行对齐避免false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
};

#endif // LOCK_FREE_QUEUE_H
    