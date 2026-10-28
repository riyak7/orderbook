#include <benchmark/benchmark.h>
#include <windows.h>
#include <psapi.h>
#include <vector>
#include "OrderBook.h"

static size_t current_memory_usage_bytes() {
    PROCESS_MEMORY_COUNTERS pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.WorkingSetSize;
}

static void BM_AddOrder_NoMatch(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto* book = new OrderBook();
        state.ResumeTiming();

        for (int i = 0; i < 1000; ++i) {
            benchmark::DoNotOptimize(book->add_order(Side::BUY, 1000.0 - i, 10));
        }

        state.PauseTiming();
        delete book;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_AddOrder_NoMatch);

static void BM_AddOrder_HeavyMatching(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto* book = new OrderBook();
        state.ResumeTiming();

        for (int i = 0; i < 1000; ++i) {
            Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
            benchmark::DoNotOptimize(book->add_order(side, 100.0, 10));
        }

        state.PauseTiming();
        delete book;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_AddOrder_HeavyMatching);

// times a batch of inserts instead of one, because PauseTiming/ResumeTiming
// cost a few hundred ns each and would swamp a single insert
static void BM_AddOrder_DeepBook(benchmark::State& state) {
    int depth = state.range(0);
    const int batch = 100;

    for (auto _ : state) {
        state.PauseTiming();
        auto* book = new OrderBook();
        for (int i = 0; i < depth; ++i) {
            book->add_order(Side::BUY, 100000.0 - i, 10);
        }
        state.ResumeTiming();

        // each one lands in a new level spread through the book
        for (int i = 0; i < batch; ++i) {
            double price = 100000.0 - (i * depth / batch) - 0.5;
            benchmark::DoNotOptimize(book->add_order(Side::BUY, price, 10));
        }

        state.PauseTiming();
        delete book;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * batch);
}
BENCHMARK(BM_AddOrder_DeepBook)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_CancelOrder(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto* book = new OrderBook();
        std::vector<uint64_t> ids;
        for (int i = 0; i < 1000; ++i) {
            ids.push_back(book->add_order(Side::BUY, 1000.0 - i, 10));
        }
        state.ResumeTiming();

        for (uint64_t id : ids) {
            benchmark::DoNotOptimize(book->cancel_order(id));
        }

        state.PauseTiming();
        delete book;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_CancelOrder);

static void BM_MemoryFootprint(benchmark::State& state) {
    int order_count = state.range(0);

    for (auto _ : state) {
        state.PauseTiming();

        size_t before = current_memory_usage_bytes();
        auto* book = new OrderBook();
        for (int i = 0; i < order_count; ++i) {
            book->add_order(Side::BUY, 100000.0 - i, 10);
        }
        size_t after = current_memory_usage_bytes();

        state.counters["BytesPerOrder"] = static_cast<double>(after - before) / order_count;

        delete book;
        state.ResumeTiming();
    }
}
// nothing is timed here so it has to be capped, otherwise it never finishes
BENCHMARK(BM_MemoryFootprint)->Arg(1000)->Arg(10000)->Arg(100000)->Iterations(1);

BENCHMARK_MAIN();
