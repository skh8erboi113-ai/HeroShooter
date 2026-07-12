// app/src/main/cpp/main/GameEngine.h
#pragma once

#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "../states/GameStateManager.h"

#include <atomic>
#include <memory>
#include <cstdint>

namespace hs {

class VulkanRenderer;
class PhysicsWorld;
class AudioEngine;
class NetworkManager;
class ThreadPool;
class JobSystem;
class EntityManager;
class InputManager;
class AssetManager;
class ShaderManager;
class DebugRenderer;

enum class EngineState : uint8_t {
    Uninitialised,
    Initialised,
    Running,
    Paused,
    ShutdownRequested,
    Shutdown
};

class GameEngine final {
public:
    explicit GameEngine(android_app* app) noexcept;
    ~GameEngine();

    GameEngine(const GameEngine&)            = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    [[nodiscard]] bool init();
    void tick();
    void shutdown();

    void onSurfaceCreated(ANativeWindow* window);
    void onSurfaceDestroyed();
    void onResume();
    void onPause();
    void onLowMemory();
    void onConfigurationChanged();
    void requestShutdown();

    void processInputEvents(android_app* app);

    // ── Subsystem accessors (for GameState access) ─────────────────────────
    [[nodiscard]] EntityManager&    entityManager()  noexcept { return *m_entityManager; }
    [[nodiscard]] VulkanRenderer&   renderer()       noexcept { return *m_renderer;      }
    [[nodiscard]] PhysicsWorld&     physicsWorld()   noexcept { return *m_physicsWorld;  }
    [[nodiscard]] AudioEngine&      audioEngine()    noexcept { return *m_audioEngine;   }
    [[nodiscard]] NetworkManager&   networkManager() noexcept { return *m_networkManager;}
    [[nodiscard]] InputManager&     inputManager()   noexcept { return *m_inputManager;  }
    [[nodiscard]] AssetManager&     assetManager()   noexcept { return *m_assetManager;  }
    [[nodiscard]] ShaderManager&    shaderManager()  noexcept { return *m_shaderManager; }
    [[nodiscard]] GameStateManager& stateManager()   noexcept { return m_stateManager;   }
    [[nodiscard]] JobSystem&        jobSystem()      noexcept { return *m_jobSystem;      }

    // ── State queries ──────────────────────────────────────────────────────
    [[nodiscard]] bool isRunning() const noexcept {
        return m_state.load(std::memory_order_acquire) == EngineState::Running;
    }
    [[nodiscard]] uint64_t frameIndex() const noexcept { return m_frameIndex; }
    [[nodiscard]] float    engineTime()  const noexcept { return m_engineTime; }

private:
    [[nodiscard]] bool initRenderer();
    [[nodiscard]] bool initPhysics();
    [[nodiscard]] bool initAudio();
    [[nodiscard]] bool initNetworking();
    [[nodiscard]] bool initECS();
    [[nodiscard]] bool initThreading();
    [[nodiscard]] bool initInput();
    [[nodiscard]] bool initAssets();

    void updateInput();
    void updateECS(float deltaTime);
    void updatePhysics(float deltaTime);
    void updateNetwork(float deltaTime);
    void updateStates(float deltaTime);
    void render();

    [[nodiscard]] float computeDeltaTime() noexcept;

private:
    android_app*    m_app       = nullptr;
    std::atomic<EngineState> m_state { EngineState::Uninitialised };

    // ── Subsystems (order = initialisation order) ─────────────────────────
    std::unique_ptr<ThreadPool>     m_threadPool;
    std::unique_ptr<JobSystem>      m_jobSystem;
    std::unique_ptr<InputManager>   m_inputManager;
    std::unique_ptr<ShaderManager>  m_shaderManager;
    std::unique_ptr<EntityManager>  m_entityManager;
    std::unique_ptr<VulkanRenderer> m_renderer;
    std::unique_ptr<AssetManager>   m_assetManager;
    std::unique_ptr<PhysicsWorld>   m_physicsWorld;
    std::unique_ptr<AudioEngine>    m_audioEngine;
    std::unique_ptr<NetworkManager> m_networkManager;

#ifdef ENGINE_DEBUG_BUILD
    std::unique_ptr<DebugRenderer>  m_debugRenderer;
#endif

    GameStateManager    m_stateManager;

    // ── Timing ────────────────────────────────────────────────────────────
    int64_t     m_lastFrameTimeNs   = 0;
    float       m_deltaTime         = 0.0f;
    float       m_engineTime        = 0.0f;
    float       m_fixedTimeStep     = 1.0f / 60.0f;
    float       m_physicsAccum      = 0.0f;
    uint64_t    m_frameIndex        = 0;

    // ── Performance stats ─────────────────────────────────────────────────
    float       m_statsTimer        = 0.0f;
    float       m_avgFrameTimeMs    = 0.0f;
    static constexpr float kStatsInterval = 2.0f; // Log stats every 2 seconds
};

} // namespace hs
