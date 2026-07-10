// app/src/main/cpp/threading/JobSystem.cpp
#include "JobSystem.h"
#include "../utils/Logger.h"

#include <thread>
#include <vector>

namespace hs {

JobSystem::JobSystem(ThreadPool& pool) noexcept
    : m_pool(pool)
{}

// ─────────────────────────────────────────────────────────────────────────────
JobHandle JobSystem::schedule(Task task) {
    // Allocate a shared completion flag
    auto state = std::make_shared<std::atomic<bool>>(false);

    m_pool.submit([task = std::move(task), state]() mutable {
        task();
        // Signal completion — all threads waiting on this handle will unblock
        state->store(true, std::memory_order_release);
    });

    return JobHandle(std::move(state));
}

// ─────────────────────────────────────────────────────────────────────────────
void JobSystem::parallelFor(
    uint32_t                                count,
    uint32_t                                chunkSize,
    std::function<void(uint32_t, uint32_t)> func)
{
    if (count == 0) return;

    // Compute number of chunks
    const uint32_t numChunks = (count + chunkSize - 1) / chunkSize;

    // Shared atomic counter — decremented by each worker on completion
    auto remaining = std::make_shared<std::atomic<uint32_t>>(numChunks);
    auto allDone   = std::make_shared<std::atomic<bool>>(false);

    for (uint32_t chunk = 0; chunk < numChunks; ++chunk) {
        const uint32_t start = chunk * chunkSize;
        const uint32_t end   = std::min(start + chunkSize, count);

        m_pool.submit([func, start, end, remaining, allDone]() {
            func(start, end);
            if (remaining->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                // Last chunk completed
                allDone->store(true, std::memory_order_release);
            }
        });
    }

    // Spin-wait until all chunks complete
    // This blocks the CALLING thread (game thread), which is intentional —
    // we want the ECS update to finish before the physics step starts.
    uint32_t spins = 0;
    while (!allDone->load(std::memory_order_acquire)) {
        if (++spins > 10000) {
            std::this_thread::yield();
            spins = 0;
        }
    }
}

void JobSystem::waitAll() {
    m_pool.waitIdle();
}

} // namespace hs
