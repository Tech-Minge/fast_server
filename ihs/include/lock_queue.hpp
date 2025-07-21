#include <atomic>
#include <array>
#include <cstddef>

template<typename T, size_t Capacity>
class LockFreeQueue {
private:
    struct PaddedAtomic {
        std::atomic<size_t> value{0};
        char padding[64 - sizeof(std::atomic<size_t>)];
    };

    std::array<T, Capacity> buffer;
    PaddedAtomic head; // 头指针（独占缓存行）
    PaddedAtomic tail; // 尾指针（独占缓存行）

public:
    bool push(const T& item) {
        size_t curr_tail = tail.value.load(std::memory_order_relaxed);
        size_t next_tail = (curr_tail + 1) % Capacity;
        
        // 队列满检查（acquire屏障防止重排）
        if (next_tail == head.value.load(std::memory_order_acquire))
            return false;

        buffer[curr_tail] = item;
        tail.value.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t curr_head = head.value.load(std::memory_order_relaxed);
        if (curr_head == tail.value.load(std::memory_order_acquire)) 
            return false;

        item = buffer[curr_head];
        size_t next_head = (curr_head + 1) % Capacity;
        head.value.store(next_head, std::memory_order_release);
        return true;
    }

    size_t size() const {
        size_t t = tail.value.load(std::memory_order_acquire);
        size_t h = head.value.load(std::memory_order_acquire);
        return (t >= h) ? (t - h) : (Capacity - h + t);
    }
};