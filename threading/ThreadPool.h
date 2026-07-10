// app/src/main/cpp/threading/ThreadPool.h
#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// ThreadPool
//
// Fixed-size pool of worker threads for background jobs.
// Jobs are std::function<void()> — no allocation overhead for lambdas that
// capture small values (SSO). For frequent small jobs, use JobSystem instead
// which uses a lock-free queue.
//
// Thread affinity: On ARM64 devices with big.LITTLE, we request that worker
// threads are pinned to "little" (efficiency) cores to leave "big" cores for
// the game thread and audio thread. This is done via Android-specific APIs.
// ─────────────────────────────────────────────────────────────────────────────
class ThreadPool final {
public:
    using Job = std::function<void()>;

    explicit ThreadPool(uint32_t workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a job; returns immediately. Job runs on an available worker.
    void submit(Job job);

    // Block the calling thread until all queued jobs have completed.
    void waitIdle();

    void shutdown();

    [[nodiscard]] bool     isRunning()   const noexcept { return m_running.load(); }
    [[nodiscard]] uint32_t workerCount() const noexcept {
        return static_cast<uint32_t>(m_workers.size());
    }
    [[nodiscard]] size_t   pendingJobs() const noexcept;

private:
    void workerLoop(uint32_t threadIndex);

    std::vector<std::thread>        m_workers;
    std::queue<Job>                 m_jobQueue;
    mutable std::mutex              m_queueMutex;
    std::condition_variable         m_condition;
    std::condition_variable         m_idleCondition;
    std::atomic<bool>               m_running   { true };
    std::atomic<uint32_t>           m_activeJobs{ 0 };
};

} // namespace hs
