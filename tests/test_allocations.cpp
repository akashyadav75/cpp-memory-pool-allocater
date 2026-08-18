#include "MemoryPool.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>

// Helper macro to run a test and print success
#define RUN_TEST(testFunc) \
    do { \
        std::cout << "[RUNNING] " << #testFunc << "..." << std::endl; \
        testFunc(); \
        std::cout << "[SUCCESS] " << #testFunc << " passed!\n" << std::endl; \
    } while (0)

/**
 * @brief Test 1: Basic Allocation and Deallocation
 */
void test_basic_allocation() {
    MemoryPool pool(1024 * 1024); // 1MB Slab
    assert(pool.getUsedMemory() == 0);
    assert(pool.getSlabCount() == 0);

    // Allocate some memory
    void* p1 = pool.allocate(128);
    assert(p1 != nullptr);
    assert(pool.getUsedMemory() >= 128);
    assert(pool.getSlabCount() == 1);

    void* p2 = pool.allocate(256);
    assert(p2 != nullptr);
    assert(p2 != p1);

    // Free memory
    pool.free(p1);
    pool.free(p2);

    assert(pool.getUsedMemory() == 0);
}

/**
 * @brief Test 2: Alignment Verification
 * 
 * Verifies that the memory pool honors custom alignment requests (e.g., 16, 32, 64, 128 bytes).
 * This is crucial for performance-sensitive code like SIMD (AVX/SSE) which requires specific alignment.
 */
void test_alignment() {
    MemoryPool pool(1024 * 1024);

    size_t alignments[] = {8, 16, 32, 64, 128};
    for (size_t align : alignments) {
        void* ptr = pool.allocate(100, align);
        assert(ptr != nullptr);
        
        // Verify that the pointer is aligned to the requested boundary
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        assert((addr % align) == 0);
        (void)addr; // Prevent unused variable warning in Release builds

        pool.free(ptr);
    }
}

/**
 * @brief Test 3: Edge Cases (Null pointers, zero size, excessive size)
 */
void test_edge_cases() {
    MemoryPool pool(1024 * 1024);

    // 1. Zero size allocation should return nullptr
    void* pZero = pool.allocate(0);
    assert(pZero == nullptr);
    (void)pZero; // Prevent unused variable warning

    // 2. Freeing a nullptr should be safe and do nothing
    pool.free(nullptr);

    // 3. Extremely large allocation that exceeds OS capabilities should fail gracefully
    // (e.g., 100 Terabytes)
    size_t hugeSize = 100ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    void* pHuge = pool.allocate(hugeSize);
    assert(pHuge == nullptr);
    (void)pHuge; // Prevent unused variable warning
}

/**
 * @brief Test 4: Dynamic Slab Expansion
 * 
 * Verifies that the memory pool automatically requests new slabs from the OS
 * when existing slabs are full.
 */
void test_slab_expansion() {
    // Create a pool with small slab size (64KB)
    MemoryPool pool(64 * 1024);

    // Allocate multiple blocks that exceed 64KB in total
    void* p1 = pool.allocate(40 * 1024);
    assert(p1 != nullptr);
    assert(pool.getSlabCount() == 1);

    // This allocation cannot fit in the remaining 24KB (due to block header overhead)
    // It should trigger allocation of a second slab
    void* p2 = pool.allocate(40 * 1024);
    assert(p2 != nullptr);
    assert(pool.getSlabCount() == 2);

    pool.free(p1);
    pool.free(p2);
}

/**
 * @brief Test 5: Coalescing (Fragmentation Prevention)
 * 
 * Verifies that physical neighbors are merged back into a single large block
 * when they are freed, preventing external fragmentation.
 */
void test_coalescing() {
    MemoryPool pool(1024 * 1024);

    // Allocate three contiguous blocks
    void* p1 = pool.allocate(100);
    void* p2 = pool.allocate(100);
    void* p3 = pool.allocate(100);

    assert(p1 != nullptr && p2 != nullptr && p3 != nullptr);

    // Freeing p2 makes a free block between p1 and p3.
    pool.free(p2);

    // Freeing p1 should merge p1 and p2 into a single free block of size ~200+ bytes.
    pool.free(p1);

    // Freeing p3 should merge all three blocks back into the entire slab.
    pool.free(p3);

    assert(pool.getUsedMemory() == 0);
}

/**
 * @brief Test 6: Thread Safety (Concurrent Allocations)
 * 
 * Spawns multiple threads performing high-frequency allocation/deallocation
 * to ensure that the internal mutex locking prevents race conditions.
 */
void test_thread_safety() {
    MemoryPool pool(1024 * 1024, true); // Thread-safe pool

    constexpr int NUM_THREADS = 8;
    constexpr int NUM_ITERATIONS = 500;

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&pool]() {
            std::vector<void*> ptrs;
            ptrs.reserve(NUM_ITERATIONS);

            // Allocate phase
            for (int j = 0; j < NUM_ITERATIONS; ++j) {
                // Vary allocation sizes to simulate real-world usage
                size_t size = (j % 5 + 1) * 32;
                void* ptr = pool.allocate(size);
                assert(ptr != nullptr);
                ptrs.push_back(ptr);
            }

            // Free phase
            for (void* ptr : ptrs) {
                pool.free(ptr);
            }
        });
    }

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // All memory should be returned to the pool
    assert(pool.getUsedMemory() == 0);
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "  CUSTOM MEMORY POOL ALLOCATOR - UNIT TEST SUITE  \n";
    std::cout << "==================================================\n\n";

    RUN_TEST(test_basic_allocation);
    RUN_TEST(test_alignment);
    RUN_TEST(test_edge_cases);
    RUN_TEST(test_slab_expansion);
    RUN_TEST(test_coalescing);
    RUN_TEST(test_thread_safety);

    std::cout << "==================================================\n";
    std::cout << "             ALL TESTS PASSED SUCCESSFULLY!       \n";
    std::cout << "==================================================\n";

    return 0;
}
