// app/src/main/cpp/memory/MemoryTracker.h
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "../utils/Logger.h"

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// MemoryTracker — global thread-safe memory accounting.
//
// Hook into malloc/free wrappers to track:
//   • Total bytes allocated (by subsystem)
//   • Peak allocation (for memory budget planning)
//   • Allocation count (for detecting leaks)
//
// In production, per-category tracking helps identify which subsystem
// is using the most memory (physics bodies, texture data, audio buffers, etc.)
// ─────────────────────────────────────────────────────────────────────────────
enum class MemoryCategory : uint8_t {
    General         = 0,
    ECS             = 1,
    Rendering       = 2,
    Physics         = 3,
    Audio           = 4,
    Networking      = 5,
    AssetLoading    = 6,
    Count
};

class MemoryTracker final {
public:
    static MemoryTracker& instance() noexcept {
        static MemoryTracker tracker;
        return tracker;
    }

    void trackAlloc(MemoryCategory cat, size_t bytes) noexcept {
        const auto idx          = static_cast<int>(cat);
        const size_t prev       = m_allocated[idx].fetch_add(bytes, std::memory_order_relaxed);
        const size_t current    = prev + bytes;
        size_t peak             = m_peak[idx].load(std::memory_order_relaxed);
        while (current > peak &&
               !m_peak[idx].compare_exchange_weak(peak, current,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}

        m_totalAllocated.fetch_add(bytes, std::memory_order_relaxed);
        m_allocCount.fetch_add(1, std::memory_order_relaxed);
    }

    void trackFree(MemoryCategory cat, size_t bytes) noexcept {
        m_allocated[static_cast<int>(cat)].fetch_sub(bytes, std::memory_order_relaxed);
        m_totalAllocated.fetch_sub(bytes, std::memory_order_relaxed);
        m_allocCount.fetch_sub(1, std::memory_order_relaxed);
    }

    void report() const noexcept {
        LOG_INFO("=== Memory Report ===");
        LOG_INFO("  Total live: %zu KB (peak: %zu KB)",
                 m_totalAllocated.load() / 1024,
                 m_globalPeak.load() / 1024);
        LOG_INFO("  Live alloc count: %zu", m_allocCount.load());

        static constexpr const char* kNames[] = {
            "General", "ECS", "Rendering", "Physics",
            "Audio", "Networking", "AssetLoading"
        };
        for (int i = 0; i < static_cast<int>(MemoryCategory::Count); ++i) {
            const size_t bytes = m_allocated[i].load();
            if (bytes > 0) {
                LOG_INFO("  [%s]: %zu KB (peak: %zu KB)",
                         kNames[i], bytes / 1024, m_peak[i].load() / 1024);
            }
        }
        LOG_INFO("=====================");
    }

private:
    MemoryTracker() = default;

    static constexpr int kCatCount = static_cast<int>(MemoryCategory::Count);
    std::atomic<size_t> m_allocated[kCatCount]  {};
    std::atomic<size_t> m_peak[kCatCount]       {};
    std::atomic<size_t> m_totalAllocated        { 0 };
    std::atomic<size_t> m_globalPeak            { 0 };
    std::atomic<size_t> m_allocCount            { 0 };
};

// Convenience macros for tracked allocations
#define HS_ALLOC(cat, size)     \
    ([&]{ auto* p = malloc(size); \
          hs::MemoryTracker::instance().trackAlloc(hs::MemoryCategory::cat, (size)); \
          return p; }())

#define HS_FREE(cat, ptr, size) \
    do { hs::MemoryTracker::instance().trackFree(hs::MemoryCategory::cat, (size)); \
         free(ptr); } while(0)

} // namespace hs
