#include <atomic>
#include <vector>
#include <cstdint>

template<typename T, size_t Capacity>
class MPSCQueue {
private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    
    std::vector<T> buffer;
    const size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    alignas(64) std::atomic<size_t> cache_tail; // 消费者缓存的tail，减少缓存失效

public:
    MPSCQueue() : capacity(Capacity), head(0), tail(0), cache_tail(0) {
        buffer.resize(capacity);
    }

    bool enqueue(const T& value) {
        const size_t current_head = head.load(std::memory_order_relaxed);
        const size_t next_head = current_head + 1;
        
        // 检查队列是否已满
        if (next_head - cache_tail.load(std::memory_order_acquire) > capacity) {
            // 更新缓存的tail，再次检查
            cache_tail.store(tail.load(std::memory_order_acquire), std::memory_order_relaxed);
            if (next_head - cache_tail.load(std::memory_order_relaxed) > capacity) {
                return false; // 队列已满
            }
        }
        
        // 将数据写入缓冲区
        buffer[current_head & (capacity - 1)] = value;
        
        // 发布数据（内存屏障确保数据写入完成后才更新head）
        head.store(next_head, std::memory_order_release);
        return true;
    }

    bool dequeue(T& value) {
        const size_t current_tail = tail.load(std::memory_order_relaxed);
        
        // 检查队列是否为空
        if (current_tail >= head.load(std::memory_order_acquire)) {
            return false; // 队列为空
        }
        
        // 读取数据
        value = buffer[current_tail & (capacity - 1)];
        
        // 更新tail（内存屏障确保数据读取完成后才更新tail）
        tail.store(current_tail + 1, std::memory_order_release);
        return true;
    }
};

#include <atomic>
#include <vector>
#include <cstdint>

template<typename T, size_t Capacity>
class SPSCQueue {
private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    
    std::vector<T> buffer;
    const size_t capacity;
    alignas(64) std::atomic<size_t> head; // 生产者指针
    alignas(64) std::atomic<size_t> tail; // 消费者指针

public:
    SPSCQueue() : capacity(Capacity), head(0), tail(0) {
        buffer.resize(capacity);
    }

    bool enqueue(const T& value) {
        const size_t current_head = head.load(std::memory_order_relaxed);
        const size_t next_head = current_head + 1;
        
        // 检查队列是否已满（无需原子操作，因为只有一个生产者）
        if (next_head - tail.load(std::memory_order_acquire) > capacity) {
            return false; // 队列已满
        }
        
        // 将数据写入缓冲区
        buffer[current_head & (capacity - 1)] = value;
        
        // 发布数据（内存屏障确保数据写入完成后才更新head）
        head.store(next_head, std::memory_order_release);
        return true;
    }

    bool dequeue(T& value) {
        const size_t current_tail = tail.load(std::memory_order_relaxed);
        
        // 检查队列是否为空（无需原子操作，因为只有一个消费者）
        if (current_tail >= head.load(std::memory_order_acquire)) {
            return false; // 队列为空
        }
        
        // 读取数据
        value = buffer[current_tail & (capacity - 1)];
        
        // 更新tail（内存屏障确保数据读取完成后才更新tail）
        tail.store(current_tail + 1, std::memory_order_release);
        return true;
    }
};

#include <atomic>
#include <thread>

template<typename T, size_t Capacity>
class MPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
private:
    struct Node {
        alignas(64) std::atomic<size_t> next;
        T data;
    };

    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    Node buffer_[Capacity];

public:
    MPSCQueue() {
        // 初始化链表：每个节点指向下一个位置
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].next.store((i + 1) % Capacity, std::memory_order_relaxed);
        }
    }

    // 多生产者入队
    bool enqueue(const T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = buffer_[tail].next.load(std::memory_order_acquire);

        // 检查队列是否满
        if (next == head_.load(std::memory_order_acquire)) return false;

        // 写入数据
        buffer_[tail].data = item;
        // CAS更新tail，解决多生产者竞争
        while (!tail_.compare_exchange_weak(
            tail, next, 
            std::memory_order_release, 
            std::memory_order_relaxed
        )) {
            next = buffer_[tail].next.load(std::memory_order_acquire);
        }
        return true;
    }

    // 单消费者出队
    bool dequeue(T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        // 检查队列是否空
        if (head == tail_.load(std::memory_order_acquire)) return false;

        item = std::move(buffer_[head].data);
        head_.store(buffer_[head].next, std::memory_order_release);
        return true;
    }
};

