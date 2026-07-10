// app/src/main/cpp/main/android_main.cpp
//
// Entry point for the AGDK GameActivity.
// android_main() replaces JNI_OnLoad / NativeActivity's ANativeActivity_onCreate.
// GameActivity handles the heavy lifting of Java ↔ native lifecycle bridging.
//
// Threading model:
//   • android_main() runs on a dedicated "game thread" created by GameActivity.
//   • Rendering runs on the render thread (spawned by GameEngine).
//   • Physics & networking run on the thread pool.
//   • Audio runs on Oboe's high-priority thread.
//
// All lifecycle transitions (pause/resume/surface created/destroyed) are
// serialised through the GameEngine state machine to prevent race conditions.

#include "GameEngine.h"
#include "../utils/Logger.h"

// AGDK GameActivity headers
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

// C++20 standard headers (exceptions disabled — use error codes)
#include <atomic>
#include <cassert>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Input event filter — called on the game thread before events are queued.
// Return true to consume the event (don't forward to Java), false to pass through.
// ─────────────────────────────────────────────────────────────────────────────
static bool onTouchEvent(GameActivityMotionEvent* event) {
    // We handle all touch events natively
    return true;
}

static bool onKeyEvent(GameActivityKeyEvent* event) {
    // Forward hardware back button to engine; consume everything else
    if (event->keyCode == AKEYCODE_BACK) {
        return false;   // Let Java handle back navigation
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Android lifecycle command handler.
// Called on the game thread when the system sends lifecycle events.
// ─────────────────────────────────────────────────────────────────────────────
static void onAppCmd(android_app* app, int32_t cmd) {
    // The GameEngine pointer is stored in userData during engine initialisation
    auto* engine = reinterpret_cast<hs::GameEngine*>(app->userData);
    if (!engine) return;

    switch (cmd) {
        // Surface available — create or restore Vulkan swapchain
        case APP_CMD_INIT_WINDOW:
            LOG_INFO("Lifecycle: APP_CMD_INIT_WINDOW");
            engine->onSurfaceCreated(app->window);
            break;

        // Surface destroyed — destroy Vulkan swapchain but keep device alive
        case APP_CMD_TERM_WINDOW:
            LOG_INFO("Lifecycle: APP_CMD_TERM_WINDOW");
            engine->onSurfaceDestroyed();
            break;

        // App gained focus — resume rendering and game loop
        case APP_CMD_GAINED_FOCUS:
            LOG_INFO("Lifecycle: APP_CMD_GAINED_FOCUS");
            engine->onResume();
            break;

        // App lost focus — pause rendering, throttle CPU/GPU
        case APP_CMD_LOST_FOCUS:
            LOG_INFO("Lifecycle: APP_CMD_LOST_FOCUS");
            engine->onPause();
            break;

        // Low memory — release non-essential GPU and CPU resources
        case APP_CMD_LOW_MEMORY:
            LOG_WARN("Lifecycle: APP_CMD_LOW_MEMORY — releasing caches");
            engine->onLowMemory();
            break;

        // Configuration changed (orientation, DPI) — resize swapchain
        case APP_CMD_CONFIG_CHANGED:
            LOG_INFO("Lifecycle: APP_CMD_CONFIG_CHANGED");
            engine->onConfigurationChanged();
            break;

        case APP_CMD_DESTROY:
            LOG_INFO("Lifecycle: APP_CMD_DESTROY");
            engine->requestShutdown();
            break;

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// android_main — the C entry point for the game thread.
// GameActivity calls this on a dedicated thread after JNI setup is complete.
// ─────────────────────────────────────────────────────────────────────────────
void android_main(android_app* app) {
    LOG_INFO("======================================");
    LOG_INFO("  HeroShooterEngine starting up       ");
    LOG_INFO("  API Level: %d", app->activity->sdkVersion);
    LOG_INFO("======================================");

    // ── Configure input event filters ────────────────────────────────────────
    // AGDK polls and filters input on the game thread — no Java overhead
    android_app_set_motion_event_filter(app, onTouchEvent);
    android_app_set_key_event_filter(app,   onKeyEvent);

    // ── Attach lifecycle command handler ─────────────────────────────────────
    app->onAppCmd = onAppCmd;

    // ── Construct the engine ─────────────────────────────────────────────────
    // Engine is stack-allocated here; it manages all subsystem lifetimes.
    hs::GameEngine engine(app);
    app->userData = &engine;

    // ── Initialise all subsystems ─────────────────────────────────────────────
    if (!engine.init()) {
        LOG_ERROR("Engine initialisation failed — aborting");
        return;
    }

    LOG_INFO("Engine initialised — entering main loop");

    // ── Main game loop ────────────────────────────────────────────────────────
    // The loop runs on this thread.
    // Swappy manages frame pacing; the engine drives ECS, physics, networking.
    while (!app->destroyRequested) {
        // Process all pending OS events (lifecycle, input) before game tick
        // android_app_poll_source is non-blocking (timeout = 0 when rendering)
        int events        = 0;
        android_poll_source* source = nullptr;

        // Poll with zero timeout when the engine is active so we never block
        // the render loop. Use a positive timeout when paused to save battery.
        const int timeoutMs = engine.isRunning() ? 0 : 50;

        while (ALooper_pollAll(timeoutMs, nullptr, &events,
                               reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
        }

        // Consume pending input events accumulated since last frame
        engine.processInputEvents(app);

        // Tick the game: ECS update → physics step → render
        engine.tick();
    }

    LOG_INFO("Main loop exited — shutting down engine");
    engine.shutdown();
    LOG_INFO("Engine shutdown complete");
}
