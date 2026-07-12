// app/src/main/cpp/states/MatchState.h
#pragma once

#include "GameStateManager.h"
#include "../ecs/EntityManager.h"

#include <cstdint>
#include <array>

namespace hs {

class InputManager;
class NetworkManager;

// ─────────────────────────────────────────────────────────────────────────────
// MatchState — the main gameplay loop for a hero shooter match.
//
// Responsibilities:
//   • Spawn the local player entity with appropriate components
//   • Spawn map static geometry entities
//   • Handle respawn timers
//   • Track match score / time limit
//   • Forward input snapshot to the player's movement component
//   • Trigger pause state on back button
// ─────────────────────────────────────────────────────────────────────────────
class MatchState final : public GameState {
public:
    explicit MatchState(GameEngine& engine) noexcept;

    void onEnter()   override;
    void onSuspend() override;
    void onResume()  override;
    void onExit()    override;

    void update(float deltaTime) override;
    void render(VulkanRenderer& renderer) override;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "MatchState";
    }

private:
    void spawnLocalPlayer();
    void spawnMapGeometry();
    void handlePlayerInput(float deltaTime);
    void updateMatchTimer(float deltaTime);
    void checkMatchEnd();

    entt::entity    m_localPlayer   = entt::null;

    // Match state
    float       m_matchTimer        = 0.0f;
    float       m_matchDuration     = 300.0f;   // 5-minute match
    uint32_t    m_localScore        = 0;
    uint32_t    m_enemyScore        = 0;
    bool        m_matchEnded        = false;

    // Respawn
    float       m_respawnTimer      = 0.0f;
    bool        m_isRespawning      = false;
    float       m_respawnDelay      = 5.0f;

    // Camera (simple follow camera)
    float       m_cameraPitch       = -20.0f;   // Degrees
    float       m_cameraYaw         = 0.0f;
    float       m_cameraDistance    = 5.0f;
    float       m_cameraHeight      = 2.5f;
};

} // namespace hs
