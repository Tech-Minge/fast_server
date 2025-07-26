#define MAX_PACKETS 1000

typedef struct {
    uint8_t data[1024];     // 数据负载
    std::atomic<bool> ready; // 原子状态标志 (false=可用, true=处理中)
} Packet;

Packet packets[MAX_PACKETS]; // 栈预分配

// 上游写入数据后标记为就绪
void upstream_send(int index) {
    packets[index].ready.store(true, std::memory_order_release); // 释放语义屏障
}

// 下游处理完成后重置状态
void downstream_finish(int index) {
    packets[index].ready.store(false, std::memory_order_release);
}

// 上游检查重用安全性
bool is_safe_to_reuse(int index) {
    return !packets[index].ready.load(std::memory_order_acquire); // 获取语义屏障
}


/*
### 高效的数据包处理同步方案

针对您的需求，我设计了一种基于原子计数器和环形缓冲区的高效同步机制，确保上游在循环使用Packet数组时，能够安全地知道下游何时完成处理。

### 核心设计思路

1. **双缓冲机制**：使用两个原子计数器分别跟踪生产和消费进度
2. **环形缓冲区**：利用模运算实现数组的循环使用
3. **无锁设计**：通过原子操作避免锁竞争，提高性能
4. **批量处理**：支持一次性处理多个数据包，减少同步频率

### 完整实现代码

```cpp
#include <atomic>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <cstring>

// 假设的Packet结构
struct Packet {
    char data[1024];  // 数据包内容
    // 其他元数据
};

class PacketProcessor {
public:
    // 构造函数，传入预分配的Packet数组和大小
    PacketProcessor(Packet* packets, size_t size)
        : packets_(packets), size_(size), 
          producerPos_(0), consumerPos_(0) {
        if (size == 0) {
            throw std::invalid_argument("Packet array size must be greater than 0");
        }
    }
    
    // 上游获取一个可用的Packet
    // 返回：成功时返回Packet指针和索引，失败时返回nullptr
    Packet* acquirePacket(size_t& index) {
        // 获取当前生产者位置
        size_t currentPos = producerPos_.load(std::memory_order_relaxed);
        
        // 计算下一个位置
        size_t nextPos = (currentPos + 1) % size_;
        
        // 检查下游是否已经处理完当前位置的Packet
        if (nextPos == consumerPos_.load(std::memory_order_acquire)) {
            // 缓冲区已满，没有可用的Packet
            return nullptr;
        }
        
        // 获取当前位置的Packet
        index = currentPos;
        Packet* packet = &packets_[currentPos];
        
        // 更新生产者位置（原子操作，确保对下游可见）
        producerPos_.store(nextPos, std::memory_order_release);
        
        return packet;
    }
    
    // 上游批量获取多个可用的Packet
    // 返回：实际获取的Packet数量
    size_t acquirePackets(size_t count, Packet** packets, size_t* indices) {
        // 获取当前生产者位置
        size_t currentPos = producerPos_.load(std::memory_order_relaxed);
        
        // 计算可用的Packet数量
        size_t available = 0;
        size_t pos = currentPos;
        
        while (available < count && pos != consumerPos_.load(std::memory_order_acquire)) {
            packets[available] = &packets_[pos];
            indices[available] = pos;
            available++;
            pos = (pos + 1) % size_;
        }
        
        if (available > 0) {
            // 更新生产者位置（原子操作）
            producerPos_.store(pos, std::memory_order_release);
        }
        
        return available;
    }
    
    // 下游标记某个Packet已处理完成
    void markPacketProcessed(size_t index) {
        // 确保所有对Packet的写操作都完成
        std::atomic_thread_fence(std::memory_order_release);
        
        // 更新消费者位置到指定索引之后
        // 注意：这里假设下游按顺序处理Packet
        if (index != consumerPos_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("Packets must be processed in order");
        }
        
        size_t nextPos = (index + 1) % size_;
        consumerPos_.store(nextPos, std::memory_order_release);
    }
    
    // 下游获取下一个待处理的Packet
    // 返回：成功时返回Packet指针和索引，失败时返回nullptr
    Packet* getNextPacket(size_t& index) {
        // 获取当前消费者位置
        size_t currentPos = consumerPos_.load(std::memory_order_relaxed);
        
        // 检查是否有新的Packet需要处理
        if (currentPos == producerPos_.load(std::memory_order_acquire)) {
            // 没有新的Packet
            return nullptr;
        }
        
        // 获取当前位置的Packet
        index = currentPos;
        Packet* packet = &packets_[currentPos];
        
        return packet;
    }
    
    // 检查是否有可处理的Packet
    bool hasMorePackets() const {
        return consumerPos_.load(std::memory_order_acquire) != 
               producerPos_.load(std::memory_order_acquire);
    }
    
private:
    Packet* packets_;         // 预分配的Packet数组
    const size_t size_;       // 数组大小
    
    // 生产者和消费者位置（原子操作）
    std::atomic<size_t> producerPos_;
    std::atomic<size_t> consumerPos_;
};

// 使用示例
#include <iostream>
#include <thread>
#include <vector>

void exampleUsage() {
    // 预分配1000个Packet对象
    constexpr size_t PACKET_COUNT = 1000;
    std::vector<Packet> packetBuffer(PACKET_COUNT);
    
    // 创建处理器
    PacketProcessor processor(packetBuffer.data(), PACKET_COUNT);
    
    // 上游线程：生产数据
    std::thread producer([&]() {
        for (int i = 0; i < 10000; ++i) {
            size_t index;
            Packet* packet = processor.acquirePacket(index);
            
            if (packet) {
                // 填充数据
                snprintf(packet->data, sizeof(packet->data), "Packet #%d", i);
                
                std::cout << "Produced packet at index " << index << ": " << packet->data << std::endl;
                
                // 模拟处理时间
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // 缓冲区已满，等待
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });
    
    // 下游线程：消费数据
    std::thread consumer([&]() {
        while (true) {
            size_t index;
            Packet* packet = processor.getNextPacket(index);
            
            if (packet) {
                // 处理数据
                std::cout << "Consumed packet at index " << index << ": " << packet->data << std::endl;
                
                // 标记处理完成
                processor.markPacketProcessed(index);
                
                // 模拟处理时间
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else {
                // 没有数据，短暂休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    });
    
    // 等待生产者完成
    producer.join();
    // 消费者线程可以持续运行或通过其他方式终止
}
```

### 关键实现细节

1. **原子操作**：
   - 使用`std::atomic`确保多线程环境下的安全访问
   - 通过内存屏障（`std::memory_order_*`）保证数据可见性
   - 实现无锁同步，避免锁竞争带来的性能开销

2. **环形缓冲区**：
   - 通过模运算实现数组的循环使用
   - 生产者和消费者各自维护独立的索引
   - 当生产者追上消费者时，表示缓冲区已满

3. **批量处理**：
   - 提供`acquirePackets`方法支持一次性获取多个Packet
   - 减少原子操作次数，提高吞吐量

4. **同步机制**：
   - 生产者在获取Packet前检查消费者位置
   - 消费者按顺序处理Packet，并标记处理完成
   - 通过原子计数器实现高效同步

这种设计可以确保上游在循环使用Packet数组时，不会覆盖下游尚未处理的数据，同时保持了高性能和线程安全。*/

