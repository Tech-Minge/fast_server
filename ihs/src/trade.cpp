#include "trading_system.h"
#include "fix42_protocol.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <random>

// 线程池实现
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    ConcurrentQueue<std::function<void()>> task_queue_;
    
public:
    ThreadPool(size_t num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this]() {
                while (task_queue_.is_running()) {
                    std::function<void()> task;
                    if (task_queue_.pop(task)) {
                        try {
                            task();
                        } catch (const std::exception& e) {
                            std::cerr << "Task error: " << e.what() << std::endl;
                        }
                    }
                }
            });
        }
    }
    
    ~ThreadPool() {
        task_queue_.stop();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    template<typename F, typename... Args>
    void enqueue(F&& f, Args&&... args) {
        task_queue_.push(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }
};

// MDReceiver 实现
MDReceiver::MDReceiver(const std::string& address, int port, ConcurrentQueue<MarketData>* md_queue)
    : address_(address), port_(port), md_queue_(md_queue) {}

MDReceiver::~MDReceiver() {
    stop();
}

void MDReceiver::start() {
    running_ = true;
    receiver_thread_ = std::thread(&MDReceiver::receive_loop, this);
}

void MDReceiver::stop() {
    running_ = false;
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
}

void MDReceiver::receive_loop() {
    std::cout << "Starting MD receiver on " << address_ << ":" << port_ << std::endl;
    
    // 实际实现中这里会是真实的网络接收逻辑
    // 这里用模拟数据代替
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> price_dist(150.0, 160.0);
    std::uniform_int_distribution<> size_dist(10, 100);
    
    while (running_) {
        // 模拟接收行情数据
        MarketData md;
        md.symbol = "AAPL";
        md.bid_price = price_dist(gen);
        md.bid_size = size_dist(gen);
        md.ask_price = md.bid_price + 0.01;
        md.ask_size = size_dist(gen);
        md.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // 发送到队列
        md_queue_->push(md);
        
        // 模拟行情频率
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "MD receiver stopped" << std::endl;
}

// AlgoModule 实现
AlgoModule::AlgoModule(ConcurrentQueue<MarketData>* md_queue, 
                     ConcurrentQueue<OrderRequest>* order_queue)
    : md_queue_(md_queue), order_queue_(order_queue) {}

AlgoModule::~AlgoModule() {
    stop();
}

void AlgoModule::start() {
    running_ = true;
    processing_thread_ = std::thread(&AlgoModule::process_loop, this);
}

void AlgoModule::stop() {
    running_ = false;
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

void AlgoModule::process_loop() {
    std::cout << "Starting algo module" << std::endl;
    
    while (running_) {
        MarketData md;
        if (md_queue_->pop(md, 100)) { // 100ms超时
            handle_market_data(md);
        }
    }
    
    std::cout << "Algo module stopped" << std::endl;
}

void AlgoModule::handle_market_data(const MarketData& md) {
    // 保存最新行情
    {
        std::lock_guard<std::mutex> lock(prices_mutex_);
        last_prices_[md.symbol] = md;
    }
    
    // 简单的交易算法示例：当买一价低于某个阈值时买入
    static std::atomic<int> order_counter = 0;
    static std::string active_order_id;
    
    if (active_order_id.empty() && md.bid_price < 155.0) {
        // 生成新订单
        int counter = ++order_counter;
        active_order_id = "ALGO_ORD_" + std::to_string(counter);
        generate_order(md.symbol, OrderRequest::Side::BUY, md.bid_price, 100);
        std::cout << "Algo generated buy order: " << active_order_id << std::endl;
    }
    else if (!active_order_id.empty() && md.ask_price > md.bid_price + 0.5) {
        // 达到盈利目标，取消订单
        cancel_order(active_order_id, md.symbol);
        active_order_id.clear();
        std::cout << "Algo cancelled order: " << active_order_id << std::endl;
    }
}

void AlgoModule::generate_order(const std::string& symbol, OrderRequest::Side side,
                              double price, int quantity) {
    OrderRequest req;
    req.type = OrderRequest::Type::NEW;
    req.order_id = "ALGO_ORD_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    req.symbol = symbol;
    req.side = side;
    req.price = price;
    req.quantity = quantity;
    
    order_queue_->push(req);
}

void AlgoModule::cancel_order(const std::string& order_id, const std::string& symbol) {
    OrderRequest req;
    req.type = OrderRequest::Type::CANCEL;
    req.order_id = order_id;
    req.symbol = symbol;
    
    order_queue_->push(req);
}

// Trader 实现
Trader::Trader(const std::string& server, int port,
             const std::string& sender_id, const std::string& target_id,
             ConcurrentQueue<OrderRequest>* order_queue,
             ConcurrentQueue<OrderResponse>* response_queue)
    : fix_server_(server), fix_port_(port),
      sender_comp_id_(sender_id), target_comp_id_(target_id),
      order_queue_(order_queue), response_queue_(response_queue),
      fix_session_(nullptr) {}

Trader::~Trader() {
    stop();
    disconnect();
}

void Trader::start() {
    running_ = true;
    if (connect()) {
        trading_thread_ = std::thread(&Trader::trading_loop, this);
    } else {
        std::cerr << "Failed to start trader - connection failed" << std::endl;
    }
}

void Trader::stop() {
    running_ = false;
    if (trading_thread_.joinable()) {
        trading_thread_.join();
    }
}

bool Trader::connect() {
    // 实际实现中会初始化FIX会话
    fix_session_ = new FIX42Protocol(sender_comp_id_, target_comp_id_, 30);
    
    // 设置FIX回调
    auto* fix = static_cast<FIX42Protocol*>(fix_session_);
    fix->setOnExecutionReportHandler([this](const std::string& order_id,
                                           FIX42Protocol::ExecType exec_type,
                                           const std::string& exec_id,
                                           double price, int quantity,
                                           const std::string& reason) {
        OrderResponse resp;
        resp.order_id = order_id;
        resp.exec_id = exec_id;
        resp.price = price;
        resp.filled_quantity = quantity;
        
        // 转换执行类型
        switch (exec_type) {
            case FIX42Protocol::ExecType::NEW:
                resp.status = OrderResponse::Status::NEW;
                break;
            case FIX42Protocol::ExecType::FILLED:
                resp.status = OrderResponse::Status::FILLED;
                break;
            case FIX42Protocol::ExecType::PARTIALLY_FILLED:
                resp.status = OrderResponse::Status::PARTIALLY_FILLED;
                break;
            case FIX42Protocol::ExecType::CANCELLED:
                resp.status = OrderResponse::Status::CANCELLED;
                break;
            case FIX42Protocol::ExecType::REJECTED:
                resp.status = OrderResponse::Status::REJECTED;
                resp.reason = reason;
                break;
        }
        
        response_queue_->push(resp);
    });
    
    return static_cast<FIX42Protocol*>(fix_session_)->connect(fix_server_, fix_port_);
}

void Trader::disconnect() {
    if (fix_session_) {
        auto* fix = static_cast<FIX42Protocol*>(fix_session_);
        fix->logout("Normal exit");
        fix->disconnect();
        delete fix;
        fix_session_ = nullptr;
    }
}

void Trader::trading_loop() {
    std::cout << "Starting trader module" << std::endl;
    
    auto* fix = static_cast<FIX42Protocol*>(fix_session_);
    if (!fix) {
        std::cerr << "FIX session not initialized" << std::endl;
        return;
    }
    
    // 发送登录请求
    fix->logon([](bool success, const std::string& reason) {
        if (success) {
            std::cout << "FIX logon successful" << std::endl;
        } else {
            std::cerr << "FIX logon failed: " << reason << std::endl;
        }
    });
    
    while (running_) {
        OrderRequest req;
        if (order_queue_->pop(req, 100)) { // 100ms超时
            if (req.type == OrderRequest::Type::NEW) {
                FIX42Protocol::Side side = (req.side == OrderRequest::Side::BUY) 
                    ? FIX42Protocol::Side::BUY 
                    : FIX42Protocol::Side::SELL;
                
                std::string order_id = fix->sendLimitOrder(
                    req.symbol, side, req.price, req.quantity);
                std::cout << "Sent new order: " << order_id << std::endl;
            }
            else if (req.type == OrderRequest::Type::CANCEL) {
                FIX42Protocol::Side side = (req.side == OrderRequest::Side::BUY) 
                    ? FIX42Protocol::Side::BUY 
                    : FIX42Protocol::Side::SELL;
                
                bool success = fix->cancelOrder(req.order_id, req.symbol, side, req.price);
                std::cout << "Sent cancel request for order " << req.order_id 
                          << ", success: " << std::boolalpha << success << std::endl;
            }
        }
        
        // 处理FIX响应
        handle_fix_response();
    }
    
    std::cout << "Trader module stopped" << std::endl;
}

void Trader::handle_fix_response() {
    // 在实际实现中，这里会处理FIX协议的响应消息
    // 由于我们在connect中已经设置了回调，这里可以留空
}

// TradingSystem 实现
TradingSystem::TradingSystem(const std::string& md_address, int md_port,
                           const std::string& fix_server, int fix_port,
                           const std::string& sender_id, const std::string& target_id,
                           size_t thread_pool_size) {
    // 创建消息队列
    md_queue_ = std::make_unique<ConcurrentQueue<MarketData>>();
    order_queue_ = std::make_unique<ConcurrentQueue<OrderRequest>>();
    response_queue_ = std::make_unique<ConcurrentQueue<OrderResponse>>();
    
    // 创建线程池
    thread_pool_ = std::make_unique<ThreadPool>(thread_pool_size);
    
    // 创建模块
    md_receiver_ = std::make_unique<MDReceiver>(md_address, md_port, md_queue_.get());
    algo_module_ = std::make_unique<AlgoModule>(md_queue_.get(), order_queue_.get());
    trader_ = std::make_unique<Trader>(fix_server, fix_port, sender_id, target_id,
                                      order_queue_.get(), response_queue_.get());
}

void TradingSystem::start() {
    running_ = true;
    md_receiver_->start();
    algo_module_->start();
    trader_->start();
}

void TradingSystem::stop() {
    running_ = false;
    
    // 按相反顺序停止模块
    trader_->stop();
    algo_module_->stop();
    md_receiver_->stop();
    
    // 停止队列
    md_queue_->stop();
    order_queue_->stop();
    response_queue_->stop();
}

void TradingSystem::run() {
    start();
    
    // 主循环，等待用户输入退出
    std::cout << "Trading system running. Press 'q' to quit." << std::endl;
    while (running_) {
        char c;
        std::cin >> c;
        if (c == 'q' || c == 'Q') {
            running_ = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    stop();
}
