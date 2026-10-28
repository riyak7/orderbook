# orderbook

Limit order book + matching engine in C++17. Handles limit orders, market orders and cancels, matched by price then time.

## how it works

Limit orders trade against whatever they can straight away and the rest sits on the book. Market orders just eat through the other side until they're filled or the book is empty, and anything left over gets thrown away (they never rest).

Matching is price-time priority. A buy hits the lowest ask first and a sell hits the highest bid first, and it trades at the resting order's price, so a buy at 101 against an ask at 100 fills at 100. If there are a few orders at the same price, whoever got there first gets filled first. It keeps going down the levels until the incoming order is done, the other side runs out, or the next price is past its limit.

e.g. asks are 100 x 5, 100 x 7, 101 x 5 and a buy for 8 @ 101 comes in. It takes the whole 5 from the first order at 100 and 3 from the second one, so you're left with 100 x 4 and 101 x 5.

Orders with quantity <= 0, or a limit price <= 0, get rejected and you get back `INVALID_ORDER_ID` (0) instead of an id. Ids start from 1 so 0 is never a real one. `cancel_order` returns true if it actually cancelled something and false if the id doesn't exist or the order already filled.

## design

Each side is a `std::map` of price -> `std::deque<Order>`. Bids use `std::greater` so they're sorted high to low, asks are low to high, so the best price on either side is always `begin()`. Adding a new level is O(log n). I went with a map because it keeps the prices sorted for me. A hash map can't tell you the best price, and a sorted vector means shifting everything over whenever a level gets added in the middle.

Each price level is a deque because it's a FIFO queue: new orders go on the back, fills come off the front, both O(1). The other reason is that deque doesn't move existing elements when you push_back or pop_front, so pointers to orders stay valid. That matters because of the lookup map. A vector would reallocate and break all of them.

`order_lookup_` is an `unordered_map<id, Order*>` so cancel can go straight to the order instead of searching the book. Only resting orders are in it. They get added when they rest and removed when they're cancelled or fully filled. (Originally filled orders were never removed, so cancelling one wrote to freed memory. Fixed now and there's a test for it.)

Cancels are lazy. Taking an order out of the middle of a deque is O(n) and moves everything after it, which would break the pointers. So cancel just sets `cancelled = true` and removes the id from the lookup, O(1). The order stays in the deque until matching gets to it and pops it. The cost is that cancelled orders hang around in memory until then, and `print_book` has to skip them (and skip a level if everything in it is cancelled).

## building

Needs CMake 3.16+ and a C++17 compiler. I'm using MSYS2 g++ (UCRT64) and ninja on Windows. Google Benchmark and GoogleTest get downloaded by cmake.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/orderbook          # little demo
./build/orderbook_tests    # or: ctest --test-dir build
./build/orderbook_bench
```

If you don't give a build type it defaults to Release, since benchmarking a debug build is pointless.

If the exes crash immediately when run from git bash/powershell it's probably loading the wrong libstdc++ dll. Run them from the MSYS2 UCRT64 shell or put `C:\msys64\ucrt64\bin` first in PATH.

The memory benchmark uses the windows API so the bench only builds on windows right now.

## tests

GoogleTest. Covers full and partial fills, filling across multiple levels, stopping at the limit price, time priority at the same price, cancelling a resting order / an already filled one / an id that doesn't exist, cancelled orders getting skipped, market orders into an empty book and bigger than the book, and the input validation. The tests check the book by printing it into a string, which is why `print_book` takes an ostream (defaults to cout).

## benchmarks

Release build, g++ 16.1, Windows 11, 16 threads @ 2.9GHz, 1.25MB L2, 24MB L3. Numbers vary quite a bit between runs on my laptop (20%+ depending on power settings and what else is running), so treat them as rough.

```
heavy matching (buy/sell alternating at one price)   ~90 ns/order   (~11.4M orders/sec)
add order, no match, new level each time              ~210 ns/order
insert at new level, 100 levels                       ~170 ns
insert at new level, 1,000 levels                     ~220 ns
insert at new level, 10,000 levels                    ~404 ns
cancel                                                ~22 ns
memory, 100k orders each on its own level             ~708 bytes/order
```

Insert time grows with depth but faster than just log n would explain. 1,000 -> 10,000 levels is only ~1.3x more comparisons but ~1.8x the time, I think because at 10k levels the tree doesn't fit in L2 anymore so walking down it starts missing cache.

Making a new level is the slow part. It's a new map node plus a new deque, and libstdc++'s deque allocates a 512 byte block right away even for one order. That's where most of the ~700 bytes/order comes from, and why matching at one price is a lot faster than adding new levels.

The deep book benchmark times 100 inserts per iteration instead of 1 because PauseTiming/ResumeTiming cost a few hundred ns themselves, more than the insert. The memory numbers for smaller books were unreliable (the 1k run even came out as 0, because the allocator reuses memory from earlier runs) so I only kept 100k.

## future improvements

- integer tick prices instead of double. Doubles can't represent most decimal prices exactly so two prices that should be the same level can end up as different keys. Ints are exact, faster to compare, and you can't get NaN/inf.
- one templated matching function. The matching loop is copy-pasted 4 times (limit buy/sell, market buy/sell) and only really differs in which side it walks and the price check. The lookup bug had to be fixed in all 4 places, which is the problem with that.
- reporting trades/fills. Right now matching just changes quantities and nothing records what traded. Should give back fill events (both ids, price, qty).
- tick-indexed array + memory pool. With integer ticks the levels could be a flat array indexed by tick instead of a map, so finding a level is O(1) and nearby prices are next to each other in memory. Getting memory from a preallocated pool instead of every new map node and deque allocating separately would cut allocation cost and keep things together in cache, which is what’s hurting the deep book numbers.
