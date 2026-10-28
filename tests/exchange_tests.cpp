#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "Exchange.h"
#include "OrderBook.h"

static std::string book_str(const OrderBook& book) {
    std::ostringstream out;
    book.print_book(out);
    return out.str();
}

static std::string book_str(const Exchange& exchange, const std::string& instrument) {
    std::ostringstream out;
    exchange.print_book(instrument, out);
    return out.str();
}

// plain description of a command, so the same list can go to an OrderBook and an Exchange
struct Step {
    CommandType type;
    Side side;
    double price;
    int quantity;
    uint64_t id;
};

// fixed seed so every run gets the same commands. prices are close together so lots of things match
static std::vector<Step> random_steps(unsigned seed, int count) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> kind(0, 9);
    std::uniform_int_distribution<int> side(0, 1);
    std::uniform_int_distribution<int> price(95, 105);
    std::uniform_int_distribution<int> quantity(1, 20);

    std::vector<Step> steps;
    uint64_t adds = 0;
    for (int i = 0; i < count; ++i) {
        int k = kind(rng);
        Side s = side(rng) == 0 ? Side::BUY : Side::SELL;

        if (k < 6 || adds == 0) {
            double p = price(rng);
            int q = quantity(rng);
            steps.push_back({CommandType::ADD_LIMIT, s, p, q, INVALID_ORDER_ID});
            ++adds;
        } else if (k < 8) {
            int q = quantity(rng);
            steps.push_back({CommandType::ADD_MARKET, s, 0.0, q, INVALID_ORDER_ID});
            ++adds;
        } else {
            // any id handed out so far. might be resting, filled or already cancelled
            std::uniform_int_distribution<uint64_t> pick(1, adds);
            steps.push_back({CommandType::CANCEL, s, 0.0, 0, pick(rng)});
        }
    }
    return steps;
}

struct Results {
    std::vector<uint64_t> ids;
    std::vector<bool> cancels;
};

// single threaded reference
static Results apply_to_book(OrderBook& book, const std::vector<Step>& steps) {
    Results results;
    for (const Step& step : steps) {
        switch (step.type) {
        case CommandType::ADD_LIMIT:
            results.ids.push_back(book.add_order(step.side, step.price, step.quantity));
            break;
        case CommandType::ADD_MARKET:
            results.ids.push_back(book.add_market_order(step.side, step.quantity));
            break;
        case CommandType::CANCEL:
            results.cancels.push_back(book.cancel_order(step.id));
            break;
        }
    }
    return results;
}

struct Pending {
    std::vector<std::future<uint64_t>> ids;
    std::vector<std::future<bool>> cancels;
};

static void submit_step(Exchange& exchange, const std::string& instrument, const Step& step, Pending& pending) {
    switch (step.type) {
    case CommandType::ADD_LIMIT:
        pending.ids.push_back(exchange.submit_limit_order(instrument, step.side, step.price, step.quantity));
        break;
    case CommandType::ADD_MARKET:
        pending.ids.push_back(exchange.submit_market_order(instrument, step.side, step.quantity));
        break;
    case CommandType::CANCEL:
        pending.cancels.push_back(exchange.submit_cancel(instrument, step.id));
        break;
    }
}

static Results collect(Pending& pending) {
    Results results;
    for (auto& f : pending.ids) {
        results.ids.push_back(f.get());
    }
    for (auto& f : pending.cancels) {
        results.cancels.push_back(f.get());
    }
    return results;
}

TEST(ExchangeTest, InstrumentsDontInteract) {
    Exchange exchange({"AAPL", "MSFT"});

    // these would trade if they were in the same book
    auto aapl_id = exchange.submit_limit_order("AAPL", Side::BUY, 100.0, 10);
    auto msft_id = exchange.submit_limit_order("MSFT", Side::SELL, 100.0, 7);
    exchange.drain();

    EXPECT_EQ(book_str(exchange, "AAPL"), "Asks:\nBids:\n 100 x 10 \n");
    EXPECT_EQ(book_str(exchange, "MSFT"), "Asks:\n 100 x 7 \nBids:\n");

    // ids are per instrument, so both books hand out 1
    uint64_t aapl = aapl_id.get();
    uint64_t msft = msft_id.get();
    EXPECT_EQ(aapl, 1u);
    EXPECT_EQ(msft, 1u);

    // cancelling AAPL order 1 leaves MSFT order 1 alone
    auto cancelled = exchange.submit_cancel("AAPL", aapl);
    exchange.drain();
    EXPECT_TRUE(cancelled.get());
    EXPECT_EQ(book_str(exchange, "AAPL"), "Asks:\nBids:\n");
    EXPECT_EQ(book_str(exchange, "MSFT"), "Asks:\n 100 x 7 \nBids:\n");
}

