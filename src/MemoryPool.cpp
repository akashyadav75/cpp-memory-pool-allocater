#include "MemoryPool.hpp"
#include <iostream>
#include <algorithm>
#include <new>
#include <cstdint>

// Platform-specific headers for VirtualAlloc / mmap
#if defined(_WIN32) || defined(_WIN64)
    #define WIN32_LEAN_AND_MEAN
    // NOMINMAX prevents windows.h from defining the `min`/`max` macros, which
    // would otherwise collide with and break calls to std::max/std::min below
    // (e.g. expanding std::max(...) into invalid syntax at compile time).
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace {
    /**
     * @brief Describes how a candidate free Block must be carved so that the FINAL
     * payload address handed back to the user satisfies a given alignment.
     *
     * Why is this needed?
     * - Rounding up the requested SIZE (as done previously) does nothing to guarantee
     *   that the actual returned POINTER is aligned. A block's payload address depends
     *   on where its header happens to sit in memory, which has no relationship to the
     *   payload size at all.
     * - There are two distinct scenarios:
     *   1. The block's OWN payload address is already aligned -> use it directly
     *      (needsSplit = false). No extra header overhead is introduced.
     *   2. The block's payload is misaligned -> we must carve off a small "front
     *      waste" region (kept as its own free block, `frontWasteSize` bytes) so
     *      that a brand-new Block header can begin exactly where
     *      (newHeaderAddr + sizeof(Block)) is a multiple of `alignment`.
     *      Note: because Block::split() can only place a new header AFTER the
     *      current payload start, the earliest a new header can land is at the
     *      current payload address itself (frontWasteSize == 0), never earlier -
     *      which is why scenario 1 must be checked as a separate, explicit case.
     */
    struct AlignmentPlan {
        bool needsSplit;
        size_t frontWasteSize; // Only meaningful when needsSplit == true
    };

    AlignmentPlan computeAlignmentPlan(uintptr_t rawPayloadAddr, size_t alignment, size_t headerSize) {
        if (rawPayloadAddr % alignment == 0) {
            // Case 1: Already aligned - no carving needed.
            return AlignmentPlan{false, 0};
        }

        // Case 2: Solve for the smallest front waste W >= 0 such that
        // (rawPayloadAddr + W + headerSize) % alignment == 0.
        uintptr_t addrIfZeroWaste = rawPayloadAddr + headerSize;
        uintptr_t remainder = addrIfZeroWaste % alignment;
        size_t frontWaste = (remainder == 0) ? 0 : static_cast<size_t>(alignment - remainder);
        return AlignmentPlan{true, frontWaste};
    }
}

/**
 * @brief Constructor for the Memory Pool.
 */
MemoryPool::MemoryPool(size_t defaultSlabSize, bool threadSafe)
    : m_defaultSlabSize(defaultSlabSize),
      m_threadSafe(threadSafe),
      m_freeListHead(nullptr),
      m_totalCapacity(0),
      m_usedMemory(0) {
    
    // Ensure the default slab size is at least a page size.
    // On most modern systems, the page size is 4KB (4096 bytes).
    size_t pageSize = 4096;
#if defined(_WIN32) || defined(_WIN64)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    pageSize = sysInfo.dwPageSize;
#else
    pageSize = sysconf(_SC_PAGESIZE);
#endif

    if (m_defaultSlabSize < pageSize) {
        m_defaultSlabSize = pageSize;
    }
    // Align default slab size to page boundary
    m_defaultSlabSize = alignUp(m_defaultSlabSize, pageSize);
}

/**
 * @brief Destructor. Releases all OS-allocated slabs back to the system.
 */
MemoryPool::~MemoryPool() {
    // Acquire lock if thread-safe
    std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
    if (m_threadSafe) {
        lock.lock();
    }

    // Free all slabs back to the Operating System
    for (const auto& slab : m_slabs) {
        freeSlabToOS(slab);
    }
    m_slabs.clear();
    m_freeListHead = nullptr;
    m_totalCapacity = 0;
    m_usedMemory = 0;
}

/**
 * @brief Allocates a block of memory of the specified size.
 * 
 * Allocation Process:
 * 1. Align the requested size to the boundary (e.g., 8 or 16 bytes).
 * 2. Search the explicit free list for a block that can hold the request (First-Fit).
 * 3. If a block is found:
 *    a. Split the block if there is sufficient excess space (O(1)).
 *    b. Remove the block from the free list.
 *    c. Mark the block as allocated.
 * 4. If no block is found:
 *    a. Allocate a new large Slab from the OS using VirtualAlloc/mmap.
 *    b. Initialize the Slab as a single large free block.
 *    c. Split this new slab to satisfy the request.
 *    d. Add the remaining split portion to the free list.
 */
