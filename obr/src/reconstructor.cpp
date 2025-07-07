#include "reconstructor.h"

#include "main_board_reconstructor.h"

namespace orderbook {
std::unique_ptr<OrderBookReconstructor> createReconstructor(MarketType type) {
  switch (type) {
    case MarketType::MAIN_BOARD:
      return std::make_unique<MainBoardReconstructor>();
    default:
      return nullptr;  // 或者抛出异常
  }
}

}  // namespace orderbook
