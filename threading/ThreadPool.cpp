// app/src/main/cpp/threading/ThreadPool.cpp
#include "ThreadPool.h"
#include "../utils/Logger.h"

#include <pthread.h>    // For thread naming (Android)
#include <sched.h>      // For CPU affinity

namespace hs {

ThreadPool::ThreadPool(uint32_t workerCount) {
    LOG_INFO("ThreadPool: spawning %u worker threads", workerCount);

    m_workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this, i] { workerLoop(i); });

        // Name the thread for Android Studio profiler visibility
        char name[16];
        snprintf(name, sizeof(name), "hs-worker-%u", i);
        pthread_setname_np(m_workers.back().native_handle(), name);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(Job job) {
    {
        std::lock_guard lock(m_queueMutex);
        m_jobQueue.push(std::move(job));
    }
    m_condition.notify_one();
}

void ThreadPool::waitIdle() {
    std::unique_lock lock(m_queueMutex);
    m_idleCondition.wait(lock, [this] {
        return m_jobQueue.empty() && m_activeJobs.load() == 0;
    });
}

size_t ThreadPool::pendingJobs() const noexcept {
    std::lock_guard lock(m_queueMutex);
    return m_jobQueue.size();
}

void ThreadPool::shutdown() {
    if (!m_running.exchange(false)) return;  // Already shut down

    m_condition.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
    LOG_INFO("ThreadPool: all workers joined");
}

void ThreadPool::workerLoop(uint32_t threadIndex) {
    LOG_INFO("ThreadPool: worker %u started", threadIndex);

    while (true) {
        Job job;
        {
            std::unique_lock lock(m_queueMutex);
            m_condition.wait(lock, [this] {
                return !m_jobQueue.empty() || !m_running.load();
            });

            if (!m_running.load() && m_jobQueue.empty()) {
                LOG_INFO("ThreadPool: worker %u exiting", threadIndex);
                return;
            }

            job = std::move(m_jobQueue.front());
            m_jobQueue.pop();
            m_activeJobs.fetch_add(1, std::memory_order_relaxed);
        }

        // Execute the job outside the lock
        job();

        const uint32_t remaining = m_activeJobs.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            // Wake up any thread waiting in waitIdle()
            std::lock_guard lock(m_queueMutex);
            m_idleCondition.notify_all();
        }
    }
}

} // namespace hs
