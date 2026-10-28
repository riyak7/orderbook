#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include "OrderBook.h"
#include <cmath>
#include <limits>

// print_book output is the easiest way to see what's resting and in what order
static std::string book_str(const OrderBook& book) {
    std::ostringstream out;
    book.print_book(out);
    return out.str();
}

static const std::string EMPTY_BOOK = "Asks:\nBids:\n";

TEST(OrderBookTest, NewBookIsEmpty) {
    OrderBook book;
    EXPECT_EQ(book_str(book), EMPTY_BOOK);
}

TEST(OrderBookTest, NonCrossingOrdersRest) {
    OrderBook book;
    book.add_order(Side::BUY, 99.0, 10);
    book.add_order(Side::SELL, 101.0, 5);

    EXPECT_EQ(book_str(book), "Asks:\n 101 x 5 \nBids:\n 99 x 10 \n");
}

TEST(OrderBookTest, FullFill) {
    OrderBook book;
    uint64_t sell_id = book.add_order(Side::SELL, 100.0, 10);
    uint64_t buy_id = book.add_order(Side::BUY, 100.0, 10);

    EXPECT_EQ(book_str(book), EMPTY_BOOK);
    EXPECT_FALSE(book.cancel_order(sell_id));
    EXPECT_FALSE(book.cancel_order(buy_id));
}

TEST(OrderBookTest, PartialFillOfRestingOrder) {
    OrderBook book;
    uint64_t sell_id = book.add_order(Side::SELL, 100.0, 10);
    book.add_order(Side::BUY, 100.0, 4);

    EXPECT_EQ(book_str(book), "Asks:\n 100 x 6 \nBids:\n");
    EXPECT_TRUE(book.cancel_order(sell_id));
}

TEST(OrderBookTest, PartialFillOfIncomingOrderRestsTheRemainder) {
    OrderBook book;
    book.add_order(Side::SELL, 100.0, 4);
    uint64_t buy_id = book.add_order(Side::BUY, 100.0, 10);

    EXPECT_EQ(book_str(book), "Asks:\nBids:\n 100 x 6 \n");
    EXPECT_TRUE(book.cancel_order(buy_id));
}

TEST(OrderBookTest, BuyFillsAtTheRestingPrice) {
    OrderBook book;
    book.add_order(Side::SELL, 100.0, 5);
    book.add_order(Side::BUY, 105.0, 5);

    EXPECT_EQ(book_str(book), EMPTY_BOOK);
}

TEST(OrderBookTest, BuyFillsAcrossSeveralLevels) {
    OrderBook book;
    book.add_order(Side::SELL, 100.0, 5);
    book.add_order(Side::SELL, 101.0, 5);
    book.add_order(Side::SELL, 102.0, 5);

    book.add_order(Side::BUY, 102.0, 12);

    EXPECT_EQ(book_str(book), "Asks:\n 102 x 3 \nBids:\n");
}

TEST(OrderBookTest, SellFillsAcrossSeveralLevels) {
    OrderBook book;
    book.add_order(Side::BUY, 102.0, 5);
    book.add_order(Side::BUY, 101.0, 5);
    book.add_order(Side::BUY, 100.0, 5);

    book.add_order(Side::SELL, 100.0, 12);

    EXPECT_EQ(book_str(book), "Asks:\nBids:\n 100 x 3 \n");
}

TEST(OrderBookTest, MatchingStopsAtTheLimitPrice) {
    OrderBook book;
    book.add_order(Side::SELL, 100.0, 5);
    book.add_order(Side::SELL, 101.0, 5);
    book.add_order(Side::SELL, 102.0, 5);

    book.add_order(Side::BUY, 101.0, 12);

    // 102 is too expensive, so the last 2 rest as a bid at 101
    EXPECT_EQ(book_str(book), "Asks:\n 102 x 5 \nBids:\n 101 x 2 \n");
}

TEST(OrderBookTest, TimePriorityAtSamePrice) {
    OrderBook book;
    uint64_t first = book.add_order(Side::SELL, 100.0, 5);
    uint64_t second = book.add_order(Side::SELL, 100.0, 7);

    book.add_order(Side::BUY, 100.0, 3);
    EXPECT_EQ(book_str(book), "Asks:\n 100 x 2 7 \nBids:\n");

    book.add_order(Side::BUY, 100.0, 2);
    EXPECT_FALSE(book.cancel_order(first));
    EXPECT_TRUE(book.cancel_order(second));
}

TEST(OrderBookTest, CancelRestingOrder) {
    OrderBook book;
    uint64_t id = book.add_order(Side::BUY, 100.0, 10);

    EXPECT_TRUE(book.cancel_order(id));
    EXPECT_EQ(book_str(book), EMPTY_BOOK);
    EXPECT_FALSE(book.cancel_order(id));
}

