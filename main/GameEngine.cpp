// app/src/main/cpp/main/GameEngine.cpp
#include "GameEngine.h"

// Subsystems
#include "../rendering/VulkanRenderer.h"
#include "../physics/PhysicsWorld.h"
#include "../audio/AudioEngine.h"
#include "../network/NetworkManager.h"
#include "../threading/ThreadPool.h"
#include "../threading/JobSystem.h"
#include "../ecs/EntityManager.h"
#include "../utils/Logger.h"
#include "../utils/Timer.h"

// AGDK frame pacing
#include <swappy/swappyGL.h>        // or swappyVk.h for Vulkan
#include <swappy/swappyVk.h>

// Android
#include <android/choreographer.h>
#include <time.h>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
GameEngine::GameEngine(android_app* app) noexcept
    : m_app(app)
{}

GameEngine::~GameEngine() {
    // Shutdown is called explicitly from android_main; this is a safety net.
    if (m_state.load() != EngineState::Shutdown) {
        shutdown();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool GameEngine::init() {
    LOG_INFO("GameEngine::init() begin");

    // Initialise subsystems in dependency order.
    // Each returns false on failure — we abort immediately.
    if (!initThreading())    { LOG_ERROR("Threading init failed");    return false; }
    if (!initECS())          { LOG_ERROR("ECS init failed");          return false; }
    if (!initAudio())        { LOG_ERROR("Audio init failed");        return false; }
    if (!initPhysics())      { LOG_ERROR("Physics init failed");      return false; }
    if (!initNetworking())   { LOG_ERROR("Networking init failed");   return false; }
    // Renderer last — requires surface (may not be available yet; renderer
    // will defer Vulkan swapchain creation until onSurfaceCreated())
    if (!initRenderer())     { LOG_ERROR("Renderer init failed");     return false; }

    // Seed the frame timer
    m_lastFrameTimeNs = Timer::nowNs();

    m_state.store(EngineState::Initialised, std::memory_order_release);
    LOG_INFO("GameEngine::init() complete");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void GameEngine::tick() {
    const auto state = m_state.load(std::memory_order_acquire);

    if (state == EngineState::Paused || state == EngineState::Uninitialised) {
        return;  // Nothing to do when paused or not yet ready
    }
    if (state == EngineState::ShutdownRequested) {
        // Signal the main loop to exit
        m_app->destroyRequested = 1;
        return;
    }

    m_deltaTime = computeDeltaTime();
    m_frameIndex++;

    // ── Fixed-timestep physics accumulator ───────────────────────────────────
    // Prevents physics from running faster than the game logic on high-end
    // devices and ensures deterministic simulation for multiplayer.
    m_physicsAccum += m_deltaTime;
    while (m_physicsAccum >= m_fixedTimeStep) {
        updatePhysics(m_fixedTimeStep);
        m_physicsAccum -= m_fixedTimeStep;
    }

    // ── Variable-timestep ECS update ─────────────────────────────────────────
    updateECS(m_deltaTime);

    // ── Network send/receive ──────────────────────────────────────────────────
    updateNetwork(m_deltaTime);

    // ── Submit frame for rendering ────────────────────────────────────────────
    // Swappy inside the renderer handles frame pacing and VSync alignment.
    render();
}

// ─────────────────────────────────────────────────────────────────────────────
void GameEngine::shutdown() {
    LOG_INFO("GameEngine::shutdown() begin");
    m_state.store(EngineState::Shutdown, std::memory_order_release);

    // Shutdown in reverse initialisation order to respect dependencies.
    if (m_networkManager)   m_networkManager->shutdown();
    if (m_physicsWorld)     m_physicsWorld->shutdown();
    if (m_audioEngine)      m_audioEngine->shutdown();
    if (m_renderer)         m_renderer->shutdown();
    if (m_entityManager)    m_entityManager->clear();
    if (m_jobSystem)        m_jobSystem->shutdown();
    if (m_threadPool)       m_threadPool->shutdown();

    LOG_INFO("GameEngine::shutdown() complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle handlers
// ─────────────────────────────────────────────────────────────────────────────

void GameEngine::onSurfaceCreated(ANativeWindow* window) {
    LOG_INFO("GameEngine::onSurfaceCreated");
    if (m_renderer) {
        m_renderer->createSurface(window);
    }
    m_state.store(EngineState::Running, std::memory_order_release);
}

void GameEngine::onSurfaceDestroyed() {
    LOG_INFO("GameEngine::onSurfaceDestroyed");
    if (m_renderer) {
        m_renderer->destroySurface();
    }
}

void GameEngine::onResume() {
    LOG_INFO("GameEngine::onResume");
    if (m_audioEngine) m_audioEngine->resume();
    if (m_networkManager) m_networkManager->resume();
    m_state.store(EngineState::Running, std::memory_order_release);
    m_lastFrameTimeNs = Timer::nowNs();   // Reset timer to avoid huge deltaTime
}

void GameEngine::onPause() {
    LOG_INFO("GameEngine::onPause");
    m_state.store(EngineState::Paused, std::memory_order_release);
    if (m_audioEngine) m_audioEngine->pause();
    if (m_networkManager) m_networkManager->pause();
}

void GameEngine::onLowMemory() {
    LOG_WARN("GameEngine::onLowMemory — releasing texture caches");
    if (m_renderer) m_renderer->releaseNonEssentialResources();
}

void GameEngine::onConfigurationChanged() {
    LOG_INFO("GameEngine::onConfigurationChanged");
    if (m_renderer) m_renderer->onWindowResize();
}

void GameEngine::requestShutdown() {
    m_state.store(EngineState::ShutdownRequested, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────────────────────
// Input processing
// ─────────────────────────────────────────────────────────────────────────────

void GameEngine::processInputEvents(android_app* app) {
    // AGDK provides a polling-based input API; no JNI overhead per event.
    android_input_buffer* inputBuffer = android_app_swap_input_buffers(app);
    if (!inputBuffer) return;

    // Touch / pointer events
    for (uint64_t i = 0; i < inputBuffer->motionEventsCount; ++i) {
        const GameActivityMotionEvent& event = inputBuffer->motionEvents[i];
        // TODO: dispatch to InputManager / ECS input component
        (void)event;
    }

    // Key events
    for (uint64_t i = 0; i < inputBuffer->keyEventsCount; ++i) {
        const GameActivityKeyEvent& event = inputBuffer->keyEvents[i];
        (void)event;
    }

    android_app_clear_motion_events(inputBuffer);
    android_app_clear_key_events(inputBuffer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame update stages
// ─────────────────────────────────────────────────────────────────────────────

void GameEngine::updateECS(float deltaTime) {
    if (m_entityManager) {
        m_entityManager->update(deltaTime);
    }
}

void GameEngine::updatePhysics(float deltaTime) {
    if (m_physicsWorld) {
        m_physicsWorld->step(deltaTime);
    }
}

void GameEngine::updateNetwork(float deltaTime) {
    if (m_networkManager) {
        m_networkManager->tick(deltaTime);
    }
}

void GameEngine::render() {
    if (m_renderer && m_renderer->isSurfaceReady()) {
        m_renderer->beginFrame();
        // Systems that need to render submit their draw calls here
        if (m_entityManager) {
            m_entityManager->submitRenderCommands(*m_renderer);
        }
        m_renderer->endFrame();   // Calls SwappyVk_queuePresent internally
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem initialisation
// ─────────────────────────────────────────────────────────────────────────────

bool GameEngine::initThreading() {
    const uint32_t hwThreads = std::thread::hardware_concurrency();
    // Reserve 1 core for the game thread and 1 for audio; use rest for jobs
    const uint32_t workerCount = (hwThreads > 2) ? hwThreads - 2 : 1;
    LOG_INFO("Initialising thread pool: %u workers", workerCount);

    m_threadPool = std::make_unique<ThreadPool>(workerCount);
    m_jobSystem  = std::make_unique<JobSystem>(*m_threadPool);
    return m_threadPool->isRunning() && m_jobSystem->isRunning();
}

bool GameEngine::initECS() {
    m_entityManager = std::make_unique<EntityManager>(*m_jobSystem);
    return m_entityManager->init();
}

bool GameEngine::initAudio() {
    m_audioEngine = std::make_unique<AudioEngine>();
    return m_audioEngine->init(m_app->activity->assetManager);
}

bool GameEngine::initPhysics() {
#ifdef ENGINE_PHYSICS_ENABLED
    m_physicsWorld = std::make_unique<PhysicsWorld>(*m_jobSystem);
    return m_physicsWorld->init();
#else
    LOG_INFO("Physics disabled at compile time");
    return true;
#endif
}

bool GameEngine::initNetworking() {
#if defined(ENGINE_NETWORKING_ENABLED) || defined(ENGINE_NETWORKING_STUB)
    m_networkManager = std::make_unique<NetworkManager>();
    return m_networkManager->init();
#else
    LOG_INFO("Networking disabled at compile time");
    return true;
#endif
}

bool GameEngine::initRenderer() {
    m_renderer = std::make_unique<VulkanRenderer>(m_app);
    // Surface may not be ready yet — renderer will defer swapchain creation
    return m_renderer->initDevice();
}

// ─────────────────────────────────────────────────────────────────────────────
float GameEngine::computeDeltaTime() noexcept {
    const int64_t now       = Timer::nowNs();
    const int64_t elapsed   = now - m_lastFrameTimeNs;
    m_lastFrameTimeNs       = now;

    // Convert nanoseconds to seconds; clamp to prevent physics explosions
    // after a pause (e.g., debugger break, backgrounding)
    constexpr float kMaxDelta = 0.1f;   // 100 ms cap
    const float deltaSeconds = static_cast<float>(elapsed) * 1e-9f;
    return (deltaSeconds < kMaxDelta) ? deltaSeconds : kMaxDelta;
}

} // namespace hs
