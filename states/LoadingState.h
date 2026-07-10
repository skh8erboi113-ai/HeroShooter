// app/src/main/cpp/states/LoadingState.h
#pragma once

#include "GameStateManager.h"
#include <atomic>
#include <thread>
#include <memory>

namespace hs {

class ShaderManager;
class EntityManager;

// ─────────────────────────────────────────────────────────────────────────────
// LoadingState
//
// Manages asynchronous asset loading while displaying a progress indicator.
//
// Architecture:
//   • Main thread: renders loading screen at 60fps via Swappy
//   • Worker thread: loads shaders, meshes, textures via AAssetManager
//   • Progress: atomic float [0.0, 1.0] shared between threads
//   • On completion: transitions to MatchState via push()
// ─────────────────────────────────────────────────────────────────────────────
class LoadingState final : public GameState {
public:
    explicit LoadingState(GameEngine& engine) noexcept;
    ~LoadingState() override;

    void onEnter()  override;
    void onExit()   override;

    void update(float deltaTime) override;
    void render(VulkanRenderer& renderer) override;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "LoadingState";
    }

private:
    void loadAssetsAsync();

    std::thread             m_loadThread;
    std::atomic<float>      m_progress      { 0.0f };
    std::atomic<bool>       m_loadComplete  { false };
    std::atomic<bool>       m_loadFailed    { false };

    float   m_spinnerAngle  = 0.0f;
    float   m_elapsedTime   = 0.0f;
};

} // namespace hs
