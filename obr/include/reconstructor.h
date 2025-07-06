#pragma once

#include <string>
#include <memory>
#include "types.h"

namespace orderbook {



class OrderBookReconstructor {
public:
    virtual ~OrderBookReconstructor() = default;
    
    // 处理一个委托订单
    virtual void processOrder(Order order) = 0;
    
    // 处理一笔交易
    virtual void processTrade(Trade trade) = 0;
    
};

// 工厂函数：创建特定市场的订单簿重建器
std::unique_ptr<OrderBookReconstructor> createReconstructor(MarketType type);

}  // namespace orderbook