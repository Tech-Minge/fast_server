#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <memory>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// ====================== 基础组件 ======================
// 线程安全的无锁队列 (SPSC - 单生产者单消费者)
template <typename T, size_t Size>
class LockFreeQueue {
public:
    bool push(T&& item) {
        size_t next_tail = (tail + 1) % Size;
        if (next_tail == head.load(std::memory_order_acquire)) 
            return false;
        
        ring[tail] = std::move(item);
        tail = next_tail;
        return true;
    }

    bool pop(T& item) {
        size_t current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail)
            return false;
        
        item = std::move(ring[current_head]);
        head = (current_head + 1) % Size;
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) size_t tail{0};
    T ring[Size];
};

// 线程池
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) : running(true) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (running) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { 
                            return !tasks.empty() || !running; 
                        });
                        
                        if (!running && tasks.empty()) 
                            return;
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template <typename F>
    void enqueue(F&& f) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        running = false;
        condition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> running;
};

// ====================== 行情数据模块 ======================
struct MarketData {
    uint64_t timestamp;
    std::string symbol;
    double bid_price;
    double ask_price;
    uint32_t bid_size;
    uint32_t ask_size;

    std::string to_string() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4)
            << "[MD] " << symbol << " | "
            << "Bid: " << bid_size << "@" << bid_price << " | "
            << "Ask: " << ask_size << "@" << ask_price;
        return oss.str();
    }
};

class MarketDataReceiver {
public:
    MarketDataReceiver(LockFreeQueue<std::string, 1024>& raw_queue) 
        : raw_queue(raw_queue), running(false) {}
    
    void start() {
        running = true;
        receiver_thread = std::thread([this] { run(); });
    }
    
    void stop() {
        running = false;
        if (receiver_thread.joinable()) {
            receiver_thread.join();
        }
    }
    
private:
    void run() {
        // 模拟UDP接收行情数据
        while (running) {
            // 实际应用中这里会从网络接收数据
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            // 生成模拟行情数据
            static std::atomic<uint64_t> seq(0);
            std::string raw_data = "MD|" + std::to_string(++seq) + "|AAPL|150.25|100|150.30|200";
            
            // 推送到原始数据队列
            while (!raw_queue.push(std::move(raw_data))) {
                std::this_thread::yield();
            }
        }
    }
    
    LockFreeQueue<std::string, 1024>& raw_queue;
    std::atomic<bool> running;
    std::thread receiver_thread;
};

// ====================== 解析模块 ======================
class MarketDataParser {
public:
    MarketDataParser(LockFreeQueue<std::string, 1024>& raw_queue,
                     LockFreeQueue<MarketData, 1024>& parsed_queue)
        : raw_queue(raw_queue), parsed_queue(parsed_queue) {}
    
    void parse(const std::string& raw_data) {
        // 简化解析逻辑
        MarketData data;
        char symbol[16];
        char temp[128];
        
        if (sscanf(raw_data.c_str(), "MD|%lu|%15[^|]|%lf|%u|%lf|%u",
                   &data.timestamp, symbol, 
                   &data.bid_price, &data.bid_size,
                   &data.ask_price, &data.ask_size) == 6) {
            data.symbol = symbol;
            
            // 推送到解析后队列
            while (!parsed_queue.push(std::move(data))) {
                std::this_thread::yield();
            }
        }
    }
    
private:
    LockFreeQueue<std::string, 1024>& raw_queue;
    LockFreeQueue<MarketData, 1024>& parsed_queue;
};

// ====================== 算法模块 ======================
enum class OrderActionType {
    NEW_ORDER,
    CANCEL_ORDER
};

struct OrderAction {
    OrderActionType type;
    std::string order_id;
    std::string symbol;
    char side;  // 'B' for buy, 'S' for sell
    uint32_t quantity;
    double price;
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4)
            << "[ACTION] " << (type == OrderActionType::NEW_ORDER ? "NEW" : "CANCEL")
            << " | " << order_id << " | " << symbol << " | " << side << " | "
            << quantity << " @ " << price;
        return oss.str();
    }
};

class AlgorithmEngine {
public:
    AlgorithmEngine(LockFreeQueue<MarketData, 1024>& parsed_queue,
                    LockFreeQueue<OrderAction, 1024>& action_queue)
        : parsed_queue(parsed_queue), action_queue(action_queue) {}
    
