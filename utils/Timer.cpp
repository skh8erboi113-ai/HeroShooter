// app/src/main/cpp/utils/Timer.cpp
#include "Timer.h"
#include "Logger.h"

// Android-specific: GPU timing via EXT_disjoint_timer_query
// (Available on most Adreno/Mali GPUs with OpenGL ES 3.2)
#ifdef ENGINE_PROFILING_ENABLED

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// ScopedCPUTimer — RAII timer that logs elapsed time on destruction.
// Usage:
//   { ScopedCPUTimer t("PhysicsStep"); physics.step(dt); }
//   // Logs: "[PERF] PhysicsStep: 1.23 ms"
// ─────────────────────────────────────────────────────────────────────────────
ScopedCPUTimer::ScopedCPUTimer(const char* label) noexcept
    : m_label(label), m_start(Timer::nowNs())
{}

ScopedCPUTimer::~ScopedCPUTimer() {
    const int64_t elapsed = Timer::nowNs() - m_start;
    LOG_INFO("[PERF] %s: %.3f ms", m_label,
             static_cast<float>(elapsed) * 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// FrameStats — running average of frame timing over N frames
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t kFrameHistorySize = 120;

static float  s_frameTimeHistory[kFrameHistorySize] {};
static uint32_t s_frameHistoryIdx = 0;

void FrameStats::recordFrame(float deltaTimeSeconds) noexcept {
    s_frameTimeHistory[s_frameHistoryIdx] = deltaTimeSeconds;
    s_frameHistoryIdx = (s_frameHistoryIdx + 1) % kFrameHistorySize;
}

float FrameStats::averageFrameTimeMs() noexcept {
    float sum = 0.0f;
    for (float t : s_frameTimeHistory) sum += t;
    return (sum / kFrameHistorySize) * 1000.0f;
}

float FrameStats::averageFPS() noexcept {
    const float avgMs = averageFrameTimeMs();
    return (avgMs > 0.0f) ? 1000.0f / avgMs : 0.0f;
}

} // namespace hs

#endif // ENGINE_PROFILING_ENABLED
