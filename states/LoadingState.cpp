// app/src/main/cpp/states/LoadingState.cpp
#include "LoadingState.h"
#include "../main/GameEngine.h"
#include "../rendering/VulkanRenderer.h"
#include "../rendering/ShaderManager.h"
#include "../utils/Logger.h"

namespace hs {

LoadingState::LoadingState(GameEngine& engine) noexcept
    : GameState(engine)
{}

LoadingState::~LoadingState() {
    // Ensure loader thread has finished before destroying
    if (m_loadThread.joinable()) {
        m_loadThread.join();
    }
}

void LoadingState::onEnter() {
    LOG_INFO("LoadingState: starting async asset load");
    m_loadThread = std::thread([this] { loadAssetsAsync(); });
}

void LoadingState::onExit() {
    if (m_loadThread.joinable()) m_loadThread.join();
    LOG_INFO("LoadingState: exiting");
}

// ─────────────────────────────────────────────────────────────────────────────
// Runs on the loader thread — never touches the renderer or ECS directly.
// Only safe operations: AAssetManager reads, ShaderManager::loadShader,
// VulkanBuffer::uploadToDeviceLocal (safe to call from non-render threads
// when using a separate transfer queue).
// ─────────────────────────────────────────────────────────────────────────────
void LoadingState::loadAssetsAsync() {
    constexpr float kSteps = 6.0f;
    int step = 0;

    auto advance = [&] {
        m_progress.store(++step / kSteps, std::memory_order_release);
    };

    // Step 1: Preload shaders
    LOG_INFO("LoadingState [loader]: loading shaders");
    // m_engine.shaderManager().preloadAll();  // Through engine accessor
    advance();

    // Step 2: Load map geometry
    LOG_INFO("LoadingState [loader]: loading map geometry");
    // m_engine.assetManager().loadMap("maps/arena_01");
    advance();

    // Step 3: Load character models
    LOG_INFO("LoadingState [loader]: loading character models");
    // m_engine.assetManager().loadModel("models/hero_assault");
    advance();

    // Step 4: Load weapon models
    LOG_INFO("LoadingState [loader]: loading weapon models");
    advance();

    // Step 5: Load audio assets
    LOG_INFO("LoadingState [loader]: loading audio");
    // m_engine.audioEngine().preloadSounds({"sfx/gunshot", "sfx/footstep", ...});
    advance();

    // Step 6: Complete
    LOG_INFO("LoadingState [loader]: complete");
    advance();
    m_loadComplete.store(true, std::memory_order_release);
}

void LoadingState::update(float deltaTime) {
    m_elapsedTime   += deltaTime;
    m_spinnerAngle  += deltaTime * 360.0f;  // Full rotation per second

    if (m_loadFailed.load(std::memory_order_acquire)) {
        LOG_ERROR("LoadingState: asset load FAILED — returning to menu");
        m_engine.stateManager().pop();
        return;
    }

    // Transition when load is complete AND minimum display time has passed
    constexpr float kMinDisplayTime = 1.5f;
    if (m_loadComplete.load(std::memory_order_acquire) &&
        m_elapsedTime >= kMinDisplayTime)
    {
        LOG_INFO("LoadingState: transitioning to MatchState");
        // Replace the loading state with the match
        // m_engine.stateManager().replace(
        //     std::make_unique<MatchState>(m_engine));
        m_engine.stateManager().pop();
    }
}

void LoadingState::render(VulkanRenderer& renderer) {
    // Render a simple loading screen:
    // In production this would draw a progress bar mesh and spinner sprite.
    // For now we just log progress periodically.
    const float progress = m_progress.load(std::memory_order_acquire);
    (void)renderer;
    (void)progress;
    LOG_DEBUG("LoadingState::render progress=%.0f%%", progress * 100.0f);
}

} // namespace hs
