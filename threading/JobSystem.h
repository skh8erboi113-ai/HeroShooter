// app/src/main/cpp/threading/JobSystem.h
#pragma once

#include "ThreadPool.h"
#include "AtomicQueue.h"

#include <functional>
#include <atomic>
#include <memory>
#include <cstdint>
#include <vector>
#include <span>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// JobHandle — reference-counted handle to a pending or completed job.
// The caller can .wait() on the handle to synchronise with job completion.
// ─────────────────────────────────────────────────────────────────────────────
class JobHandle {
public:
    JobHandle() noexcept = default;

    // Blocks until the job is complete.
    // Uses a spin-wait with exponential backoff — optimal for <1ms jobs.
    void wait() const noexcept {
        if (!m_state) return;
        uint32_t spins = 0;
        while (!m_state->load(std::memory_order_acquire)) {
            if (++spins > 1000) {
                // After 1000 spins (~1µs), yield to avoid wasting a core
                std::this_thread::yield();
                spins = 0;
            }
        }
    }

    [[nodiscard]] bool isComplete() const noexcept {
        return !m_state || m_state->load(std::memory_order_acquire);
    }

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<std::atomic<bool>> state) noexcept
        : m_state(std::move(state)) {}

    std::shared_ptr<std::atomic<bool>> m_state;
};

// ─────────────────────────────────────────────────────────────────────────────
// JobSystem
//
// High-level job scheduling layer on top of ThreadPool.
// Provides:
//   • Single jobs with completion tracking (JobHandle)
//   • Parallel-for: splits a range into N equal chunks, one per worker
//   • Job chaining: start job B only when job A is complete (via dependencies)
//
// The parallel-for pattern is the primary tool for ECS system parallelism:
//   jobSystem.parallelFor(entities, workerCount,
//       [](std::span<const entt::entity> chunk) {
//           for (auto e : chunk) processEntity(e);
//       });
// ─────────────────────────────────────────────────────────────────────────────
class JobSystem final {
public:
    using Task = std::function<void()>;

    explicit JobSystem(ThreadPool& pool) noexcept;
    ~JobSystem() = default;

    [[nodiscard]] bool isRunning() const noexcept { return m_pool.isRunning(); }
    void shutdown() {}  // Pool shutdown handles cleanup

    // ── Single job ─────────────────────────────────────────────────────────
    [[nodiscard]] JobHandle schedule(Task task);

    // ── Parallel-for ───────────────────────────────────────────────────────
    // Splits [0, count) into chunks and runs each on a worker.
    // Blocks until all chunks are complete.
    //
    // Example:
    //   parallelFor(0, entityCount, 64,
    //       [&](uint32_t start, uint32_t end) {
    //           for (uint32_t i = start; i < end; ++i) { ... }
    //       });
    void parallelFor(
        uint32_t                                    count,
        uint32_t                                    chunkSize,
        std::function<void(uint32_t, uint32_t)>     func
    );

    // Waits for all scheduled jobs to complete
    void waitAll();

private:
    ThreadPool& m_pool;
};

} // namespace hs