void* MemoryPool::allocate(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    if (alignment == 0) {
        alignment = 8;
    }

    // Align the requested size to the alignment boundary.
    // Note: This alone does NOT guarantee the returned pointer is aligned -
    // it only ensures the payload region's length is a clean multiple of
    // `alignment`. Actual pointer alignment is guaranteed below by carving
    // off any front waste needed so a Block header can start exactly where
    // (header + sizeof(Block)) lands on an `alignment`-byte boundary.
    size_t alignedSize = alignUp(size, alignment);

    // Lock the pool if thread safety is enabled
    std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
    if (m_threadSafe) {
        lock.lock();
    }

    // Search the explicit free list (First-Fit strategy)
    // Why First-Fit?
    // - It is highly efficient in practice (O(1) average case) compared to Best-Fit (O(N)),
    //   as it stops at the first block that fits.
    // - Combined with coalescing, it minimizes external fragmentation effectively.
    //
    // For each candidate, we also compute the AlignmentPlan describing whether/how much
    // front waste must be carved off so the FINAL returned payload address satisfies the
    // requested alignment. A block only qualifies if it has enough room for that waste,
    // a fresh Block header (if needed), and the requested payload.
    Block* current = m_freeListHead;
    Block* foundBlock = nullptr;
    AlignmentPlan foundPlan{false, 0};

    while (current) {
        uintptr_t rawPayload = reinterpret_cast<uintptr_t>(current->getPayload());
        AlignmentPlan plan = computeAlignmentPlan(rawPayload, alignment, sizeof(Block));

        bool fits = plan.needsSplit
            ? (current->size >= plan.frontWasteSize + sizeof(Block) + alignedSize)
            : (current->size >= alignedSize);

        if (fits) {
            foundBlock = current;
            foundPlan = plan;
            break;
        }
        current = current->nextFree;
    }

    // Tracks whether `foundBlock` is currently registered in the explicit free list.
    bool foundInFreeList = false;

    // If no block was found in the free list, allocate a new slab from the OS
    if (!foundBlock) {
        // We need enough space for: the aligned request size, a Block header for it,
        // worst-case alignment waste (up to `alignment - 1` bytes), and one extra
        // Block header in case that waste must be carved into its own free fragment.
        size_t requiredSlabSize = alignedSize + (sizeof(Block) * 2) + alignment;
        size_t slabSizeToAllocate = (std::max)(m_defaultSlabSize, requiredSlabSize);

        Slab newSlab = allocateSlabFromOS(slabSizeToAllocate);
        if (!newSlab.address) {
            // OS-level out of memory (VirtualAlloc/mmap returned failure)
            return nullptr;
        }

        m_slabs.push_back(newSlab);
        m_totalCapacity += newSlab.size;

        // Initialize the new slab as a single large free block
        Block* newBlock = ::new (newSlab.address) Block(newSlab.size - sizeof(Block));
        newBlock->isFree = true;

        // Add this new block to the free list
        addToFreeList(newBlock);

        // Recompute the alignment plan for this freshly-created block.
        uintptr_t rawPayload = reinterpret_cast<uintptr_t>(newBlock->getPayload());
        foundPlan = computeAlignmentPlan(rawPayload, alignment, sizeof(Block));
        foundBlock = newBlock;
    }

    foundInFreeList = true;

    // If the plan calls for carving off front waste, do so now. The resulting
    // `alignedBlock` is a brand-new, not-yet-registered Block whose payload is
    // guaranteed to satisfy the requested alignment. The original `foundBlock`
    // shrinks down to the waste region and correctly remains a free block right
    // where it already sits in the free list (Block::split() only touches
    // physical next/prev links, never the explicit free-list pointers).
    if (foundPlan.needsSplit) {
        Block* alignedBlock = foundBlock->split(foundPlan.frontWasteSize);
        // Guaranteed non-null: we verified sufficient size for waste + header +
        // payload before selecting this block.
        foundBlock = alignedBlock;
        foundInFreeList = false; // alignedBlock was never registered in the free list
    }

    // Try to split the block to avoid internal fragmentation
    Block* remainingBlock = foundBlock->split(alignedSize);
    if (remainingBlock) {
        // If split succeeded, we have a new free block.
        // We must add the new split block to the free list.
        addToFreeList(remainingBlock);
    }

    // Remove the allocated block from the free list (only if it was actually in it)
    if (foundInFreeList) {
        removeFromFreeList(foundBlock);
    }
    foundBlock->isFree = false;

    // Update statistics
    m_usedMemory += foundBlock->size + sizeof(Block);

    // Return the payload pointer to the user - now guaranteed to satisfy `alignment`.
    void* result = foundBlock->getPayload();
    return result;
}

/**
 * @brief Deallocates/frees an allocated block of memory.
 * 
 * Deallocation Process:
 * 1. Convert payload pointer back to Block header pointer.
 * 2. Mark the block as free.
 * 3. Coalesce the block with physically adjacent neighbors (O(1)).
 * 4. Add the resulting block to the explicit free list.
 */
