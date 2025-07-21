#ifndef TRADING_SYSTEM_H
#define TRADING_SYSTEM_H

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <any>

// 前向声明
class MDReceiver;
class AlgoModule;
class Trader;
class ThreadPool;

// 数据结构定义
struct MarketData {
    std::string symbol;
    double bid_price;
    int bid_size;
    double ask_price;
    int ask_size;
    uint64_t timestamp;
};

struct OrderRequest {
    enum class Type { NEW, CANCEL } type;
    std::string order_id;
    std::string symbol;
    enum class Side { BUY, SELL } side;
    double price;
    int quantity;
};

struct OrderResponse {
    std::string order_id;
    std::string exec_id;
    enum class Status { NEW, FILLED, PARTIALLY_FILLED, CANCELLED, REJECTED } status;
    double price;
    int filled_quantity;
    std::string reason;
};

// 线程安全的消息队列
template<typename T>
class ConcurrentQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> running_{true};

public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            queue_.push(item);
            cond_.notify_one();
        }
    }

    bool pop(T& item, int timeout_ms = -1) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (timeout_ms >= 0) {
            if (!cond_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                              [this] { return !queue_.empty() || !running_; })) {
                return false; // 超时
            }
        } else {
            cond_.wait(lock, [this] { return !queue_.empty() || !running_; });
        }
        
        if (!running_) return false;
        
        item = queue_.front();
        queue_.pop();
        return true;
    }

    void stop() {
        running_ = false;
        cond_.notify_all();
    }

    bool is_running() const {
        return running_;
    }
};

// 行情接收器
class MDReceiver {
private:
    std::string address_;
    int port_;
    std::atomic<bool> running_{false};
    std::thread receiver_thread_;
    ConcurrentQueue<MarketData>* md_queue_;
    
    void receive_loop();

public:
    MDReceiver(const std::string& address, int port, ConcurrentQueue<MarketData>* md_queue);
    ~MDReceiver();
    
    void start();
    void stop();
};

// 算法模块
class AlgoModule {
private:
    ConcurrentQueue<MarketData>* md_queue_;
    ConcurrentQueue<OrderRequest>* order_queue_;
    std::atomic<bool> running_{false};
    std::thread processing_thread_;
    std::unordered_map<std::string, MarketData> last_prices_; // 维护最新行情
    std::mutex prices_mutex_;
    
    void process_loop();
    void handle_market_data(const MarketData& md);
    void generate_order(const std::string& symbol, OrderRequest::Side side, 
                       double price, int quantity);
    void cancel_order(const std::string& order_id, const std::string& symbol);

public:
    AlgoModule(ConcurrentQueue<MarketData>* md_queue, 
              ConcurrentQueue<OrderRequest>* order_queue);
    ~AlgoModule();
    
    void start();
    void stop();
    // 可以添加设置算法参数的方法
};

// 交易执行模块
class Trader {
private:
    std::string fix_server_;
    int fix_port_;
    std::string sender_comp_id_;
    std::string target_comp_id_;
    ConcurrentQueue<OrderRequest>* order_queue_;
    ConcurrentQueue<OrderResponse>* response_queue_;
    std::atomic<bool> running_{false};
    std::thread trading_thread_;
    void* fix_session_; // 实际实现中会是FIX会话对象
    
    void trading_loop();
    std::string send_new_order(const OrderRequest& req);
    bool send_cancel_order(const OrderRequest& req);
    void handle_fix_response();

public:
    Trader(const std::string& server, int port, 
          const std::string& sender_id, const std::string& target_id,
          ConcurrentQueue<OrderRequest>* order_queue,
          ConcurrentQueue<OrderResponse>* response_queue);
    ~Trader();
    
    void start();
    void stop();
    bool connect();
    void disconnect();
};

// 系统控制器
class TradingSystem {
private:
    std::unique_ptr<ConcurrentQueue<MarketData>> md_queue_;
    std::unique_ptr<ConcurrentQueue<OrderRequest>> order_queue_;
    std::unique_ptr<ConcurrentQueue<OrderResponse>> response_queue_;
    std::unique_ptr<MDReceiver> md_receiver_;
    std::unique_ptr<AlgoModule> algo_module_;
    std::unique_ptr<Trader> trader_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::atomic<bool> running_{false};
    
public:
    TradingSystem(const std::string& md_address, int md_port,
                 const std::string& fix_server, int fix_port,
                 const std::string& sender_id, const std::string& target_id,
                 size_t thread_pool_size = std::thread::hardware_concurrency());
    
    void start();
    void stop();
    void run();
};

#endif // TRADING_SYSTEM_H
