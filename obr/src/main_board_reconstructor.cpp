#include "main_board_reconstructor.h"
#include <iostream>


namespace orderbook
{
MainBoardReconstructor::MainBoardReconstructor() {

}

MainBoardReconstructor::~MainBoardReconstructor() {

}

void MainBoardReconstructor::processOrder(Order order) {
    std::cout << "Processing order: " << order.order_id << " at price: " << order.price << std::endl;
}

void MainBoardReconstructor::processTrade(Trade trade) {
    std::cout << "Processing trade: " << trade.trade_id << " at price: " << trade.price << std::endl;
}

}