TEST(OrderBookTest, CancelledOrderDoesNotMatch) {
    OrderBook book;
    uint64_t cheap = book.add_order(Side::SELL, 100.0, 5);
    book.add_order(Side::SELL, 101.0, 5);
    book.cancel_order(cheap);

    book.add_order(Side::BUY, 101.0, 5);

    EXPECT_EQ(book_str(book), EMPTY_BOOK);
}

TEST(OrderBookTest, PrintSkipsCancelledOrders) {
    OrderBook book;
    book.add_order(Side::SELL, 100.0, 5);
    uint64_t id = book.add_order(Side::SELL, 100.0, 7);
    uint64_t lone = book.add_order(Side::SELL, 101.0, 3);
    book.cancel_order(id);
    book.cancel_order(lone);

    EXPECT_EQ(book_str(book), "Asks:\n 100 x 5 \nBids:\n");
}

TEST(OrderBookTest, CancelAlreadyFilledOrder) {
    OrderBook book;
    uint64_t sell_id = book.add_order(Side::SELL, 100.0, 10);
    book.add_order(Side::BUY, 100.0, 10);

    // used to write through a dangling pointer
    EXPECT_FALSE(book.cancel_order(sell_id));

    book.add_order(Side::SELL, 100.0, 4);
    EXPECT_EQ(book_str(book), "Asks:\n 100 x 4 \nBids:\n");
}

TEST(OrderBookTest, CancelOrderFilledByMarketOrder) {
    OrderBook book;
    uint64_t sell_id = book.add_order(Side::SELL, 100.0, 10);
    book.add_market_order(Side::BUY, 10);

    EXPECT_FALSE(book.cancel_order(sell_id));
}

TEST(OrderBookTest, CancelUnknownId) {
    OrderBook book;
    book.add_order(Side::BUY, 100.0, 10);

    EXPECT_FALSE(book.cancel_order(12345));
    EXPECT_FALSE(book.cancel_order(INVALID_ORDER_ID));
    EXPECT_EQ(book_str(book), "Asks:\nBids:\n 100 x 10 \n");
}

TEST(OrderBookTest, MarketOrderIntoEmptyBook) {
    OrderBook book;
    uint64_t id = book.add_market_order(Side::BUY, 10);

    EXPECT_NE(id, INVALID_ORDER_ID);
    EXPECT_EQ(book_str(book), EMPTY_BOOK);
    EXPECT_FALSE(book.cancel_order(id));
}

TEST(OrderBookTest, MarketOrderWalksTheBook) {
    OrderBook book;
    book.add_order(Side::BUY, 101.0, 5);
    book.add_order(Side::BUY, 100.0, 5);

    book.add_market_order(Side::SELL, 7);

    EXPECT_EQ(book_str(book), "Asks:\nBids:\n 100 x 3 \n");
}

TEST(OrderBookTest, MarketOrderLargerThanBook) {
    OrderBook book;
    book.add_order(Side::SELL, 100.0, 5);
    book.add_order(Side::SELL, 101.0, 5);
    book.add_order(Side::BUY, 99.0, 8);

    uint64_t id = book.add_market_order(Side::BUY, 100);

    // whatever is left over gets dropped, it never rests as a bid
    EXPECT_EQ(book_str(book), "Asks:\nBids:\n 99 x 8 \n");
    EXPECT_FALSE(book.cancel_order(id));
}

TEST(OrderBookTest, RejectsBadQuantity) {
    OrderBook book;
    EXPECT_EQ(book.add_order(Side::BUY, 100.0, 0), INVALID_ORDER_ID);
    EXPECT_EQ(book.add_order(Side::BUY, 100.0, -5), INVALID_ORDER_ID);
    EXPECT_EQ(book.add_market_order(Side::SELL, 0), INVALID_ORDER_ID);
    EXPECT_EQ(book.add_market_order(Side::SELL, -5), INVALID_ORDER_ID);
    EXPECT_EQ(book_str(book), EMPTY_BOOK);
}

TEST(OrderBookTest, RejectsBadPrice) {
    OrderBook book;
    EXPECT_EQ(book.add_order(Side::BUY, 0.0, 10), INVALID_ORDER_ID);
    EXPECT_EQ(book.add_order(Side::SELL, -1.0, 10), INVALID_ORDER_ID);
    EXPECT_EQ(book_str(book), EMPTY_BOOK);
}

TEST(OrderBookTest, RejectedOrderDoesNotUseUpAnId) {
    OrderBook book;
    book.add_order(Side::BUY, 100.0, 0);
    EXPECT_EQ(book.add_order(Side::BUY, 100.0, 10), 1u);
}

TEST(OrderBookTest, RejectsNaNAndInfinitePrice) {
    OrderBook book;
    EXPECT_EQ(book.add_order(Side::BUY, std::nan(""), 10), INVALID_ORDER_ID);
    EXPECT_EQ(book.add_order(Side::SELL, std::numeric_limits<double>::infinity(), 10), INVALID_ORDER_ID);
    EXPECT_EQ(book_str(book), EMPTY_BOOK);
}