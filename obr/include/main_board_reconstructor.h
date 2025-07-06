// include/orderbook_reconstructor/main_board_reconstructor.h
#pragma once
#include "reconstructor.h"

namespace orderbook {

class MainBoardReconstructor : public OrderBookReconstructor {
public:
    MainBoardReconstructor();
    ~MainBoardReconstructor() override;
    
    // 处理一个委托订单
    virtual void processOrder(Order order) override;
    
    // 处理一笔交易
    virtual void processTrade(Trade trade) override;
};

}  // namespace orderbook