#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace orderbook {
enum class MarketType {
  UNKNOWN,
  MAIN_BOARD,  // 主板
  GEM,         // 创业板
};

enum class OrderType {
  LIMIT,
  MARKET,
  IOC,
  FOK,
};

struct Order {
  uint64_t timestamp;
  std::string order_id;
  double price;
  uint32_t quantity;
  bool is_buy;     // true for bid, false for ask
  OrderType type;  // LIMIT, MARKET, etc.
};

struct Trade {
  uint64_t timestamp;
  std::string trade_id;
  double price;
  uint32_t quantity;
  bool aggressive_side;  // true if buyer-initiated
};
}  // namespace orderbook
