// app/src/main/cpp/memory/PoolAllocator.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// PoolAllocator
//
// Fixed-size block allocator for high-frequency, same-size allocations.
// Examples: ECS component arrays, bullet pool, particle pool.
//
// Properties:
//   • O(1) alloc and free
//   • Zero fragmentation (all blocks are the same size)
//   • Cache-friendly: all blocks are contiguous in memory
//   • Thread-unsafe: use per-thread allocators or external locking
//
// Memory layout:
//   [Block 0][Block 1]...[Block N-1]
//   Free list: singly linked list through the free blocks
// ─────────────────────────────────────────────────────────────────────────────
class PoolAllocator final {
public:
    // blockSize:  Size of each allocation in bytes (must be >= sizeof(void*))
    // blockCount: Maximum number of concurrent live allocations
    // alignment:  Block alignment in bytes (must be power of 2, >= alignof(void*))
    PoolAllocator(size_t blockSize, size_t blockCount, size_t alignment = 16);
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // Allocate a single block. Returns nullptr if pool is exhausted.
    [[nodiscard]] void* allocate() noexcept;

    // Return a block to the pool. ptr must have been returned by allocate().
    void deallocate(void* ptr) noexcept;

    // ── Diagnostics ───────────────────────────────────────────────────────
    [[nodiscard]] size_t freeBlockCount()  const noexcept { return m_freeCount; }
    [[nodiscard]] size_t totalBlockCount() const noexcept { return m_totalCount; }
    [[nodiscard]] size_t usedBlockCount()  const noexcept { return m_totalCount - m_freeCount; }
    [[nodiscard]] bool   isEmpty()         const noexcept { return m_freeCount == m_totalCount; }
    [[nodiscard]] bool   isFull()          const noexcept { return m_freeCount == 0; }

private:
    // Free list node — occupies the memory of the free block itself
    struct FreeNode { FreeNode* next; };

    uint8_t*    m_memory     = nullptr;     // Backing allocation
    FreeNode*   m_freeHead   = nullptr;     // Head of free list
    size_t      m_blockSize  = 0;
    size_t      m_totalCount = 0;
    size_t      m_freeCount  = 0;
};

} // namespace hs