#include <atomic>
#include <cstddef>
#include <utility>

// 单生产者单消费者队列
template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
private:
    // 缓存行对齐（64字节），避免伪共享
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    // 存储缓冲区（支持非POD类型）
    alignas(64) std::aligned_storage_t<sizeof(T), alignof(T)> buffer_[Capacity];
    // 本地缓存head值，减少原子操作
    size_t cached_head_{0};

public:
    SPSCQueue() = default;

    ~SPSCQueue() {
        // 析构残留对象
        while (size() > 0) {
            T item;
            dequeue(item);
        }
    }

    // 入队（生产者调用）
    template<typename U>
    bool enqueue(U&& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (tail + 1) & (Capacity - 1); // 位运算替代取模

        // 检查队列是否满（优先读缓存）
        if (next_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (next_tail == cached_head_) return false;
        }

        // 原地构造对象（避免拷贝）
        new (&buffer_[tail]) T(std::forward<U>(item));
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // 出队（消费者调用）
    bool dequeue(T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        // 检查队列是否空
        if (head == tail_.load(std::memory_order_acquire)) return false;

        // 取出对象并析构
        item = std::move(*reinterpret_cast<T*>(&buffer_[head]));
        reinterpret_cast<T*>(&buffer_[head])->~T();

        size_t next_head = (head + 1) & (Capacity - 1);
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // 当前队列大小
    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return (tail >= head) ? (tail - head) : (Capacity - head + tail);
    }
};


#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <iostream>
#include <cassert>

// 通用环形队列基类
template <typename T, size_t BufferSize>
class RingBufferBase {
protected:
    static_assert((BufferSize & (BufferSize - 1)) == 0, 
                  "BufferSize must be a power of 2");
    static constexpr size_t IndexMask = BufferSize - 1;
    
    T buffer_[BufferSize];
    
public:
    virtual ~RingBufferBase() = default;
    virtual bool push(const T& item) = 0;
    virtual bool pop(T& item) = 0;
};

// ================= SPSC 队列 =================
template <typename T, size_t BufferSize>
class SPSCRingBuffer : public RingBufferBase<T, BufferSize> {
    using Base = RingBufferBase<T, BufferSize>;
    using Base::buffer_;
    using Base::IndexMask;
    
    // 缓存行对齐防止伪共享
    alignas(64) std::atomic<uint64_t> head_{0};  // 消费者位置
    alignas(64) std::atomic<uint64_t> tail_{0};  // 生产者位置

public:
    SPSCRingBuffer() = default;

