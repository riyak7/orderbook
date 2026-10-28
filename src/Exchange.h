#pragma once
#include "OrderBook.h"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class CommandType { ADD_LIMIT, ADD_MARKET, CANCEL };

// one request for a book. the worker fills in the promise that matches the type
struct Command {
    CommandType type = CommandType::ADD_LIMIT;
    Side side = Side::BUY;
    double price = 0.0;
    int quantity = 0;
    uint64_t id = INVALID_ORDER_ID;  // only used by cancel
    std::promise<uint64_t> new_id;   // adds: the id the book assigned
    std::promise<bool> cancelled;    // cancel: what cancel_order returned
};

// fifo of commands. any thread can push, only the worker pops
class CommandQueue {
public:
    void push(Command command);
    // waits for a command. returns false once stopped and empty
    bool pop(Command& command);
    // worker calls this after it has applied a popped command
    void mark_done();
    // waits until every pushed command has been popped and marked done
    void wait_until_done();
    void stop();

private:
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable all_done_;
    std::queue<Command> commands_;
    size_t unfinished_ = 0;
    bool stopped_ = false;
};

// everything for one instrument. only its own thread touches book
struct Worker {
    OrderBook book;
    CommandQueue queue;
    std::thread thread;
};

class Exchange {
public:
    explicit Exchange(const std::vector<std::string>& instruments);
    ~Exchange();

    // owns threads, so no copying
    Exchange(const Exchange&) = delete;
    Exchange& operator=(const Exchange&) = delete;

    std::future<uint64_t> submit_limit_order(const std::string& instrument, Side side, double price, int quantity);
    std::future<uint64_t> submit_market_order(const std::string& instrument, Side side, int quantity);
    std::future<bool> submit_cancel(const std::string& instrument, uint64_t id);

    // blocks until every command submitted so far has been applied
    void drain();
    // only safe after drain() with nothing else submitting
    void print_book(const std::string& instrument, std::ostream& out = std::cout) const;

private:
    Worker& find_worker(const std::string& instrument) const;
    static void run(Worker& worker);

    // filled in the constructor and never changed after, so lookups from many threads are fine
    std::unordered_map<std::string, std::unique_ptr<Worker>> workers_;
};
