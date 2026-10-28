#include "OrderBook.h"
#include <iostream>
#include <algorithm>

uint64_t OrderBook::add_order(Side side, double price, int quantity) {
    if (quantity <= 0 || price <= 0.0) {
        return INVALID_ORDER_ID;
    }

    Order incoming{next_id_++, side, price, quantity};
    
    if (side == Side::BUY) {
        while (incoming.quantity > 0 && !asks_.empty()) {
            auto best_ask_it = asks_.begin();
            double best_ask_price = best_ask_it->first;

            if (best_ask_price >  incoming.price) {
                break;
            }

            std::deque<Order>& level = best_ask_it->second;
            Order& resting = level.front();

            if (resting.cancelled) {
                level.pop_front();
                if (level.empty()) {
                    asks_.erase(best_ask_it);
                }
                continue;
            }


            int traded_qty = std::min(incoming.quantity, resting.quantity);

            incoming.quantity -= traded_qty;
            resting.quantity -= traded_qty;

            if (resting.quantity == 0) {
                order_lookup_.erase(resting.id);
                level.pop_front();
            }

            if (level.empty()) {
                asks_.erase(best_ask_it);
            }
        }
    } else {
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto best_bid_it = bids_.begin();
            double best_bid_price = best_bid_it->first;

            if (best_bid_price < incoming.price) {
                break;
            }

            std::deque<Order>& level = best_bid_it->second;
            Order& resting = level.front();

            if (resting.cancelled) {
                level.pop_front();
                if (level.empty()) {
                    bids_.erase(best_bid_it);
                }
                continue;
            }


            int traded_qty = std::min(incoming.quantity, resting.quantity);

            incoming.quantity -= traded_qty;
            resting.quantity -= traded_qty;

            if (resting.quantity == 0){
                order_lookup_.erase(resting.id);
                level.pop_front();
            }

            if (level.empty()) {
                bids_.erase(best_bid_it);
            }

        }
    }



    if (incoming.quantity > 0) {
        if (side == Side::BUY) {
            bids_[incoming.price].push_back(incoming);
            order_lookup_[incoming.id] = &bids_[incoming.price].back();
        } else {
            asks_[incoming.price].push_back(incoming);
            order_lookup_[incoming.id] = &asks_[incoming.price].back();
        }
    }

    return incoming.id;
}

uint64_t OrderBook::add_market_order(Side side, int quantity) {
    if (quantity <= 0) {
        return INVALID_ORDER_ID;
    }

    Order incoming{next_id_++, side, 0.0, quantity};

    if (side == Side::BUY) {
        while (incoming.quantity > 0 && !asks_.empty()) {
            auto best_ask_it = asks_.begin();
            std::deque<Order>& level = best_ask_it->second;
            Order& resting = level.front();

            if (resting.cancelled) {
                level.pop_front();
                if (level.empty()) {
                    asks_.erase(best_ask_it);
                }
                continue;
            }

            int traded_qty = std::min(incoming.quantity, resting.quantity);
            incoming.quantity -= traded_qty;
            resting.quantity -= traded_qty;

            if (resting.quantity == 0) {
                order_lookup_.erase(resting.id);
                level.pop_front();
            }
            if (level.empty()) {
                asks_.erase(best_ask_it);
            }
        }
    } else {
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto best_bid_it = bids_.begin();
            std::deque<Order>& level = best_bid_it->second;
            Order& resting = level.front();

            if (resting.cancelled) {
                level.pop_front();
                if (level.empty()) {
                    bids_.erase(best_bid_it);
                }
                continue;
            }

            int traded_qty = std::min(incoming.quantity, resting.quantity);
            incoming.quantity -= traded_qty;
            resting.quantity -= traded_qty;

            if (resting.quantity == 0) {
                order_lookup_.erase(resting.id);
                level.pop_front();
            }
            if (level.empty()) {
                bids_.erase(best_bid_it);
            }
        }
    }

    return incoming.id;

}




void OrderBook::print_book(std::ostream& out) const {
    
    auto is_live = [](const Order& order) { return !order.cancelled; };

    out << "Asks:\n";
    for (const auto& [price, orders] : asks_) {
        // level might only have cancelled orders left in it (lazy cancel)
        if (std::none_of(orders.begin(), orders.end(), is_live)) {
            continue;
        }
        out << " " << price << " x ";
        for (const auto& order : orders) {
            if (order.cancelled) {
                continue;
            }
            out << order.quantity << " ";
        }
        out << "\n";
    }

    out << "Bids:\n";
    for (const auto& [price, orders] : bids_) {
        if (std::none_of(orders.begin(), orders.end(), is_live)) {
            continue;
        }
        out << " " << price << " x ";
        for (const auto& order : orders) {
            if (order.cancelled) {
                continue;
            }
            out << order.quantity << " ";
        }
        out << "\n";
    }

    
}

bool OrderBook::cancel_order(uint64_t id) {
    auto it = order_lookup_.find(id);
    if (it == order_lookup_.end()) {
        return false;
    }

    it->second->cancelled = true;
    order_lookup_.erase(it);
    return true;
}


