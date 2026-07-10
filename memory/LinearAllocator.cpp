// app/src/main/cpp/memory/LinearAllocator.cpp
#include "LinearAllocator.h"
#include "../utils/Logger.h"

#include <cstdlib>
#include <algorithm>

namespace hs {

LinearAllocator::LinearAllocator(size_t capacity) : m_capacity(capacity) {
    // 64-byte alignment for the base address — ensures any sub-allocation
    // with alignment <= 64 bytes is trivially aligned.
    m_memory = static_cast<uint8_t*>(aligned_alloc(64, capacity));
    if (!m_memory) {
        LOG_ERROR("LinearAllocator: failed to allocate %zu bytes", capacity);
        m_capacity = 0;
    } else {
        LOG_INFO("LinearAllocator: created %zu KB arena",  capacity / 1024);
    }
}

LinearAllocator::~LinearAllocator() {
    if (m_cursor > 0) {
        LOG_WARN("LinearAllocator: destroyed with %zu bytes allocated "
                 "(peak=%zu bytes)", m_cursor, m_peakUsage);
    }
    free(m_memory);
}

// ─────────────────────────────────────────────────────────────────────────────
void* LinearAllocator::allocate(size_t size, size_t alignment) noexcept {
    if (!m_memory || size == 0) return nullptr;

    // Align the cursor up to the requested alignment
    // alignment must be a power of 2
    const size_t alignedCursor = (m_cursor + alignment - 1) & ~(alignment - 1);
    const size_t newCursor     = alignedCursor + size;

    if (newCursor > m_capacity) {
        LOG_ERROR("LinearAllocator: out of memory "
                  "(requested %zu, used %zu/%zu)",
                  size, m_cursor, m_capacity);
        return nullptr;
    }

    m_cursor     = newCursor;
    m_peakUsage  = std::max(m_peakUsage, m_cursor);

    return m_memory + alignedCursor;
}

void LinearAllocator::reset() noexcept {
    m_cursor = 0;
    // Optional: poison the memory in debug builds to detect use-after-free
#ifdef ENGINE_DEBUG_BUILD
    __builtin_memset(m_memory, 0xCD, m_capacity);
#endif
}

} // namespace hs
