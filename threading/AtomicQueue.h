// app/src/main/cpp/threading/AtomicQueue.h
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <optional>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// AtomicQueue — lock-free, single-producer single-consumer (SPSC) ring buffer.
//
// Used for:
//   • Audio → Game thread: sound completion events
//   • Network → Game thread: received packet notifications
//   • Physics → Game thread: collision events (body ID pairs)
//
// Capacity must be a power of 2 for the index masking optimisation.
// T must be trivially copyable (no locks in push/pop).
//
// Implementation: uses a head/tail index pair with acquire/release ordering.
// No CAS loops — suitable for embedded/mobile where CAS failure is expensive.
// ─────────────────────────────────────────────────────────────────────────────
template<typename T, uint32_t Capacity>
class AtomicQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
        "AtomicQueue capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>,
        "AtomicQueue requires trivially copyable T");

public:
    AtomicQueue() noexcept : m_head(0), m_tail(0) {}

    // Push from the PRODUCER thread. Returns false if full.
    [[nodiscard]] bool push(const T& item) noexcept {
        const uint32_t tail     = m_tail.load(std::memory_order_relaxed);
        const uint32_t nextTail = (tail + 1) & kMask;

        if (nextTail == m_head.load(std::memory_order_acquire)) {
            return false;   // Queue full
        }

        m_buffer[tail] = item;
        m_tail.store(nextTail, std::memory_order_release);
        return true;
    }

    // Pop from the CONSUMER thread. Returns empty optional if queue is empty.
    [[nodiscard]] std::optional<T> pop() noexcept {
        const uint32_t head = m_head.load(std::memory_order_relaxed);

        if (head == m_tail.load(std::memory_order_acquire)) {
            return std::nullopt;    // Queue empty
        }

        T item = m_buffer[head];
        m_head.store((head + 1) & kMask, std::memory_order_release);
        return item;
    }

    [[nodiscard]] bool         isEmpty() const noexcept {
        return m_head.load(std::memory_order_acquire)
            == m_tail.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t size() const noexcept {
        const uint32_t tail = m_tail.load(std::memory_order_acquire);
        const uint32_t head = m_head.load(std::memory_order_acquire);
        return (tail - head) & kMask;
    }

private:
    static constexpr uint32_t kMask = Capacity - 1;

    alignas(64) std::atomic<uint32_t> m_head;   // Consumer index (own cache line)
    alignas(64) std::atomic<uint32_t> m_tail;   // Producer index (own cache line)

    // Buffer — padded to cache line size to avoid false sharing with indices
    alignas(64) std::array<T, Capacity> m_buffer {};
};

// ─────────────────────────────────────────────────────────────────────────────
// Collision event for physics → game thread communication
// ─────────────────────────────────────────────────────────────────────────────
struct CollisionEvent {
    uint32_t bodyIdA;
    uint32_t bodyIdB;
    float    impulse;       // Contact impulse magnitude (for damage calc)
    float    normalX, normalY, normalZ; // Contact normal in world space
};

// Pre-defined queues for inter-thread communication
using CollisionEventQueue = AtomicQueue<CollisionEvent, 256>;

} // namespace hs