void MemoryPool::free(void* ptr) {
    if (!ptr) {
        return;
    }

    // Retrieve block header from payload pointer
    Block* block = Block::fromPayload(ptr);

    // Lock the pool if thread safety is enabled
    std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
    if (m_threadSafe) {
        lock.lock();
    }

    // Guard against double-free
    if (block->isFree) {
        return;
    }

    // Update statistics
    m_usedMemory -= (block->size + sizeof(Block));

    // Mark the block as free
    block->isFree = true;

    // Coalesce physical neighbors to prevent fragmentation.
    // Why coalesce immediately?
    // - It provides deterministic O(1) deallocation latency.
    // - It prevents the free list from filling up with tiny, fragmented blocks.
    // Note: If physical neighbors are coalesced, we must remove those neighbors
    // from the free list before merging, then add the final merged block to the free list.
    
    // Check if next physical block is free and merge
    if (block->next && block->next->isFree) {
        Block* nextBlock = block->next;
        removeFromFreeList(nextBlock);
        
        block->size += sizeof(Block) + nextBlock->size;
        block->next = nextBlock->next;
        if (nextBlock->next) {
            nextBlock->next->prev = block;
        }
    }

    // Check if previous physical block is free and merge
    if (block->prev && block->prev->isFree) {
        Block* prevBlock = block->prev;
        removeFromFreeList(prevBlock);
        
        prevBlock->size += sizeof(Block) + block->size;
        prevBlock->next = block->next;
        if (block->next) {
            block->next->prev = prevBlock;
        }
        block = prevBlock;
    }

    // Add the final (possibly coalesced) block back to the explicit free list
    addToFreeList(block);
}

/**
 * @brief Allocates a large slab directly from the OS using VirtualAlloc/mmap.
 * 
 * Why VirtualAlloc/mmap?
 * - VirtualAlloc is the low-level Windows API for managing virtual memory pages. It bypasses
 *   the user-mode heap allocator, eliminating internal heap locking, tracking, and fragmentation.
 * - mmap is the POSIX equivalent, allowing us to map anonymous pages directly into the process's address space.
 * 
 * Alternatives Considered & Why Rejected:
 * 1. std::malloc / operator new:
 *    - Why Rejected: They are backed by runtime libraries which allocate memory with their own
 *      headers and alignment padding, leading to "double header" overhead (malloc header + our Block header).
 * 2. HeapCreate / HeapAlloc (Windows):
 *    - Why Rejected: Creating a private heap still relies on the Windows NT Heap Manager, which
 *      performs its own internal fragmentation management and locking, adding latency.
 */
MemoryPool::Slab MemoryPool::allocateSlabFromOS(size_t size) {
    Slab slab = {nullptr, 0};

    // Round up size to page size to ensure clean virtual memory allocation
    size_t pageSize = 4096;
#if defined(_WIN32) || defined(_WIN64)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    pageSize = sysInfo.dwPageSize;
#else
    pageSize = sysconf(_SC_PAGESIZE);
#endif
    size_t alignedSize = alignUp(size, pageSize);

#if defined(_WIN32) || defined(_WIN64)
    // MEM_COMMIT: Allocates physical storage in memory or in the paging file.
    // MEM_RESERVE: Reserves a range of the process's virtual address space without allocating physical storage.
    // PAGE_READWRITE: Enables read-only or read-write access to the committed region of pages.
    void* addr = VirtualAlloc(nullptr, alignedSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    // MAP_PRIVATE: Create a private copy-on-write mapping.
    // MAP_ANONYMOUS: The mapping is not backed by any file; its contents are initialized to zero.
    void* addr = mmap(nullptr, alignedSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        addr = nullptr;
    }
#endif

    if (addr) {
        slab.address = addr;
        slab.size = alignedSize;
    } else {
        std::cerr << "[MemoryPool] Error: OS virtual memory allocation failed for size " << alignedSize << " bytes." << std::endl;
    }

    return slab;
}

/**
 * @brief Frees an OS-allocated slab back to the system.
 */
void MemoryPool::freeSlabToOS(const Slab& slab) {
    if (!slab.address || slab.size == 0) {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    // MEM_RELEASE: Decommits and releases the specified region of pages.
    // When releasing memory, the size parameter must be 0, and the address must be the base address.
    if (!VirtualFree(slab.address, 0, MEM_RELEASE)) {
        std::cerr << "[MemoryPool] Error: VirtualFree failed." << std::endl;
    }
#else
    if (munmap(slab.address, slab.size) != 0) {
        std::cerr << "[MemoryPool] Error: munmap failed." << std::endl;
    }
#endif
}

/**
 * @brief Adds a block to the explicit doubly-linked free list.
 * 
 * Why explicit free list?
 * - It contains ONLY free blocks. This means when searching for a free block, we do not have
 *   to traverse allocated blocks, making allocation latency dependent only on the number of
 *   free blocks (O(Free_Blocks)) rather than the total number of blocks (O(Total_Blocks)).
 */
void MemoryPool::addToFreeList(Block* block) {
    if (!block) return;

    // Insert at the head of the free list (LIFO order is simple and fast - O(1))
    block->nextFree = m_freeListHead;
    block->prevFree = nullptr;

    if (m_freeListHead) {
        m_freeListHead->prevFree = block;
    }
    m_freeListHead = block;
}

/**
 * @brief Removes a block from the explicit doubly-linked free list.
 */
void MemoryPool::removeFromFreeList(Block* block) {
    if (!block) return;

    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        // Block was the head of the free list
        m_freeListHead = block->nextFree;
    }

    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }

    block->nextFree = nullptr;
    block->prevFree = nullptr;
}
