#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <deque>
#include <stack>
#include <thread>
#include <list>
#include <unordered_set>
#include <fstream>
#include <algorithm>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <cassert>


using namespace std;
using namespace std::placeholders;


struct Order {
    uint64_t id;
    int64_t price;
    uint64_t quantity;
    int8_t side; // 1 for buy, 2 for sell
};


struct OptimPriceInfo {
    int64_t dealPrice;
    int64_t expectedDealQuantity;
    int64_t buyAboveQuantity;
    int64_t askBelowQuantity;
    int64_t buyDealPriceLeftQuantity;
    int64_t askDealPriceRightQuantity;

    bool operator>(const OptimPriceInfo& other) const {
        uint64_t quantityDiff = std::abs(buyAboveQuantity - askBelowQuantity);
        uint64_t otherQuantityDiff = std::abs(other.buyAboveQuantity - other.askBelowQuantity);
        return expectedDealQuantity > other.expectedDealQuantity ||
               (expectedDealQuantity == other.expectedDealQuantity && quantityDiff < otherQuantityDiff);
    }
};

struct OrderBookStatus {
    uint64_t nts;
    int64_t cvl;
    int64_t cto;
    int64_t lpr;

    int bp[5];
    int ap[5];
    int bs[5];
    int as[5];

    void printInfo() const {
        cout << "NTS: " << nts << ", CVL: " << cvl << ", CTO: " << cto << ", LPR: " << lpr << "\n";
        cout << "Bid Prices: ";
        for (int i = 0; i < 5; ++i) {
            cout << bp[i] << " ";
        }
        cout << "\nAsk Prices: ";
        for (int i = 0; i < 5; ++i) {
            cout << ap[i] << " ";
        }

        cout << "\nBid Sizes: ";
        for (int i = 0; i < 5; ++i) {
            cout << bs[i] << " ";
        }
        cout << "\nAsk Sizes: ";
        for (int i = 0; i < 5; ++i) {
            cout << as[i] << " ";
        }
        cout << "\n";
        cout << "----------------------------\n\n";
    }
};

struct ConfigurableComparator {
    bool is_ascending;
    explicit ConfigurableComparator(bool asc) : is_ascending(asc) {}

    bool operator()(uint64_t a, uint64_t b) const {
        return is_ascending ? (a < b) : (a > b);
    }
};

struct PriceLevel {
    uint64_t quantity;
    list<Order> orders;
};

using OrderBookMap = std::map<uint64_t, PriceLevel, ConfigurableComparator>;




class OrderIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Order;
    using difference_type = std::ptrdiff_t;
    using pointer = Order*;
    using reference = Order&;

    OrderIterator() = default;
    
    explicit OrderIterator(OrderBookMap& price_map, bool is_end = false)
        : map_iter(is_end ? price_map.end() : price_map.begin()),
          map_end(price_map.end()) {
        if (map_iter != map_end) {
            list_iter = map_iter->second.orders.begin();
            advance_to_valid();
        }
    }

    reference operator*() {
        return *list_iter;
    }
    
    pointer operator->() {
        return &(*list_iter);
    }

    OrderIterator& operator++() {
        if (map_iter == map_end) return *this;
        
        ++list_iter;
        advance_to_valid();
        return *this;
    }

    OrderIterator operator++(int) {
        OrderIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    OrderBookMap::iterator getMapIter() const {
        return map_iter;
    }

    bool operator==(const OrderIterator& other) const {
        if (map_iter != other.map_iter) return false;
        if (map_iter == map_end) return true;
        return list_iter == other.list_iter;
    }
    
    bool operator!=(const OrderIterator& other) const {
        return !(*this == other);
    }



private:
    // 辅助函数：前进到有效位置
    void advance_to_valid() {
        // 跳过空列表或已到列表末尾的情况
        while (map_iter != map_end) {
            if (list_iter != map_iter->second.orders.end()) {
                return;
            }
            
            ++map_iter;
            if (map_iter != map_end) {
                list_iter = map_iter->second.orders.begin();
            }
        }
    }

    OrderBookMap::iterator map_iter;
    OrderBookMap::iterator map_end;
    std::list<Order>::iterator list_iter;
};

