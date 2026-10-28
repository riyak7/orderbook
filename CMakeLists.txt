cmake_minimum_required(VERSION 3.16)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# benchmark numbers are meaningless in debug, so default to release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_compile_options(-Wall -Wextra)

add_executable(orderbook
    src/main.cpp
    src/OrderBook.cpp
)
target_include_directories(orderbook PRIVATE src)

add_executable(orderbook_bench
    benchmarks/bench_main.cpp
    src/OrderBook.cpp
)
target_include_directories(orderbook_bench PRIVATE src)
target_link_libraries(orderbook_bench PRIVATE benchmark::benchmark psapi)

add_executable(orderbook_tests
    tests/orderbook_tests.cpp
    src/OrderBook.cpp
)
target_include_directories(orderbook_tests PRIVATE src)
target_link_libraries(orderbook_tests PRIVATE GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(orderbook_tests)
