// app/src/main/cpp/memory/PoolAllocator.cpp
#include "PoolAllocator.h"
#include "../utils/Logger.h"

#include <cstdlib>      // for aligned_alloc
#include <new>          // for placement new
#include <algorithm>    // for std::max

namespace hs {

PoolAllocator::PoolAllocator(size_t blockSize, size_t blockCount, size_t alignment) {
    // Ensure blocks are large enough to store the free list pointer
    m_blockSize  = std::max(blockSize, sizeof(FreeNode));
    // Align block size up to the requested alignment
    m_blockSize  = (m_blockSize + alignment - 1) & ~(alignment - 1);
    m_totalCount = blockCount;
    m_freeCount  = blockCount;

    const size_t totalBytes = m_blockSize * blockCount;

    // aligned_alloc: alignment must be a multiple of sizeof(void*) and
    // totalBytes must be a multiple of alignment.
    m_memory = static_cast<uint8_t*>(
        aligned_alloc(alignment, (totalBytes + alignment - 1) & ~(alignment - 1))
    );

    if (!m_memory) {
        LOG_ERROR("PoolAllocator: failed to allocate %zu bytes (alignment=%zu)",
                  totalBytes, alignment);
        return;
    }

    // Build the free list by chaining each block
    m_freeHead = nullptr;
    for (size_t i = blockCount; i-- > 0; ) {
        auto* node  = reinterpret_cast<FreeNode*>(m_memory + i * m_blockSize);
        node->next  = m_freeHead;
        m_freeHead  = node;
    }

    LOG_INFO("PoolAllocator: %zu blocks x %zu bytes = %zu KB",
             blockCount, m_blockSize, totalBytes / 1024);
}

PoolAllocator::~PoolAllocator() {
    if (usedBlockCount() > 0) {
        LOG_WARN("PoolAllocator: destroyed with %zu live allocations (memory leak!)",
                 usedBlockCount());
    }
    free(m_memory);
    m_memory = nullptr;
}

void* PoolAllocator::allocate() noexcept {
    if (!m_freeHead) {
        LOG_ERROR("PoolAllocator: pool exhausted (%zu/%zu blocks in use)",
                  usedBlockCount(), m_totalCount);
        return nullptr;
    }
    FreeNode* block = m_freeHead;
    m_freeHead      = m_freeHead->next;
    --m_freeCount;
    return block;
}

void PoolAllocator::deallocate(void* ptr) noexcept {
    if (!ptr) return;

    // Debug: verify the pointer is within our backing memory
#ifdef ENGINE_DEBUG_BUILD
    const auto offset = static_cast<uint8_t*>(ptr) - m_memory;
    assert(offset >= 0 && static_cast<size_t>(offset) < m_blockSize * m_totalCount &&
           "PoolAllocator::deallocate: pointer not owned by this pool");
    assert(offset % m_blockSize == 0 &&
           "PoolAllocator::deallocate: pointer is not aligned to block boundary");
#endif

    auto* node = static_cast<FreeNode*>(ptr);
    node->next  = m_freeHead;
    m_freeHead  = node;
    ++m_freeCount;
}

} // namespace hs