    // 生产者：入队操作
    bool push(const T& item) override {
        const uint64_t current_tail = tail_.load(std::memory_order_relaxed);
        const uint64_t next_tail = current_tail + 1;
        
        // 检查队列是否已满
        if (next_tail - head_.load(std::memory_order_acquire) > BufferSize) {
            return false;
        }
        
        // 写入数据
        buffer_[current_tail & IndexMask] = item;
        
        // 释放语义：确保写入在更新tail前完成
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // 消费者：出队操作
    bool pop(T& item) override {
        const uint64_t current_head = head_.load(std::memory_order_relaxed);
        
        // 检查队列是否为空
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // 读取数据
        item = buffer_[current_head & IndexMask];
        
        // 释放语义：确保读取在更新head前完成
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // 带忙等待的入队
    void push_busywait(const T& item) {
        while (!push(item)) {
            // 减少CPU占用
            #if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
            #endif
        }
    }

    // 带忙等待的出队
    T pop_busywait() {
        T item;
        while (!pop(item)) {
            #if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
            #endif
        }
        return item;
    }
};

// ================= MPSC 队列 =================
template <typename T, size_t BufferSize>
class MPSCRingBuffer : public RingBufferBase<T, BufferSize> {
    using Base = RingBufferBase<T, BufferSize>;
    using Base::buffer_;
    using Base::IndexMask;
    
    struct Slot {
        std::atomic<uint64_t> sequence;
        T data;
    };
    
    alignas(64) Slot slots_[BufferSize];
    alignas(64) std::atomic<uint64_t> head_{0};  // 消费者位置
    alignas(64) std::atomic<uint64_t> tail_{0};  // 生产者位置

public:
    MPSCRingBuffer() {
        // 初始化槽位序列号
        for (size_t i = 0; i < BufferSize; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // 生产者：入队操作（多线程安全）
    bool push(const T& item) override {
        uint64_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t slot_index;
        uint64_t expected_seq;
        
        while (true) {
            slot_index = current_tail & IndexMask;
            expected_seq = slots_[slot_index].sequence.load(std::memory_order_acquire);
            
            // 检查槽位是否可用
            const int64_t diff = static_cast<int64_t>(expected_seq) - 
                                 static_cast<int64_t>(current_tail);
            
            if (diff == 0) {
                // 尝试获取槽位
                if (tail_.compare_exchange_weak(
                    current_tail, 
                    current_tail + 1,
                    std::memory_order_relaxed
                )) {
                    break; // 成功获取槽位
                }
            } else if (diff < 0) {
                // 队列已满
                return false;
            } else {
                // 槽位被其他生产者占用，重试
                current_tail = tail_.load(std::memory_order_relaxed);
            }
        }
        
        // 写入数据
        slots_[slot_index].data = item;
        
        // 标记槽位为已填充
        slots_[slot_index].sequence.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // 消费者：出队操作（单线程）
    bool pop(T& item) override {
        const uint64_t current_head = head_.load(std::memory_order_relaxed);
        const size_t slot_index = current_head & IndexMask;
        const uint64_t expected_seq = current_head + 1;
        
        // 检查数据是否就绪
        if (slots_[slot_index].sequence.load(std::memory_order_acquire) != expected_seq) {
            return false;
        }
        
        // 读取数据
        item = slots_[slot_index].data;
        
        // 重置槽位序列号
        slots_[slot_index].sequence.store(current_head + BufferSize, std::memory_order_release);
        
        // 更新消费位置
        head_.store(current_head + 1, std::memory_order_relaxed);
        return true;
    }

    // 带忙等待的入队
    void push_busywait(const T& item) {
        while (!push(item)) {
            #if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
            #endif
        }
    }

    // 带忙等待的出队
    T pop_busywait() {
        T item;
        while (!pop(item)) {
            #if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
            #endif
        }
        return item;
    }
};

// ================== 测试函数 ==================
void test_SPSC() {
    SPSCRingBuffer<int, 1024> queue;
    const int test_count = 1000000;
    int producer_sum = 0;
    int consumer_sum = 0;

    // 生产者线程
    std::thread producer([&] {
        for (int i = 0; i < test_count; ++i) {
            queue.push_busywait(i);
            producer_sum += i;
        }
    });

    // 消费者线程
    std::thread consumer([&] {
        for (int i = 0; i < test_count; ++i) {
            int val = queue.pop_busywait();
            consumer_sum += val;
        }
    });

    producer.join();
    consumer.join();

    assert(producer_sum == consumer_sum);
    std::cout << "SPSC test passed. Sum: " << consumer_sum << std::endl;
}

void test_MPSC() {
    MPSCRingBuffer<int, 1024> queue;
    const int test_count = 100000;
    const int producer_count = 4;
    std::atomic<int> producer_sum(0);
    int consumer_sum = 0;

    // 多个生产者线程
    std::vector<std::thread> producers;
    for (int i = 0; i < producer_count; ++i) {
        producers.emplace_back([&, i] {
            int local_sum = 0;
            for (int j = 0; j < test_count; ++j) {
                int item = i * test_count + j;
                queue.push_busywait(item);
                local_sum += item;
            }
            producer_sum.fetch_add(local_sum);
        });
    }

    // 单消费者线程
    std::thread consumer([&] {
        for (int i = 0; i < test_count * producer_count; ++i) {
            consumer_sum += queue.pop_busywait();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    assert(producer_sum.load() == consumer_sum);
    std::cout << "MPSC test passed. Sum: " << consumer_sum << std::endl;
}

int main() {
    test_SPSC();
    test_MPSC();
    return 0;
}