/*
# 高效 Packet 生命周期管理实现

以下是一个高效管理 packet 对象生命周期的实现，使用无锁环形缓冲区、原子操作和条件变量来确保上游安全地重用 packet 对象：

```cpp
#include <atomic>
#include <vector>
#include <array>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <cassert>

// Packet 结构
struct Packet {
    std::array<char, 1024> data;
    uint64_t sequence = 0;
};

// 无锁环形缓冲区管理 Packet 状态
class PacketManager {
public:
    static constexpr size_t BUFFER_SIZE = 1000;
    static_assert((BUFFER_SIZE & (BUFFER_SIZE - 1)) == 0, "Buffer size must be power of two");
    
    PacketManager() : buffer_(BUFFER_SIZE) {
        // 初始化所有 packet 为可用状态
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            buffer_[i].packet = &packets_[i];
            buffer_[i].state.store(State::Available, std::memory_order_relaxed);
        }
    }
    
    // 获取下一个可用的 packet
    Packet* acquirePacket() {
        const size_t index = next_index_.fetch_add(1, std::memory_order_relaxed) & (BUFFER_SIZE - 1);
        
        // 快速路径：尝试直接获取
        State expected = State::Available;
        if (buffer_[index].state.compare_exchange_strong(
            expected, 
            State::Acquired, 
            std::memory_order_acquire,
            std::memory_order_relaxed))
        {
            return buffer_[index].packet;
        }
        
        // 慢速路径：等待 packet 可用
        return waitForPacket(index);
    }
    
    // 通知下游已填充 packet
    void submitPacket(Packet* packet) {
        const size_t index = packet - packets_.data();
        assert(index < BUFFER_SIZE);
        buffer_[index].state.store(State::Processing, std::memory_order_release);
    }
    
    // 下游通知处理完成
    void completePacket(Packet* packet) {
        const size_t index = packet - packets_.data();
        assert(index < BUFFER_SIZE);
        
        // 设置状态为可用
        State old_state = buffer_[index].state.exchange(
            State::Available, 
            std::memory_order_acq_rel);
            
        // 确保状态转换有效
        assert(old_state == State::Processing);
        
        // 如果有上游在等待，通知它
        if (buffer_[index].waiters.load(std::memory_order_relaxed) > 0) {
            std::lock_guard<std::mutex> lock(wait_mutex_);
            wait_cv_.notify_all();
        }
    }

private:
    // Packet 状态
    enum class State : uint8_t {
        Available,    // 可用，上游可填充
        Acquired,     // 上游已获取，正在填充
        Processing    // 已提交下游处理
    };
    
    // 带状态的 Packet 项
    struct PacketItem {
        Packet* packet;
        std::atomic<State> state;
        std::atomic<uint32_t> waiters{0};
    };
    
    // 实际存储的 packets
    std::array<Packet, BUFFER_SIZE> packets_;
    
    // 带状态管理的 buffer
    std::vector<PacketItem> buffer_;
    
    // 下一个分配的索引
    std::atomic<size_t> next_index_{0};
    
    // 等待同步
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    
    // 等待 packet 可用
    Packet* waitForPacket(size_t index) {
        // 增加等待者计数
        buffer_[index].waiters.fetch_add(1, std::memory_order_relaxed);
        
        // 等待状态变为可用
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_cv_.wait(lock, [&] {
            State state = buffer_[index].state.load(std::memory_order_acquire);
            return state == State::Available;
        });
        
        // 尝试获取
        State expected = State::Available;
        if (!buffer_[index].state.compare_exchange_strong(
            expected, 
            State::Acquired, 
            std::memory_order_acquire,
            std::memory_order_relaxed))
        {
            // 获取失败（可能被其他线程抢走）
            buffer_[index].waiters.fetch_sub(1, std::memory_order_relaxed);
            return nullptr; // 需要重新尝试
        }
        
        // 减少等待者计数
        buffer_[index].waiters.fetch_sub(1, std::memory_order_relaxed);
        return buffer_[index].packet;
    }
};

// 下游处理模块
class DownstreamProcessor {
public:
    DownstreamProcessor(PacketManager& manager) : manager_(manager) {}
    
    void process(Packet* packet) {
        // 模拟处理过程
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        
        // 处理完成后通知上游
        manager_.completePacket(packet);
    }

private:
    PacketManager& manager_;
};

// 上游生产模块
class UpstreamProducer {
public:
    UpstreamProducer(PacketManager& manager, DownstreamProcessor& processor)
        : manager_(manager), processor_(processor) {}
    
    void run() {
        uint64_t sequence = 0;
        
        while (true) {
            // 获取 packet
            Packet* packet = manager_.acquirePacket();
            
            // 填充数据
            packet->sequence = sequence++;
            std::fill(packet->data.begin(), packet->data.end(), sequence % 256);
            
            // 提交给下游处理
            manager_.submitPacket(packet);
            processor_.process(packet);
        }
    }

private:
    PacketManager& manager_;
    DownstreamProcessor& processor_;
};

// 测试函数
void test_packet_reuse() {
    PacketManager manager;
    DownstreamProcessor processor(manager);
    UpstreamProducer producer(manager, processor);
    
    // 运行生产-消费循环
    std::thread producer_thread([&] { producer.run(); });
    
    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 停止生产者
    // 实际应用中应有更优雅的停止机制
    producer_thread.detach();
}

int main() {
    test_packet_reuse();
    std::cout << "Packet reuse test completed.\n";
    return 0;
}
```

## 设计说明

### 核心数据结构

```cpp
struct PacketItem {
    Packet* packet;
    std::atomic<State> state;
    std::atomic<uint32_t> waiters{0};
};

std::array<Packet, BUFFER_SIZE> packets_;
std::vector<PacketItem> buffer_;
```

1. **Packet 数组**：
   - 预分配在栈上的 1000 个 Packet 对象
   - 固定大小，避免动态内存分配

2. **PacketItem 管理**：
   - 包含 Packet 指针和状态信息
   - 原子状态管理
   - 等待者计数器

### 状态管理

```cpp
enum class State : uint8_t {
    Available,    // 可用，上游可填充
    Acquired,     // 上游已获取，正在填充
    Processing    // 已提交下游处理
};
```

状态转换图：
```
Available → Acquired → Processing → Available
```

### 关键操作

#### 1. 上游获取 Packet
```cpp
Packet* acquirePacket() {
    // 计算下一个索引
    const size_t index = next_index_++ & MASK;
    
    // 尝试快速获取
    if (state.compare_exchange_strong(Available, Acquired)) {
        return packet;
    }
    
    // 慢速路径等待
    return waitForPacket(index);
}
```

#### 2. 上游提交 Packet
```cpp
void submitPacket(Packet* packet) {
    // 设置状态为 Processing
    state.store(State::Processing, std::memory_order_release);
}
```

#### 3. 下游完成处理
```cpp
void completePacket(Packet* packet) {
    // 设置状态为 Available
    state.exchange(State::Available, std::memory_order_acq_rel);
    
    // 如果有等待者，通知他们
    if (waiters > 0) {
        notify_waiters();
    }
}
```

### 性能优化技术

1. **无锁快速路径**：
   - 使用 `compare_exchange_strong` 原子操作
   - 在大多数情况下避免锁竞争

2. **批量通知**：
   - 只有存在等待者时才通知条件变量
   - 减少不必要的唤醒

3. **内存局部性**：
   - Packet 对象连续存储
   - 缓存友好的访问模式

4. **状态压缩**：
   - 使用 uint8_t 表示状态
   - 与 Packet 指针相邻存储

5. **环形缓冲区**：
   - 使用位掩码替代取模运算
   - 索引自动回绕

### 多线程安全设计

1. **内存顺序**：
   - `std::memory_order_acquire` 用于加载关键状态
   - `std::memory_order_release` 用于存储关键状态
   - `std::memory_order_acq_rel` 用于状态转换

2. **状态转换原子性**：
   - 所有状态转换使用原子操作
   - 确保状态变更的可见性

3. **条件变量保护**：
   - 使用互斥锁保护条件变量
   - 避免丢失唤醒

### 使用模式

```cpp
// 上游
Packet* packet = manager.acquirePacket();
fillPacket(packet); // 填充数据
manager.submitPacket(packet); // 提交给下游

// 下游
processPacket(packet); // 处理数据
manager.completePacket(packet); // 通知处理完成
```

### 性能优化选项

#### 1. 无等待获取（忙等待）
```cpp
Packet* acquirePacketNoWait(size_t index) {
    constexpr int MAX_SPINS = 100;
    int spins = 0;
    
    while (spins++ < MAX_SPINS) {
        State state = buffer_[index].state.load(std::memory_order_relaxed);
        if (state == State::Available) {
            State expected = State::Available;
            if (buffer_[index].state.compare_exchange_strong(
                expected, 
                State::Acquired,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            {
                return buffer_[index].packet;
            }
        }
        _mm_pause(); // CPU 暂停指令减少能耗
    }
    return nullptr; // 回退到等待路径
}
```

#### 2. 批量处理
```cpp
// 获取多个可用 packet
std::vector<Packet*> acquirePackets(size_t count) {
    std::vector<Packet*> result;
    result.reserve(count);
    
    size_t acquired = 0;
    while (acquired < count) {
        Packet* p = manager_.acquirePacket();
        result.push_back(p);
        acquired++;
    }
    return result;
}
```

#### 3. 超时控制
```cpp
// 带超时的等待
Packet* waitForPacketWithTimeout(size_t index, int timeout_ms) {
    buffer_[index].waiters++;
    
    std::unique_lock<std::mutex> lock(wait_mutex_);
    if (!wait_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        return buffer_[index].state.load(std::memory_order_acquire) == State::Available;
    })) {
        // 超时处理
        buffer_[index].waiters--;
        return nullptr;
    }
    
    // 尝试获取
    State expected = State::Available;
    if (!buffer_[index].state.compare_exchange_strong(
        expected, 
        State::Acquired,
        std::memory_order_acquire,
        std::memory_order_relaxed))
    {
        buffer_[index].waiters--;
        return nullptr;
    }
    
    buffer_[index].waiters--;
    return buffer_[index].packet;
}
```

#### 4. NUMA 优化
```cpp
// NUMA 感知的 Packet 分配
class NUMAPacketManager : public PacketManager {
public:
    NUMAPacketManager(int numa_node) {
        // 在指定 NUMA 节点分配内存
        packets_ = static_cast<Packet*>(numa_alloc_onnode(
            sizeof(Packet) * BUFFER_SIZE, 
            numa_node));
        
        // 初始化状态数组
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            new (&buffer_[i]) PacketItem{
                .packet = &packets_[i],
                .state = State::Available
            };
        }
    }
    
    ~NUMAPacketManager() {
        numa_free(packets_, sizeof(Packet) * BUFFER_SIZE);
    }

private:
    Packet* packets_;
};
```

## 关键优势

1. **零动态内存分配**：
   - 所有对象预先分配在栈上
   - 运行时无内存分配开销

2. **高效状态管理**：
   - 原子操作替代锁
   - 状态转换最小化

3. **无争用设计**：
   - 每个 Packet 独立状态
   - 减少缓存行竞争

4. **优雅的慢路径**：
   - 条件变量等待避免忙等
   - 按需唤醒减少上下文切换

5. **线性扩展性**：
   - 处理能力随核心数扩展
   - 无中心瓶颈点

这个实现确保了上游能够安全地重用 packet 对象，同时高效地感知下游处理完成状态，适用于高性能网络处理、金融交易等场景。*/

