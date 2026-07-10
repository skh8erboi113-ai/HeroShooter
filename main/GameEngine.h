// app/src/main/cpp/main/GameEngine.h
#pragma once

// AGDK
#include <game-activity/native_app_glue/android_native_app_glue.h>

// Engine subsystems (forward declarations to minimise compile time)
namespace hs {
    class VulkanRenderer;
    class PhysicsWorld;
    class AudioEngine;
    class NetworkManager;
    class ThreadPool;
    class JobSystem;
    class EntityManager;
}

#include <atomic>
#include <memory>
#include <cstdint>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Engine state machine
// Transitions: UNINITIALISED → INITIALISED → RUNNING ↔ PAUSED → SHUTDOWN
// ─────────────────────────────────────────────────────────────────────────────
enum class EngineState : uint8_t {
    Uninitialised,
    Initialised,
    Running,
    Paused,
    ShutdownRequested,
    Shutdown
};

// ─────────────────────────────────────────────────────────────────────────────
// GameEngine — top-level orchestrator.
// Owns all subsystems and drives the per-frame update cycle.
// Not copyable or movable (subsystems hold back-pointers to it).
// ─────────────────────────────────────────────────────────────────────────────
class GameEngine final {
public:
    explicit GameEngine(android_app* app) noexcept;
    ~GameEngine();

    GameEngine(const GameEngine&)            = delete;
    GameEngine& operator=(const GameEngine&) = delete;
    GameEngine(GameEngine&&)                 = delete;
    GameEngine& operator=(GameEngine&&)      = delete;

    // ── Lifecycle (called from android_main on game thread) ───────────────────
    [[nodiscard]] bool init();
    void tick();
    void shutdown();

    // ── Lifecycle events (called from onAppCmd) ───────────────────────────────
    void onSurfaceCreated(ANativeWindow* window);
    void onSurfaceDestroyed();
    void onResume();
    void onPause();
    void onLowMemory();
    void onConfigurationChanged();
    void requestShutdown();

    // ── Input (called from android_main game loop) ────────────────────────────
    void processInputEvents(android_app* app);

    // ── State queries ─────────────────────────────────────────────────────────
    [[nodiscard]] bool isRunning() const noexcept {
        return m_state.load(std::memory_order_acquire) == EngineState::Running;
    }

private:
    // ── Initialisation helpers ────────────────────────────────────────────────
    [[nodiscard]] bool initRenderer();
    [[nodiscard]] bool initPhysics();
    [[nodiscard]] bool initAudio();
    [[nodiscard]] bool initNetworking();
    [[nodiscard]] bool initECS();
    [[nodiscard]] bool initThreading();

    // ── Per-frame update stages ───────────────────────────────────────────────
    void updateInput(float deltaTime);
    void updateECS(float deltaTime);
    void updatePhysics(float deltaTime);
    void updateNetwork(float deltaTime);
    void render();

    // ── Timing ────────────────────────────────────────────────────────────────
    [[nodiscard]] float computeDeltaTime() noexcept;

private:
    android_app*                        m_app           = nullptr;
    std::atomic<EngineState>            m_state         { EngineState::Uninitialised };

    // Subsystems — unique_ptr for clear ownership and RAII shutdown ordering
    std::unique_ptr<ThreadPool>         m_threadPool;
    std::unique_ptr<JobSystem>          m_jobSystem;
    std::unique_ptr<EntityManager>      m_entityManager;
    std::unique_ptr<VulkanRenderer>     m_renderer;
    std::unique_ptr<PhysicsWorld>       m_physicsWorld;
    std::unique_ptr<AudioEngine>        m_audioEngine;
    std::unique_ptr<NetworkManager>     m_networkManager;

    // Timing state
    int64_t     m_lastFrameTimeNs  = 0;
    float       m_deltaTime        = 0.0f;
    float       m_fixedTimeStep    = 1.0f / 60.0f;  // 60 Hz physics
    float       m_physicsAccum     = 0.0f;           // Accumulated physics time
    uint64_t    m_frameIndex       = 0;
};

} // namespace hs