OrderIterator begin(OrderBookMap& price_map) {
    return OrderIterator(price_map);
}

OrderIterator end(OrderBookMap& price_map) {
    return OrderIterator(price_map, true);
}




class OrderBook {
public:

    OrderBookStatus flushStatus() {
        if (bidPriceMaps.empty() || askPriceMaps.empty()) {
            return OrderBookStatus{};
        } else if (bidPriceMaps.begin()->first < askPriceMaps.begin()->first) {
            return OrderBookStatus{};
        }

        OrderBookStatus status{};
        map<uint64_t, uint64_t, std::greater<>> bidPrefixSum;
        map<uint64_t, uint64_t, std::less<>> askPrefixSum;
        uint64_t totalBidQuantity = 0;
        uint64_t totalAskQuantity = 0;
        for (const auto& [price, level] : bidPriceMaps) {
            bidPrefixSum[price] = totalBidQuantity;
            totalBidQuantity += level.quantity;
        }
        bidPrefixSum[0] = totalBidQuantity; // Add a zero price level for easier calculations

        for (const auto& [price, level] : askPriceMaps) {
            askPrefixSum[price] = totalAskQuantity;
            totalAskQuantity += level.quantity;
        }
        askPrefixSum[INT64_MAX] = totalAskQuantity; // Add a max price level for easier calculations

        OptimPriceInfo optimPriceInfo;
        optimPriceInfo.expectedDealQuantity = -1;

        auto bidIt = bidPrefixSum.begin();
        int64_t minBidPrice = askPrefixSum.begin()->first;
        while (bidIt != bidPrefixSum.end()) {
            if (bidIt->first < minBidPrice) {
                break;
            }
            int64_t curPrice = bidIt->first;
            int64_t currBuy = bidPriceMaps[curPrice].quantity;
            int64_t buyAboveQuantity = bidIt->second;


            auto askIt = askPrefixSum.lower_bound(curPrice);
            assert(askIt != askPrefixSum.end());
            int64_t currSell = askIt->first == curPrice ? askPriceMaps[curPrice].quantity : 0;
            int64_t askBelowQuantity = askIt->second;
            bool ok = true;
            if (buyAboveQuantity + currBuy > askBelowQuantity + currSell) {
                if (buyAboveQuantity > askBelowQuantity + currSell) {
                    // std::cout << "can't make deal at price " << curPrice << " buyAboveQuantity: " << buyAboveQuantity << " askBelowQuantity: " << askBelowQuantity << " currBuy: " << currBuy << " currSell: " << currSell << "\n";
                    ok = false;
                }
            } else if (buyAboveQuantity + currBuy < askBelowQuantity + currSell) {
                if (askBelowQuantity > buyAboveQuantity + currBuy) {
                    // std::cout << "can't make deal at price " << curPrice << " buyAboveQuantity: " << buyAboveQuantity << " askBelowQuantity: " << askBelowQuantity << " currBuy: " << currBuy << " currSell: " << currSell << "\n";
                    ok = false;
                }
            }

            if (!ok) {
                ++bidIt;
                continue;
            }

            int64_t expectedDealQuantity = std::min(buyAboveQuantity + currBuy, askBelowQuantity + currSell);
            OptimPriceInfo current{
                curPrice,
                expectedDealQuantity,
                buyAboveQuantity,
                askBelowQuantity,
                currBuy + buyAboveQuantity - expectedDealQuantity,
                currSell + askBelowQuantity - expectedDealQuantity
            };
            if (current > optimPriceInfo) {
                optimPriceInfo = current;
            }
            ++bidIt;
        }

        auto askIt = askPrefixSum.begin();
        int64_t maxAskPrice = bidPrefixSum.begin()->first;
        while (askIt != askPrefixSum.end()) {
            if (askIt->first > maxAskPrice) {
                break;
            }
            int64_t curPrice = askIt->first;
            int64_t currSell = askPriceMaps[curPrice].quantity;
            int64_t askBelowQuantity = askIt->second;

            auto bidIt = bidPrefixSum.lower_bound(curPrice);
            assert(bidIt != bidPrefixSum.end());
            int64_t currBuy = bidIt->first == curPrice ? bidPriceMaps[curPrice].quantity : 0;
            int64_t buyAboveQuantity = bidIt->second;
            bool ok = true;
            if (buyAboveQuantity + currBuy > askBelowQuantity + currSell) {
                if (buyAboveQuantity > askBelowQuantity + currSell) {
                    // std::cout << "can't make deal at price " << curPrice << " buyAboveQuantity: " << buyAboveQuantity << " askBelowQuantity: " << askBelowQuantity << " currBuy: " << currBuy << " currSell: " << currSell << "\n";
                    ok = false;
                }
            } else if (buyAboveQuantity + currBuy < askBelowQuantity + currSell) {
                if (askBelowQuantity > buyAboveQuantity + currBuy) {
                    // std::cout << "can't make deal at price " << curPrice << " buyAboveQuantity: " << buyAboveQuantity << " askBelowQuantity: " << askBelowQuantity << " currBuy: " << currBuy << " currSell: " << currSell << "\n";
                    ok = false;
                }
            }
            if (!ok) {
                ++askIt;
                continue;
            }
            int64_t expectedDealQuantity = std::min(buyAboveQuantity + currBuy, askBelowQuantity + currSell);
            OptimPriceInfo current{
                curPrice,
                expectedDealQuantity,
                buyAboveQuantity,
                askBelowQuantity,
                currBuy + buyAboveQuantity - expectedDealQuantity,
                currSell + askBelowQuantity - expectedDealQuantity
            };
            if (current > optimPriceInfo) {
                optimPriceInfo = current;
            }
            ++askIt;
        }

        status.lpr = optimPriceInfo.dealPrice;
        status.cvl = optimPriceInfo.expectedDealQuantity;

        status.cto = status.cvl * optimPriceInfo.dealPrice / 10000;
        
        int64_t tradeCount = 0;

        OrderIterator buyOrderIt = begin(bidPriceMaps);
        OrderIterator sellOrderIt = begin(askPriceMaps);
        Order buyOrder = *buyOrderIt;
        Order sellOrder = *sellOrderIt;
        

        while (1) {
            if (buyOrder.price < sellOrder.price) {
                break;
            }
            uint64_t tradeVolume = std::min(buyOrder.quantity, sellOrder.quantity);
            buyOrder.quantity -= tradeVolume;
            sellOrder.quantity -= tradeVolume;
            ++tradeCount;

            bool finished = false;
            if (buyOrder.quantity == 0) {
                ++buyOrderIt;
                if (buyOrderIt == end(bidPriceMaps)) {
                    finished = true;
                } else {
                    buyOrder = *buyOrderIt;
                }
            }
            if (sellOrder.quantity == 0) {
                ++sellOrderIt;
                if (sellOrderIt == end(askPriceMaps)) {    
                    finished = true;
                } else {
                    sellOrder = *sellOrderIt;
                }
            }
            if (finished) {
                break;
            }
        }


        status.nts = tradeCount;

        auto bidIter = buyOrderIt.getMapIter();
        auto askIter = sellOrderIt.getMapIter();
        int count = 4;
        while (bidIter != bidPriceMaps.end() && count >= 0) {
            status.bp[count] = bidIter->first;

            if (bidIter->first == optimPriceInfo.dealPrice) {
                status.bs[count] = optimPriceInfo.buyDealPriceLeftQuantity;
            } else {
                status.bs[count] = bidIter->second.quantity;
            }
            --count;
            ++bidIter;
        }

        count = 0;
        while (askIter != askPriceMaps.end() && count < 5) {
            status.ap[count] = askIter->first;
            if (askIter->first == optimPriceInfo.dealPrice) {
                status.as[count] = optimPriceInfo.askDealPriceRightQuantity;
            } else {
                status.as[count] = askIter->second.quantity;
            }
            ++count;
            ++askIter;
        }

        /*
        auto bidIter = bidPriceMaps.begin();
        auto askIter = askPriceMaps.begin();
        
        auto buyOrderIt = bidIter->second.orders.begin();
        auto sellOrderIt = askIter->second.orders.begin();
        int64_t tradeCount = 0;


        Order buyOrder = *buyOrderIt;
        Order sellOrder = *sellOrderIt;
        

        while (1) {
            if (buyOrder.price < sellOrder.price) {
                break;
            }
            uint64_t tradeVolume = std::min(buyOrder.quantity, sellOrder.quantity);
            buyOrder.quantity -= tradeVolume;
            sellOrder.quantity -= tradeVolume;
            ++tradeCount;

            bool finished = false;
            if (buyOrder.quantity == 0) {
                ++buyOrderIt;
                if (buyOrderIt == bidIter->second.orders.end()) {
                    ++bidIter;
                    if (bidIter == bidPriceMaps.end()) {
                        finished = true;
                    } else {
                        buyOrderIt = bidIter->second.orders.begin();
                        buyOrder = *buyOrderIt;
                    }
                } else {
                    buyOrder = *buyOrderIt;
                }
            }
            if (sellOrder.quantity == 0) {
                ++sellOrderIt;
                if (sellOrderIt == askIter->second.orders.end()) {
                    ++askIter;
                    if (askIter == askPriceMaps.end()) {    
                        finished = true;
                    } else {
                        sellOrderIt = askIter->second.orders.begin();
                        sellOrder = *sellOrderIt;
                    }
                } else {
                    sellOrder = *sellOrderIt;
                }
            }
            if (finished) {
                break;
            }
        }


        status.nts = tradeCount;

        int count = 4;
        while (bidIter != bidPriceMaps.end() && count >= 0) {
            status.bp[count] = bidIter->first;

            if (bidIter->first == optimPriceInfo.dealPrice) {
                status.bs[count] = optimPriceInfo.buyDealPriceLeftQuantity;
            } else {
                status.bs[count] = bidIter->second.quantity;
            }
            --count;
            ++bidIter;
        }

        count = 0;
        while (askIter != askPriceMaps.end() && count < 5) {
            status.ap[count] = askIter->first;
            if (askIter->first == optimPriceInfo.dealPrice) {
                status.as[count] = optimPriceInfo.askDealPriceRightQuantity;
            } else {
                status.as[count] = askIter->second.quantity;
            }
            ++count;
            ++askIter;
        }
        */

        return status;
    }

    