/*
为了确保上游在循环使用预先分配的 `packet` 数组时，能够感知下游已经处理完每个 `packet`，并且在下次使用相同索引的 `packet` 时，下游已经完成处理，可以采用 **阻塞队列** 的方式管理 `packet` 的可用性。以下是具体实现方案：

---

### **核心设计思路**
1. **预分配资源**：上游预先分配 `packet` 数组（例如 `packet[1000]`）。
2. **阻塞队列管理索引**：使用一个线程安全的队列管理 `packet` 的可用索引。
3. **同步机制**：
   - 上游从队列中取出索引，使用对应的 `packet` 并传递给下游。
   - 下游处理完 `packet` 后，将索引放回队列，通知上游可以继续使用该 `packet`。
4. **线程安全**：通过互斥锁和条件变量实现队列的线程安全操作。

---

### **代码实现**

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <chrono>

// 模拟 packet 结构体
struct Packet {
    int index;
    std::string data;
};

// 线程安全的阻塞队列
class BlockingQueue {
public:
    void push(int index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(index);
        m_cond.notify_one();  // 通知等待的线程
    }

    int pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty(); });
        int index = m_queue.front();
        m_queue.pop();
        return index;
    }

private:
    std::queue<int> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
};

// 上游线程函数
void upstream(BlockingQueue& available_indices, std::vector<Packet>& packets) {
    while (true) {
        int index = available_indices.pop();  // 等待可用的 packet 索引
        Packet& packet = packets[index];
        std::cout << "[Upstream] Using packet " << index << " with data: " << packet.data << std::endl;

        // 模拟传递给下游（实际中可能通过其他方式传递）
        // 此处假设下游处理完成后会将索引放回队列
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 模拟耗时
    }
}

// 下游线程函数
void downstream(BlockingQueue& available_indices, std::vector<Packet>& packets) {
    while (true) {
        // 模拟处理 packet（实际中可能从其他地方获取 packet 索引）
        int index = available_indices.pop();  // 假设下游从队列中获取索引
        Packet& packet = packets[index];
        std::cout << "[Downstream] Processing packet " << index << " with data: " << packet.data << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 模拟处理耗时

        // 处理完成后，将索引放回队列，表示该 packet 可以被上游再次使用
        available_indices.push(index);
    }
}

int main() {
    const int NUM_PACKETS = 1000;
    std::vector<Packet> packets(NUM_PACKETS);
    for (int i = 0; i < NUM_PACKETS; ++i) {
        packets[i] = {i, "Sample Data " + std::to_string(i)};
    }

    BlockingQueue available_indices;
    // 预先将所有 packet 索引放入队列
    for (int i = 0; i < NUM_PACKETS; ++i) {
        available_indices.push(i);
    }

    // 启动上游和下游线程
    std::thread upstream_thread(upstream, std::ref(available_indices), std::ref(packets));
    std::thread downstream_thread(downstream, std::ref(available_indices), std::ref(packets));

    upstream_thread.join();
    downstream_thread.join();

    return 0;
}
```

---

### **关键点说明**

1. **阻塞队列**：
   - 队列中存储的是 `packet` 的索引（0~999），而不是 `packet` 本身。
   - 上游通过 `pop()` 从队列中获取可用索引，确保该 `packet` 已被下游处理完毕。
   - 下游处理完 `packet` 后，通过 `push()` 将索引放回队列，通知上游可以再次使用该 `packet`。

2. **线程安全**：
   - `BlockingQueue` 使用 `std::mutex` 和 `std::condition_variable` 实现线程安全。
   - `pop()` 方法在队列为空时阻塞，等待 `push()` 的通知。

3. **循环使用**：
   - 上游和下游通过队列的索引管理机制，确保每个 `packet` 在被重新使用前已经处理完毕。
   - 无需为每个 `packet` 单独维护状态，简化了同步逻辑。

---

### **扩展与优化**

1. **性能优化**：
   - 如果下游处理速度较慢，可以增加队列的容量或调整 `packet` 数量。
   - 使用无锁队列（如基于原子操作的队列）进一步提高性能。

2. **错误处理**：
   - 添加超时机制，防止线程因等待队列而永久阻塞。
   - 捕获异常并进行日志记录，便于调试。

3. **动态调整**：
   - 支持运行时动态调整 `packet` 数量，适应不同负载场景。

4. **分布式系统**：
   - 在分布式系统中，可以使用共享内存或网络队列（如 ZeroMQ）替代本地队列。

---

### **总结**

通过 **阻塞队列** 管理 `packet` 的可用性，上游和下游可以高效协作，确保每个 `packet` 在被重新使用前已经处理完毕。这种设计简单、线程安全，并且能够灵活应对不同场景的需求。*/