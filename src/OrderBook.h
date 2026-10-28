#pragma once
#include "Order.h"
#include <map>
#include <deque>
#include <cstdint>
#include <iostream>
#include <unordered_map>

// ids start at 1, so 0 is never a real order
constexpr uint64_t INVALID_ORDER_ID = 0;

class OrderBook {
public:
    uint64_t add_order(Side side, double price, int quantity);
    uint64_t add_market_order(Side side, int quantity);
    bool cancel_order(uint64_t id);
    void print_book(std::ostream& out = std::cout) const;

private:
    uint64_t next_id_ = 1;

    std::map<double, std::deque<Order>, std::greater<double>> bids_;
    std::map<double, std::deque<Order>> asks_;

    std::unordered_map<uint64_t, Order*> order_lookup_;

};
