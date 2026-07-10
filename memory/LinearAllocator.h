// app/src/main/cpp/memory/LinearAllocator.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// LinearAllocator (Arena Allocator)
//
// Bump-pointer allocator for short-lived, per-frame allocations.
// Examples: temporary mesh data during loading, render command lists,
// per-frame physics debug geometry.
//
// Properties:
//   • O(1) allocation: just a pointer bump
//   • O(1) free: reset the entire arena (all at once)
//   • No individual deallocation — all or nothing
//   • Zero fragmentation
//   • Thread-unsafe: use per-thread arenas
//
// Usage pattern:
//   auto* arena = new LinearAllocator(4 * 1024 * 1024); // 4MB arena
//   void* tempData = arena->allocate(sizeof(MyStruct), alignof(MyStruct));
//   // ... use tempData ...
//   arena->reset(); // Free everything at once (per-frame boundary)
// ─────────────────────────────────────────────────────────────────────────────
class LinearAllocator final {
public:
    // capacity: total arena size in bytes
    explicit LinearAllocator(size_t capacity);
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator&)            = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    // Allocate 'size' bytes with 'alignment' byte alignment.
    // Returns nullptr only if capacity is exceeded.
    [[nodiscard]] void* allocate(size_t size, size_t alignment = 16) noexcept;

    // Typed allocation helper — constructs T in-place with placement new.
    // Returns nullptr if allocation fails.
    template<typename T, typename... Args>
    [[nodiscard]] T* emplace(Args&&... args) noexcept {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new(ptr) T(std::forward<Args>(args)...);
        // Note: destructor NOT called on reset() — use only for POD types
        // or types with trivial destructors.
    }

    // Free all allocations at once (O(1), just resets the cursor)
    void reset() noexcept;

    // ── Diagnostics ───────────────────────────────────────────────────────
    [[nodiscard]] size_t used()      const noexcept { return m_cursor; }
    [[nodiscard]] size_t remaining() const noexcept { return m_capacity - m_cursor; }
    [[nodiscard]] size_t capacity()  const noexcept { return m_capacity; }
    [[nodiscard]] float  fillRatio() const noexcept {
        return static_cast<float>(m_cursor) / static_cast<float>(m_capacity);
    }

    // Watermark: highest 'used' value seen since construction
    [[nodiscard]] size_t peakUsage() const noexcept { return m_peakUsage; }

private:
    uint8_t*    m_memory    = nullptr;
    size_t      m_capacity  = 0;
    size_t      m_cursor    = 0;
    size_t      m_peakUsage = 0;
};

} // namespace hs
