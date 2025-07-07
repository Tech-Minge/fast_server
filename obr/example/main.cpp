/**
 * @file main.cpp
 * @author yang (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-07-07
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include <boost/program_options.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "reconstructor.h"

template <typename T, size_t N>
bool compareArrays(const T (&a)[N], const T (&b)[N]) {
  for (size_t i = 0; i < N; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

#define COMPARE_MEMBER(name) \
  if (name != rhs.name) return false;

struct Trade {
  uint64_t id;       // Trade ID
  uint64_t orderId;  // Order ID
  uint64_t price;    // Trade price
  uint64_t volume;   // Trade volume
  uint32_t bp[5];
  uint32_t ap[5];
  bool operator==(const Trade& rhs) const {
    COMPARE_MEMBER(id);
    COMPARE_MEMBER(orderId);
    COMPARE_MEMBER(price);
    COMPARE_MEMBER(volume);
    if (!compareArrays(bp, rhs.bp)) return false;
    if (!compareArrays(ap, rhs.ap)) return false;
    return true;
  }
};

int main(int argc, char* argv[]) {
  // 定义存储参数的变量
  std::string order_file;
  std::string trade_file;
  unsigned int secid;
  std::string type;

  namespace po = boost::program_options;

  // 1. 创建选项描述器
  po::options_description desc("Allowed options");
  desc.add_options()("order_file,o",
                     po::value<std::string>(&order_file)->required(),
                     "Order file path (required)")(
      "trade_file,t", po::value<std::string>(&trade_file)->required(),
      "Trade file path (required)")("secid,s",
                                    po::value<unsigned int>(&secid)->required(),
                                    "Security ID (required)")(
      "type", po::value<std::string>(&type)->required(), "Type (cyb or zb)");

  // 2. 解析命令行参数
  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);  // 检查必须参数并更新变量[1,4](@ref)

    // 3. 验证type参数
    if (type != "cyb" && type != "zb") {
      throw std::invalid_argument("Invalid type: must be 'cyb' or 'zb'");
    }

  } catch (const po::required_option& e) {
    std::cerr << "Error: Missing required parameter - " << e.what() << "\n\n";
    std::cerr << desc << std::endl;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n\n";
    std::cerr << desc << std::endl;
    return 1;
  }

  // 4. 使用解析后的参数（示例输出）
  std::cout << "Order File: " << order_file << "\n"
            << "Trade File: " << trade_file << "\n"
            << "SecID: " << secid << "\n"
            << "Type: " << type << std::endl;
  orderbook::MarketType marketType = orderbook::MarketType::UNKNOWN;
  if (type == "cyb") {
    marketType = orderbook::MarketType::GEM;  // 创业板
  } else if (type == "zb") {
    marketType = orderbook::MarketType::MAIN_BOARD;  // 主板
  } else {
    std::cerr << "Invalid type specified" << std::endl;
  }
  // 创建订单簿重建器
  auto reconstructor = orderbook::createReconstructor(marketType);
  if (!reconstructor) {
    std::cerr << "Failed to create reconstructor for specified market type"
              << std::endl;
    return 1;
  }

  reconstructor->processOrder(orderbook::Order{});
  reconstructor->processTrade(orderbook::Trade{});

  return 0;
}