    void process(const MarketData& data) {
        // 简化算法逻辑 - 实际中这里会有复杂的交易策略
        static std::atomic<uint64_t> order_id(0);
        
        // 示例策略: 当买一价和卖一价差距足够大时下单
        double spread = data.ask_price - data.bid_price;
        if (spread > 0.05 && data.bid_size > 1000 && data.ask_size > 1000) {
            OrderAction action;
            action.type = OrderActionType::NEW_ORDER;
            action.order_id = "ORD" + std::to_string(++order_id);
            action.symbol = data.symbol;
            action.side = 'B';
            action.quantity = 100;
            action.price = data.bid_price + 0.01;
            
            // 推送到交易队列
            while (!action_queue.push(std::move(action))) {
                std::this_thread::yield();
            }
        }
    }
    
private:
    LockFreeQueue<MarketData, 1024>& parsed_queue;
    LockFreeQueue<OrderAction, 1024>& action_queue;
};

// ====================== 交易模块 ======================
class FixTrader {
public:
    FixTrader(LockFreeQueue<OrderAction, 1024>& action_queue)
        : action_queue(action_queue), running(false) {}
    
    void start() {
        running = true;
        trader_thread = std::thread([this] { run(); });
    }
    
    void stop() {
        running = false;
        if (trader_thread.joinable()) {
            trader_thread.join();
        }
    }
    
private:
    void run() {
        // 模拟FIX连接
        while (running) {
            OrderAction action;
            if (action_queue.pop(action)) {
                // 实际应用中这里会发送FIX协议消息
                std::cout << "[FIX] Sending: " << action.to_string() << std::endl;
                
                // 模拟网络延迟
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    LockFreeQueue<OrderAction, 1024>& action_queue;
    std::atomic<bool> running;
    std::thread trader_thread;
};

// ====================== 系统监控 ======================
class SystemMonitor {
public:
    void log_md_receive() { md_received++; }
    void log_md_parsed() { md_parsed++; }
    void log_action_generated() { actions_generated++; }
    void log_action_sent() { actions_sent++; }
    
    void start() {
        running = true;
        monitor_thread = std::thread([this] {
            while (running) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                uint64_t received = md_received.exchange(0);
                uint64_t parsed = md_parsed.exchange(0);
                uint64_t generated = actions_generated.exchange(0);
                uint64_t sent = actions_sent.exchange(0);
                
                std::cout << "\n[STATS] "
                          << "MD Recv: " << received << "/s | "
                          << "MD Parsed: " << parsed << "/s | "
                          << "Actions Gen: " << generated << "/s | "
                          << "Actions Sent: " << sent << "/s\n";
            }
        });
    }
    
    void stop() {
        running = false;
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
    }
    
private:
    std::atomic<uint64_t> md_received{0};
    std::atomic<uint64_t> md_parsed{0};
    std::atomic<uint64_t> actions_generated{0};
    std::atomic<uint64_t> actions_sent{0};
    std::atomic<bool> running{false};
    std::thread monitor_thread;
};

// ====================== 主系统 ======================
class TradingSystem {
public:
    TradingSystem() 
        : parser_pool(4), 
          algo_pool(2),
          md_receiver(raw_md_queue),
          trader(action_queue) {}
    
    void start() {
        // 启动监控
        monitor.start();
        
        // 启动行情接收
        md_receiver.start();
        
        // 启动交易模块
        trader.start();
        
        // 启动解析线程
        for (int i = 0; i < 4; i++) {
            parser_pool.enqueue([this] {
                std::string raw_data;
                MarketDataParser parser(raw_md_queue, parsed_md_queue);
                
                while (true) {
                    if (raw_md_queue.pop(raw_data)) {
                        parser.parse(raw_data);
                        monitor.log_md_receive();
                    } else {
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                }
            });
        }
        
        // 启动算法线程
        for (int i = 0; i < 2; i++) {
            algo_pool.enqueue([this] {
                MarketData data;
                AlgorithmEngine engine(parsed_md_queue, action_queue);
                
                while (true) {
                    if (parsed_md_queue.pop(data)) {
                        engine.process(data);
                        monitor.log_md_parsed();
                        monitor.log_action_generated();
                    } else {
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                }
            });
        }
    }
    
    void stop() {
        md_receiver.stop();
        trader.stop();
        monitor.stop();
    }

private:
    // 线程池
    ThreadPool parser_pool;
    ThreadPool algo_pool;
    
    // 队列
    LockFreeQueue<std::string, 1024> raw_md_queue;    // MD接收 -> 解析
    LockFreeQueue<MarketData, 1024> parsed_md_queue;  // 解析 -> 算法
    LockFreeQueue<OrderAction, 1024> action_queue;    // 算法 -> 交易
    
    // 模块
    MarketDataReceiver md_receiver;
    FixTrader trader;
    SystemMonitor monitor;
};

// ====================== 主函数 ======================
int main() {
    TradingSystem system;
    
    std::cout << "Starting trading system...\n";
    system.start();
    
    std::cout << "System running. Press Enter to stop...\n";
    std::cin.get();
    
    std::cout << "Stopping trading system...\n";
    system.stop();
    
    std::cout << "System stopped.\n";
    return 0;
}