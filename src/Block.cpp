#include "Block.hpp"

/**
 * @brief Splits the current block into two blocks: one of requestedSize and one representing the remaining space.
 * 
 * Why do we split?
 * - Splitting prevents internal fragmentation. If a user requests 32 bytes and we assign them a 1024-byte block,
 *   992 bytes are wasted. By splitting, we create a new free block of 992 - sizeof(Block) bytes and return it
 *   to the free list.
 * 
 * Why might we NOT split? (Alternatives considered & rejected)
 * 1. Never Split (No-split policy):
 *    - Why Rejected: Extremely simple and fast (O(1) allocation), but results in massive internal fragmentation.
 *      Highly inefficient for general-purpose variable-sized allocation pools.
 * 2. Threshold-based Splitting:
 *    - Only split if the remaining space is greater than a certain threshold (e.g., 64 bytes).
 *    - We actually implement a form of this: we only split if the remaining space can accommodate at least
 *      a Block header metadata plus a minimum payload (e.g., 8 or 16 bytes). If the remaining space is too
 *      small, splitting is rejected because the new Block header would consume all or most of the remaining
 *      space, leading to negative or zero usable payload.
 */
Block* Block::split(size_t requestedSize) {
    // Determine the minimum size required to form a new block.
    // A new block requires at least sizeof(Block) bytes for its header and at least 8 bytes for some payload.
    constexpr size_t MIN_BLOCK_OVERHEAD = sizeof(Block) + 8;

    // Check if the block has enough excess space to warrant a split.
    if (size < requestedSize + MIN_BLOCK_OVERHEAD) {
        // Not enough space to split. If we split, the new block would be too small to be useful.
        // We return nullptr to indicate that splitting was not performed.
        return nullptr;
    }

    // Calculate where the new block header will start in memory.
    // It starts immediately after the current block's payload for the requested size.
    char* currentPayloadStart = reinterpret_cast<char*>(getPayload());
    char* newBlockHeaderStart = currentPayloadStart + requestedSize;

    // Construct the new block in-place using placement new.
    size_t newBlockSize = size - requestedSize - sizeof(Block);
    Block* newBlock = ::new (static_cast<void*>(newBlockHeaderStart)) Block(newBlockSize);

    // Update physical block links
    newBlock->next = this->next;
    newBlock->prev = this;
    if (this->next) {
        this->next->prev = newBlock;
    }
    this->next = newBlock;

    // Shrink the current block to the requested size
    this->size = requestedSize;

    // Note: The caller (MemoryPool) is responsible for inserting 'newBlock' into the free list.
    return newBlock;
}

/**
 * @brief Coalesces this block with its physically adjacent neighbors if they are free.
 * 
 * Why do we coalesce?
 * - Coalescing merges adjacent free blocks to form larger contiguous memory blocks.
 * - Without coalescing, we would suffer from severe external fragmentation, where we have plenty of free
 *   memory in total, but it is broken up into small blocks, making it impossible to satisfy a larger allocation request.
 * 
 * Alternatives Considered & Why Rejected:
 * 1. Lazy Coalescing (Deferred Coalescing):
 *    - Why Rejected: Under lazy coalescing, blocks are freed without immediate merging. Merging is deferred until
 *      an allocation request fails (OOM) or during periodic garbage collection. While this makes 'free()' extremely
 *      fast (O(1)), it increases allocation latency unpredictably during a fallback coalescing pass. Immediate O(1)
 *      coalescing is preferred for real-time systems to maintain deterministic allocation latency.
 * 2. Boundary Tag Coalescing (without physical pointers):
 *    - Why Rejected: Requires traversing memory using mathematical offsets based on block sizes. While it saves
 *      pointer overhead (8-16 bytes per block), it is highly error-prone and sensitive to corruption.
 *      Physical next/prev pointers are safer, self-documenting, and robust.
 */
Block* Block::coalesce() {
    // We can only coalesce if this block itself is free.
    if (!isFree) {
        return this;
    }

    Block* current = this;

    // Try to merge with the next physical block
    if (current->next && current->next->isFree) {
        Block* nextBlock = current->next;
        
        // Accumulate the size: we gain the next block's payload AND its header size!
        current->size += sizeof(Block) + nextBlock->size;
        
        // Bypass nextBlock in the physical chain
        current->next = nextBlock->next;
        if (nextBlock->next) {
            nextBlock->next->prev = current;
        }

        // Note: The caller (MemoryPool) must remove 'nextBlock' from the free list.
    }

    // Try to merge with the previous physical block
    if (current->prev && current->prev->isFree) {
        Block* prevBlock = current->prev;
        
        // Accumulate the size: prev block gains our payload AND our header size!
        prevBlock->size += sizeof(Block) + current->size;
        
        // Bypass current in the physical chain
        prevBlock->next = current->next;
        if (current->next) {
            current->next->prev = prevBlock;
        }

        // Note: The caller (MemoryPool) must remove 'current' from the free list.
        current = prevBlock;
    }

    return current;
}
