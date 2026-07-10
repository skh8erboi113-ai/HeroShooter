// app/src/main/cpp/utils/Logger.h
#pragma once

#include <android/log.h>

// ─────────────────────────────────────────────────────────────────────────────
// Logging macros — zero overhead in release builds (LOG_DEBUG stripped).
// All macros produce a single call to __android_log_print which is efficient
// because the Android log daemon batches writes.
//
// Usage:
//   LOG_INFO("Entity %u spawned at (%.2f, %.2f, %.2f)", id, x, y, z);
// ─────────────────────────────────────────────────────────────────────────────

#define HS_LOG_TAG "HeroShooterEngine"

#define LOG_INFO(fmt, ...)  \
    __android_log_print(ANDROID_LOG_INFO,  HS_LOG_TAG, "[INFO]  " fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)  \
    __android_log_print(ANDROID_LOG_WARN,  HS_LOG_TAG, "[WARN]  " fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    __android_log_print(ANDROID_LOG_ERROR, HS_LOG_TAG, "[ERROR] " fmt, ##__VA_ARGS__)

// Debug logging stripped in release builds
#ifdef ENGINE_DEBUG_BUILD
    #define LOG_DEBUG(fmt, ...) \
        __android_log_print(ANDROID_LOG_DEBUG, HS_LOG_TAG, "[DEBUG] " fmt, ##__VA_ARGS__)

    // Assertion with message
    #define HS_ASSERT(cond, fmt, ...)                                       \
        do {                                                                \
            if (!(cond)) {                                                  \
                LOG_ERROR("ASSERT FAILED: " #cond " — " fmt, ##__VA_ARGS__); \
                __builtin_trap();                                           \
            }                                                               \
        } while(0)
#else
    #define LOG_DEBUG(fmt, ...)    ((void)0)
    #define HS_ASSERT(cond, ...)   ((void)(cond))
#endif
