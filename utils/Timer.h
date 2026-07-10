// app/src/main/cpp/utils/Timer.h
#pragma once

#include <cstdint>
#include <time.h>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Timer — nanosecond-precision wall clock using CLOCK_BOOTTIME.
//
// CLOCK_BOOTTIME is preferred over CLOCK_MONOTONIC for game timing because:
//   • It includes time during device sleep (safe for session timers)
//   • It is not affected by NTP adjustments (monotonic)
//   • It is the recommended clock for NDK game development
// ─────────────────────────────────────────────────────────────────────────────
class Timer {
public:
    // Returns current time in nanoseconds since an arbitrary epoch
    [[nodiscard]] static int64_t nowNs() noexcept {
        timespec ts;
        clock_gettime(CLOCK_BOOTTIME, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
    }

    // Returns elapsed time since construction in seconds
    [[nodiscard]] float elapsedSeconds() const noexcept {
        return static_cast<float>(nowNs() - m_startNs) * 1e-9f;
    }

    void reset() noexcept { m_startNs = nowNs(); }

    explicit Timer() noexcept : m_startNs(nowNs()) {}

private:
    int64_t m_startNs;
};

} // namespace hs
