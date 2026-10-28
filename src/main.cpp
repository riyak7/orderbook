#include <iostream>
#include "OrderBook.h"

int main() {
    OrderBook book;

    uint64_t id1 = book.add_order(Side::BUY, 100.0, 10);
    book.add_order(Side::SELL, 99.0, 5);
    book.add_order(Side::SELL, 101.0, 5);
    book.cancel_order(id1);
    book.add_order(Side::SELL, 100.0, 5);

    std::cout << "-- after limit order tests --\n";
    book.print_book();

    book.add_market_order(Side::BUY, 8);

    std::cout << "-- after market buy of 8 --\n";
    book.print_book();

    book.add_market_order(Side::BUY, 100);

    std::cout << "-- after market buy of 100 (book was nearly empty) --\n";
    book.print_book();

    return 0;
}
