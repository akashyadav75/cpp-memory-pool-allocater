#include "MemoryPool.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <iomanip>

/**
 * @brief Benchmark test comparing Custom Memory Pool vs. Standard malloc/free.
 * 
 * Why this benchmark?
 * - To measure the throughput (allocations/sec) and latency of our allocator.
 * - To demonstrate the performance benefits of bypassing standard malloc's overhead,
 *   especially under thread contention.
 * 
 * Alternatives Considered & Why Rejected:
 * 1. Google Benchmark Library:
 *    - Why Rejected: Excellent library, but introduces a heavy external build dependency
 *      and setup overhead. Standard `<chrono>` library is highly precise, built-in,
 *      and cross-platform.
 */

constexpr int NUM_OPERATIONS = 1000000; // 1 Million allocations
constexpr int NUM_THREADS = 4;          // For multithreaded contention test

// Helper function to format large numbers with commas
void printFormattedNumber(long long val) {
    std::string s = std::to_string(val);
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    std::cout << s;
}

/**
 * @brief Run single-threaded benchmark using standard malloc/free.
 */
double run_standard_malloc_benchmark() {
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void*> ptrs;
    ptrs.reserve(NUM_OPERATIONS);

    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        // Vary size slightly to simulate real-world usage
        size_t size = (i % 8 + 1) * 32;
        void* ptr = std::malloc(size);
        ptrs.push_back(ptr);
    }

    for (void* ptr : ptrs) {
        std::free(ptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
}

/**
 * @brief Run single-threaded benchmark using Custom MemoryPool.
 */
double run_custom_pool_benchmark() {
    MemoryPool pool(32 * 1024 * 1024, false); // Disable thread safety for single-thread test to get raw performance

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void*> ptrs;
    ptrs.reserve(NUM_OPERATIONS);

    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        size_t size = (i % 8 + 1) * 32;
        void* ptr = pool.allocate(size);
        ptrs.push_back(ptr);
    }

    for (void* ptr : ptrs) {
        pool.free(ptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
}

/**
 * @brief Run multithreaded benchmark with standard malloc/free.
 */
double run_multithreaded_malloc_benchmark() {
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([]() {
            std::vector<void*> ptrs;
            ptrs.reserve(NUM_OPERATIONS / NUM_THREADS);

            for (int i = 0; i < NUM_OPERATIONS / NUM_THREADS; ++i) {
                size_t size = (i % 8 + 1) * 32;
                void* ptr = std::malloc(size);
                ptrs.push_back(ptr);
            }

            for (void* ptr : ptrs) {
                std::free(ptr);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
}

/**
 * @brief Run multithreaded benchmark with Custom MemoryPool (with thread-safety lock).
 */
double run_multithreaded_pool_benchmark() {
    MemoryPool pool(32 * 1024 * 1024, true); // Enable thread safety

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&pool]() {
            std::vector<void*> ptrs;
            ptrs.reserve(NUM_OPERATIONS / NUM_THREADS);

            for (int i = 0; i < NUM_OPERATIONS / NUM_THREADS; ++i) {
                size_t size = (i % 8 + 1) * 32;
                void* ptr = pool.allocate(size);
                ptrs.push_back(ptr);
            }

            for (void* ptr : ptrs) {
                pool.free(ptr);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "      ALLOCATOR PERFORMANCE & THROUGHPUT BENCHMARK      \n";
    std::cout << "========================================================\n";
    std::cout << "Allocations/Frees per run: ";
    printFormattedNumber(NUM_OPERATIONS);
    std::cout << "\nThreads used: " << NUM_THREADS << "\n";
    std::cout << "========================================================\n\n";

    // ------------------------------------------------------------------------
    // SINGLE-THREADED BENCHMARK
    // ------------------------------------------------------------------------
    std::cout << "[1/2] Running Single-Threaded Benchmarks...\n";
    
    double mallocTimeSingle = run_standard_malloc_benchmark();
    std::cout << "  - Standard malloc/free: " << std::fixed << std::setprecision(2) << mallocTimeSingle << " ms\n";
    long long mallocThroughputSingle = static_cast<long long>(NUM_OPERATIONS / (mallocTimeSingle / 1000.0));
    std::cout << "    Throughput: ";
    printFormattedNumber(mallocThroughputSingle);
    std::cout << " ops/sec\n\n";

    double poolTimeSingle = run_custom_pool_benchmark();
    std::cout << "  - Custom MemoryPool:    " << std::fixed << std::setprecision(2) << poolTimeSingle << " ms\n";
    long long poolThroughputSingle = static_cast<long long>(NUM_OPERATIONS / (poolTimeSingle / 1000.0));
    std::cout << "    Throughput: ";
    printFormattedNumber(poolThroughputSingle);
    std::cout << " ops/sec\n\n";

    double speedupSingle = mallocTimeSingle / poolTimeSingle;
    std::cout << "  >>> Speedup Factor: " << std::fixed << std::setprecision(2) << speedupSingle << "x ";
    if (speedupSingle > 1.0) {
        std::cout << "FASTER than standard malloc!\n";
    } else {
        std::cout << "slower than standard malloc.\n";
    }
    std::cout << "--------------------------------------------------------\n\n";

    // ------------------------------------------------------------------------
    // MULTI-THREADED BENCHMARK
    // ------------------------------------------------------------------------
    std::cout << "[2/2] Running Multi-Threaded Benchmarks (Contention Test)...\n";

    double mallocTimeMulti = run_multithreaded_malloc_benchmark();
    std::cout << "  - Standard malloc/free: " << std::fixed << std::setprecision(2) << mallocTimeMulti << " ms\n";
    long long mallocThroughputMulti = static_cast<long long>(NUM_OPERATIONS / (mallocTimeMulti / 1000.0));
    std::cout << "    Throughput: ";
    printFormattedNumber(mallocThroughputMulti);
    std::cout << " ops/sec\n\n";

    double poolTimeMulti = run_multithreaded_pool_benchmark();
    std::cout << "  - Custom MemoryPool:    " << std::fixed << std::setprecision(2) << poolTimeMulti << " ms\n";
    long long poolThroughputMulti = static_cast<long long>(NUM_OPERATIONS / (poolTimeMulti / 1000.0));
    std::cout << "    Throughput: ";
    printFormattedNumber(poolThroughputMulti);
    std::cout << " ops/sec\n\n";

    double speedupMulti = mallocTimeMulti / poolTimeMulti;
    std::cout << "  >>> Speedup Factor: " << std::fixed << std::setprecision(2) << speedupMulti << "x ";
    if (speedupMulti > 1.0) {
        std::cout << "FASTER than standard malloc!\n";
    } else {
        std::cout << "slower than standard malloc.\n";
    }
    std::cout << "========================================================\n";

    return 0;
}