TEST(ExchangeTest, MatchesSingleThreadedBook) {
    const std::vector<std::string> names = {"AAPL", "MSFT", "GOOG", "TSLA"};
    const int steps_per_instrument = 5000;

    std::vector<std::vector<Step>> steps;
    for (size_t i = 0; i < names.size(); ++i) {
        steps.push_back(random_steps(42 + i, steps_per_instrument));
    }

    Exchange exchange(names);
    std::vector<Pending> pending(names.size());

    // round robin so all the workers are busy at once
    for (int s = 0; s < steps_per_instrument; ++s) {
        for (size_t i = 0; i < names.size(); ++i) {
            submit_step(exchange, names[i], steps[i][s], pending[i]);
        }
    }
    exchange.drain();

    for (size_t i = 0; i < names.size(); ++i) {
        OrderBook reference;
        Results expected = apply_to_book(reference, steps[i]);
        Results actual = collect(pending[i]);

        EXPECT_EQ(actual.ids, expected.ids) << names[i];
        EXPECT_EQ(actual.cancels, expected.cancels) << names[i];
        EXPECT_EQ(book_str(exchange, names[i]), book_str(reference)) << names[i];
    }
}

TEST(ExchangeTest, ConcurrentProducersOnDifferentInstruments) {
    const std::vector<std::string> names = {"AAPL", "MSFT", "GOOG", "TSLA", "AMZN", "NVDA"};
    const int steps_per_instrument = 5000;

    std::vector<std::vector<Step>> steps;
    for (size_t i = 0; i < names.size(); ++i) {
        steps.push_back(random_steps(100 + i, steps_per_instrument));
    }

    Exchange exchange(names);
    std::vector<Pending> pending(names.size());

    // one producer thread per instrument, all submitting at the same time
    std::vector<std::thread> producers;
    for (size_t i = 0; i < names.size(); ++i) {
        producers.emplace_back([&, i] {
            for (const Step& step : steps[i]) {
                submit_step(exchange, names[i], step, pending[i]);
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    exchange.drain();

    for (size_t i = 0; i < names.size(); ++i) {
        OrderBook reference;
        Results expected = apply_to_book(reference, steps[i]);
        Results actual = collect(pending[i]);

        EXPECT_EQ(actual.ids, expected.ids) << names[i];
        EXPECT_EQ(actual.cancels, expected.cancels) << names[i];
        EXPECT_EQ(book_str(exchange, names[i]), book_str(reference)) << names[i];
    }
}

TEST(ExchangeTest, DestroyWithQueuedWorkShutsDownCleanly) {
    const int count = 20000;
    std::vector<std::future<uint64_t>> ids;

    {
        Exchange exchange({"AAPL", "MSFT"});
        for (int i = 0; i < count; ++i) {
            ids.push_back(exchange.submit_limit_order("AAPL", Side::BUY, 100.0 - (i % 10), 1));
        }
        // no drain, the destructor runs with work still in the queue
    }

    // destructor joined the workers, so every command was applied and every future is ready
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(ids[i].wait_for(std::chrono::seconds(0)), std::future_status::ready);
        EXPECT_EQ(ids[i].get(), static_cast<uint64_t>(i + 1));
    }
}

TEST(ExchangeTest, UnknownInstrumentThrows) {
    Exchange exchange({"AAPL"});

    EXPECT_THROW(exchange.submit_limit_order("NOPE", Side::BUY, 100.0, 1), std::invalid_argument);
    EXPECT_THROW(exchange.submit_market_order("NOPE", Side::BUY, 1), std::invalid_argument);
    EXPECT_THROW(exchange.submit_cancel("NOPE", 1), std::invalid_argument);

    std::ostringstream out;
    EXPECT_THROW(exchange.print_book("NOPE", out), std::invalid_argument);

    // nothing got queued for the real instrument either
    exchange.drain();
    EXPECT_EQ(book_str(exchange, "AAPL"), "Asks:\nBids:\n");
}

TEST(ExchangeTest, DuplicateInstrumentThrows) {
    EXPECT_THROW(Exchange({"AAPL", "AAPL"}), std::invalid_argument);
}
