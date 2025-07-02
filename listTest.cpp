#include <unordered_map>
#include <list>
#include <cstdint>
#include <iostream>

using namespace std;

class Elem {
public:
    uint64_t applSeqNo;
    uint32_t price;
    uint32_t qty;
};

class A {
public:
    unordered_map<uint32_t, list<Elem>> priceMap;
    unordered_map<uint64_t, list<Elem>::iterator> applSeqNoMap;
    unordered_map<uint32_t, uint64_t> priceQtyMap;
    void printInfo() {
        // This function can be used to print the internal state for debugging purposes
        for (const auto& pricePair : priceMap) {
            std::cout << "Price: " << pricePair.first << " Size: " << pricePair.second.size() << "\n";
        }
        for (const auto& seqPair : applSeqNoMap) {
            std::cout << "ApplSeqNo: " << seqPair.first << "\n";
        }
        for (const auto& priceQtyPair : priceQtyMap) {
            std::cout << "Price: " << priceQtyPair.first << " Qty: " << priceQtyPair.second << "\n";
        }
        std::cout << "------------------------\n";
    }
    void insertElem(const Elem& elem) {
        priceMap[elem.price].push_back(elem);
        
        auto it = --priceMap[elem.price].end();
        auto nit = std::prev(priceMap[elem.price].end());
        if (it != nit) {
            std::cout << "not same\n";
        }
        applSeqNoMap[elem.applSeqNo] = it;

        priceQtyMap[elem.price] += elem.qty;
    }

    void cancelElem(const Elem& elem) {
        auto it = applSeqNoMap.find(elem.applSeqNo);
        if (it != applSeqNoMap.end()) {
            auto priceIt = priceMap.find(elem.price);
            if (priceIt != priceMap.end()) {
                priceIt->second.erase(it->second);
                if (priceIt->second.empty()) {
                    priceMap.erase(priceIt);
                }
            }

            applSeqNoMap.erase(it);

            priceQtyMap[elem.price] -= elem.qty;
            if (priceQtyMap[elem.price] == 0) {
                priceQtyMap.erase(elem.price);
            }
        }
    }
    

};

int main() {
    A a;
    Elem elem1 = {1, 100, 10};
    Elem elem2 = {2, 100, 20};
    Elem elem3 = {3, 200, 30};

    a.insertElem(elem1);
    a.insertElem(elem2);
    a.insertElem(elem3);

    a.printInfo();
    a.cancelElem(elem1);
    a.cancelElem(elem2);

    a.printInfo();
    return 0;
}