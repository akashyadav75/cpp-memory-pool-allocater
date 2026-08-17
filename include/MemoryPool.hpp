#ifndef MEMORY_POOL_HPP
#define MEMORY_POOL_HPP

#include "Block.hpp"
#include <cstddef>
#include <mutex>
#include <vector>

/**
 * @brief Custom Memory Pool Allocator leveraging OS-level virtual memory APIs.
 * 
 * Why this Allocator?
 * - Standard malloc/free/new/delete are general-purpose allocators. They must handle
 *   arbitrary multi-threaded requests, leading to lock contention, heap fragmentation,
 *   and system call overhead (switching between user and kernel mode).
 * - This allocator bypasses the standard C/C++ runtime heap manager by requesting large
 *   virtual memory pages directly from the Operating System via VirtualAlloc (on Windows)
 *   or mmap (on POSIX systems).
 * - By managing these large blocks locally, we achieve near O(1) allocation/deallocation latency
 *   and eliminate standard malloc overhead and fragmentation.
 * 
 * Alternatives Considered & Why Rejected:
 * 1. Standard malloc/free:
 *    - Why Rejected: High latency due to lock contention in multithreaded apps, and unpredictable
 *      fragmentation over long execution runs.
 * 2. jemalloc / tcmalloc:
 *    - Why Rejected: Excellent general-purpose allocators, but they introduce massive external
 *      library dependencies. For embedded, game engine, or performance-critical subsystems,
 *      a lightweight custom allocator is easier to audit, tune, and keep dependency-free.
 * 3. Thread-Local Storage (TLS) Pools:
 *    - Why Rejected: While TLS pools eliminate lock contention entirely, they can lead to memory
 *      hoarding (where thread A holds onto memory that thread B needs). A centralized pool with
 *      fine-grained locking or optional thread safety provides better overall memory utilization.
 */
class MemoryPool {
public:
    /**
     * @brief Constructor for the Memory Pool.
     * @param defaultSlabSize The size of the large memory chunks requested from the OS (default: 4MB).
     * @param threadSafe Enable or disable thread safety using internal mutex locking.
     */
    explicit MemoryPool(size_t defaultSlabSize = 4 * 1024 * 1024, bool threadSafe = true);

    /**
     * @brief Destructor. Releases all OS-allocated slabs back to the system.
     */
    ~MemoryPool();

    // Prevent copying and assignment to ensure resource safety (RAII)
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) noexcept = delete;
    MemoryPool& operator=(MemoryPool&&) noexcept = delete;

    /**
     * @brief Allocates a block of memory of the specified size.
     * @param size Number of bytes to allocate.
     * @param alignment Alignment boundary (must be a power of 2, default is 8-byte alignment).
     * @return void* Pointer to the allocated memory, or nullptr if allocation fails.
     */
    void* allocate(size_t size, size_t alignment = 8);

    /**
     * @brief Deallocates/frees an allocated block of memory.
     * @param ptr Pointer to the memory block to free.
     */
    void free(void* ptr);

    /**
     * @brief Gets the total size of memory requested from the OS.
     */
    size_t getTotalCapacity() const { return m_totalCapacity; }

    /**
     * @brief Gets the total size of memory currently allocated to the user.
     */
    size_t getUsedMemory() const { return m_usedMemory; }

    /**
     * @brief Gets the number of OS slabs currently allocated.
     */
    size_t getSlabCount() const { return m_slabs.size(); }

private:
    /**
     * @brief Represents a large chunk of memory allocated directly from the OS.
     */
    struct Slab {
        void* address;     // Base address returned by VirtualAlloc/mmap
        size_t size;       // Size of the slab in bytes
    };

    /**
     * @brief Allocates a new slab from the OS.
     * @param size Size of the slab to allocate (will be rounded up to OS page size).
     * @return Slab The allocated slab structure.
     */
    Slab allocateSlabFromOS(size_t size);

    /**
     * @brief Frees a slab back to the OS.
     * @param slab The slab to free.
     */
    void freeSlabToOS(const Slab& slab);

    /**
     * @brief Adds a block to the explicit doubly-linked free list.
     */
    void addToFreeList(Block* block);

    /**
     * @brief Removes a block from the explicit doubly-linked free list.
     */
    void removeFromFreeList(Block* block);

    /**
     * @brief Aligns a size up to the nearest multiple of alignment.
     */
    inline size_t alignUp(size_t size, size_t alignment) const {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    size_t m_defaultSlabSize;       // Default size for new slab allocations
    bool m_threadSafe;              // Thread safety flag
    mutable std::mutex m_mutex;     // Mutex for thread safety

    std::vector<Slab> m_slabs;      // List of all OS-allocated slabs
    Block* m_freeListHead;          // Head of the explicit free list

    size_t m_totalCapacity;         // Total bytes allocated from the OS
    size_t m_usedMemory;            // Total bytes currently allocated to user
};

#endif // MEMORY_POOL_HPP
