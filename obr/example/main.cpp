// cli/reconstruct_tool.cc
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <getopt.h>

#include "reconstructor.h"

void printUsage(const std::string& progName) {
    std::cerr << "Usage: " << progName << " --orders <orders_file> --trades <trades_file> --type <market_type>" << std::endl;
    std::cerr << "Market types: main_board, gem" << std::endl;
}


int main(int argc, char* argv[]) {
    std::string ordersFile;
    std::string tradesFile;
    orderbook::MarketType marketType = orderbook::MarketType::UNKNOWN;
    
    // 使用getopt长选项解析
    static struct option longOptions[] = {
        {"orders", required_argument, nullptr, 'o'},
        {"trades", required_argument, nullptr, 't'},
        {"type", required_argument, nullptr, 'm'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    
    int optionIndex = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "o:t:m:h", longOptions, &optionIndex)) != -1) {
        switch (c) {
            case 'o':
                ordersFile = optarg;
                break;
            case 't':
                tradesFile = optarg;
                break;
            case 'm':
                if (std::string(optarg) == "main_board") {
                    marketType = orderbook::MarketType::MAIN_BOARD;
                } else if (std::string(optarg) == "gem") {
                    marketType = orderbook::MarketType::GEM;
                } else {
                    std::cerr << "Invalid market type: " << optarg << std::endl;
                    printUsage(argv[0]);
                    return 1;
                }
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }
    
    // 检查必要参数
    if (ordersFile.empty() || tradesFile.empty() || marketType == orderbook::MarketType::UNKNOWN) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 创建订单簿重建器
    auto reconstructor = orderbook::createReconstructor(marketType);
    if (!reconstructor) {
        std::cerr << "Failed to create reconstructor for specified market type" << std::endl;
        return 1;
    }
    
    reconstructor->processOrder(orderbook::Order{});
    reconstructor->processTrade(orderbook::Trade{});
    
    return 0;
}