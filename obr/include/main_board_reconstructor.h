/**
 * @file main_board_reconstructor.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-07-07
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once
#include "reconstructor.h"
#include <map>
namespace orderbook {

class MainBoardReconstructor : public OrderBookReconstructor {
 public:
  MainBoardReconstructor();
  ~MainBoardReconstructor() override;

  /**
   * @brief process order
   * what can I say
   * @param order 
   */
  virtual void processOrder(Order order) override;

  /**
   * @brief trade process
   * 
   * @param trade from file
   */
  virtual void processTrade(Trade trade) override;
private:
  /**
   * @brief order book map
   * 
   */
  std::map<std::string, Order> order_book_;
  /**
   * @brief trade vector
   * 
   */
  std::vector<Trade> trades_;
};

}  // namespace orderbook