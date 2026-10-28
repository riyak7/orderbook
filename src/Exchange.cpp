#include "Exchange.h"
#include <stdexcept>
#include <utility>

void CommandQueue::push(Command command) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push(std::move(command));
        ++unfinished_;
    }
    work_ready_.notify_one();
}

bool CommandQueue::pop(Command& command) {
    std::unique_lock<std::mutex> lock(mutex_);
    // sleeps until there's work or we're told to stop. the predicate handles spurious wakeups
    work_ready_.wait(lock, [this] { return !commands_.empty() || stopped_; });

    // stopped, but finish whatever is left first
    if (commands_.empty()) {
        return false;
    }

    command = std::move(commands_.front());
    commands_.pop();
    return true;
}

void CommandQueue::mark_done() {
    std::lock_guard<std::mutex> lock(mutex_);
    --unfinished_;
    if (unfinished_ == 0) {
        all_done_.notify_all();
    }
}

void CommandQueue::wait_until_done() {
    std::unique_lock<std::mutex> lock(mutex_);
    all_done_.wait(lock, [this] { return unfinished_ == 0; });
}

void CommandQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    work_ready_.notify_one();
}

Exchange::Exchange(const std::vector<std::string>& instruments) {
    for (const std::string& name : instruments) {
        bool inserted = workers_.emplace(name, std::make_unique<Worker>()).second;
        if (!inserted) {
            throw std::invalid_argument("duplicate instrument: " + name);
        }
    }

    // start threads only after every name is checked, so a throw above can't leave threads running
    for (auto& entry : workers_) {
        Worker& worker = *entry.second;
        worker.thread = std::thread([&worker] { run(worker); });
    }
}

Exchange::~Exchange() {
    // tell everyone to stop first so they wind down in parallel, then wait for each
    for (auto& entry : workers_) {
        entry.second->queue.stop();
    }
    for (auto& entry : workers_) {
        entry.second->thread.join();
    }
}

std::future<uint64_t> Exchange::submit_limit_order(const std::string& instrument, Side side, double price, int quantity) {
    Worker& worker = find_worker(instrument);

    Command command;
    command.type = CommandType::ADD_LIMIT;
    command.side = side;
    command.price = price;
    command.quantity = quantity;

    // grab the future before the promise is moved into the queue
    std::future<uint64_t> result = command.new_id.get_future();
    worker.queue.push(std::move(command));
    return result;
}

std::future<uint64_t> Exchange::submit_market_order(const std::string& instrument, Side side, int quantity) {
    Worker& worker = find_worker(instrument);

    Command command;
    command.type = CommandType::ADD_MARKET;
    command.side = side;
    command.quantity = quantity;

    std::future<uint64_t> result = command.new_id.get_future();
    worker.queue.push(std::move(command));
    return result;
}

std::future<bool> Exchange::submit_cancel(const std::string& instrument, uint64_t id) {
    Worker& worker = find_worker(instrument);

    Command command;
    command.type = CommandType::CANCEL;
    command.id = id;

    std::future<bool> result = command.cancelled.get_future();
    worker.queue.push(std::move(command));
    return result;
}

void Exchange::drain() {
    for (auto& entry : workers_) {
        entry.second->queue.wait_until_done();
    }
}

void Exchange::print_book(const std::string& instrument, std::ostream& out) const {
    find_worker(instrument).book.print_book(out);
}

Worker& Exchange::find_worker(const std::string& instrument) const {
    auto it = workers_.find(instrument);
    if (it == workers_.end()) {
        throw std::invalid_argument("unknown instrument: " + instrument);
    }
    return *it->second;
}

// the loop each worker thread runs. the only place its book gets touched while running
void Exchange::run(Worker& worker) {
    while (true) {
        Command command;
        if (!worker.queue.pop(command)) {
            return;
        }

        switch (command.type) {
        case CommandType::ADD_LIMIT:
            command.new_id.set_value(worker.book.add_order(command.side, command.price, command.quantity));
            break;
        case CommandType::ADD_MARKET:
            command.new_id.set_value(worker.book.add_market_order(command.side, command.quantity));
            break;
        case CommandType::CANCEL:
            command.cancelled.set_value(worker.book.cancel_order(command.id));
            break;
        }

        worker.queue.mark_done();
    }
}
