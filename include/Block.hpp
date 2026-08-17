#ifndef BLOCK_HPP
#define BLOCK_HPP

#include <cstddef>
#include <new>

/**
 * @brief Represents a block of memory inside the Custom Memory Pool.
 * 
 * Why Block-based design?
 * - To manage variable-sized allocations within large VirtualAlloc-allocated chunks.
 * - Each block contains metadata (header) and user payload.
 * - Doubly-linked structure allows O(1) coalescing of adjacent free blocks to prevent memory fragmentation.
 * 
 * Alternatives Considered & Why Rejected:
 * 1. Single Fixed-Size Block Pool (Slab Allocator):
 *    - Why Rejected: While a slab allocator has O(1) allocation/deallocation with zero fragmentation,
 *      it restricts allocations to a single fixed size. This is not versatile enough for a general-purpose
 *      pool allocator replacement.
 * 2. Bitmapped Allocator:
 *    - Why Rejected: Finding free blocks requires scanning bit arrays, which can introduce latency (O(N) search)
 *      and does not handle variable sizes naturally without grouping blocks, adding complexity.
 * 3. Implicit Free List (Header only, no pointers):
 *    - Why Rejected: Traversal of free blocks requires hopping through allocated blocks as well,
 *      resulting in O(N) allocation latency where N is the total number of blocks.
 * 
 * Selected Design: Explicit Doubly-Linked Free List with Boundary Tags
 * - Each block has a header containing:
 *   - size: The size of the payload.
 *   - isFree: Flag indicating if the block is currently free.
 *   - next: Pointer to the next block in physical memory (for coalescing).
 *   - prev: Pointer to the previous block in physical memory (for coalescing).
 *   - nextFree: Pointer to the next block in the explicit free list (for fast O(Free_Blocks) allocation).
 *   - prevFree: Pointer to the previous block in the explicit free list.
 */
struct Block {
    size_t size;          // Size of the usable payload in bytes
    bool isFree;          // True if the block is free, false if allocated
    Block* next;          // Next physical block in memory
    Block* prev;          // Previous physical block in memory
    Block* nextFree;      // Next block in the explicit free list
    Block* prevFree;      // Previous block in the explicit free list

    /**
     * @brief Constructor to initialize a block.
     * @param blockSize Usable size of the payload.
     */
    Block(size_t blockSize)
        : size(blockSize), isFree(true), next(nullptr), prev(nullptr),
          nextFree(nullptr), prevFree(nullptr) {}

    /**
     * @brief Gets a pointer to the user payload.
     * 
     * Why is this inline?
     * - To eliminate function call overhead in hot allocation paths.
     */
    inline void* getPayload() {
        return reinterpret_cast<void*>(reinterpret_cast<char*>(this) + sizeof(Block));
    }

    /**
     * @brief Gets the block pointer from a user payload pointer.
     * @param payload Pointer to the user data.
     */
    static inline Block* fromPayload(void* payload) {
        if (!payload) return nullptr;
        return reinterpret_cast<Block*>(reinterpret_cast<char*>(payload) - sizeof(Block));
    }

    /**
     * @brief Splits the current block if it is larger than the requested size plus header overhead.
     * @param requestedSize The size requested by the allocator (must be aligned).
     * @return Block* Pointer to the newly created free block, or nullptr if split is not possible.
     */
    Block* split(size_t requestedSize);

    /**
     * @brief Coalesces this block with its physically adjacent neighbors if they are free.
     * @return Block* The resulting merged block (could be 'prev' if merged backwards).
     */
    Block* coalesce();
};

#endif // BLOCK_HPP