    void insertOrder(const Order& order) {
        if (order.side == 1) { // Buy order
            auto& priceLevel = bidPriceMaps[order.price];
            priceLevel.quantity += order.quantity;
            priceLevel.orders.push_back(order);
        } else if (order.side == 2) { // Sell order
            auto& priceLevel = askPriceMaps[order.price];
            priceLevel.quantity += order.quantity;
            priceLevel.orders.push_back(order);
        }

        auto obs = flushStatus();
        obs.printInfo();
    }

    void printOrderBook() const {
        cout << "Bid Price Levels:\n";
        for (const auto& [price, level] : bidPriceMaps) {
            cout << "Price: " << price << ", Quantity: " << level.quantity << "\n";
            for (const auto& order : level.orders) {
                cout << "  Order ID: " << order.id << ", Quantity: " << order.quantity << "\n";
            }
        }
        cout << "----------------------------\n";
        cout << "\nAsk Price Levels:\n";
        for (const auto& [price, level] : askPriceMaps) {
            cout << "Price: " << price << ", Quantity: " << level.quantity << "\n";
            for (const auto& order : level.orders) {
                cout << "  Order ID: " << order.id << ", Quantity: " << order.quantity << "\n";
            }
        }
    }
private:
    OrderBookMap askPriceMaps{ConfigurableComparator(true)};
    OrderBookMap bidPriceMaps{ConfigurableComparator(false)};

    // map<uint64_t, PriceLevel, std::greater<>> bidPriceMaps;
    // map<uint64_t, PriceLevel, std::less<>> askPriceMaps;

};
