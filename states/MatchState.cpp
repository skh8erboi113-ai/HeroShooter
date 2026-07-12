// app/src/main/cpp/states/MatchState.cpp
#include "MatchState.h"
#include "../main/GameEngine.h"
#include "../ecs/components/TransformComponent.h"
#include "../ecs/components/MovementComponent.h"
#include "../ecs/components/NetworkComponent.h"
#include "../rendering/VulkanRenderer.h"
#include "../input/InputManager.h"
#include "../utils/Logger.h"
#include "../utils/Timer.h"

#include <cmath>

namespace hs {

MatchState::MatchState(GameEngine& engine) noexcept
    : GameState(engine)
{}

void MatchState::onEnter() {
    LOG_INFO("MatchState: entering match");
    spawnMapGeometry();
    spawnLocalPlayer();
    m_matchTimer = 0.0f;
}

void MatchState::onSuspend() {
    LOG_INFO("MatchState: suspended (pause menu opened)");
    // Pause audio, etc.
}

void MatchState::onResume() {
    LOG_INFO("MatchState: resumed");
}

void MatchState::onExit() {
    LOG_INFO("MatchState: exiting, cleaning up entities");
    // EntityManager will be cleared by the engine on shutdown
    // or we can tag entities for removal here
}

// ─────────────────────────────────────────────────────────────────────────────
void MatchState::spawnLocalPlayer() {
    TransformComponent spawnXform;
    spawnXform.setPosition({ 0.0f, 1.8f, 0.0f });  // Stand 1.8m above ground

    m_localPlayer = m_engine.entityManager().createPlayer(spawnXform, 1);

    // Mark as locally owned
    auto& net = m_engine.entityManager().registry().get<NetworkComponent>(m_localPlayer);
    net.isLocallyOwned = true;

    LOG_INFO("MatchState: spawned local player entity [%u]",
             static_cast<uint32_t>(m_localPlayer));
}

// ─────────────────────────────────────────────────────────────────────────────
void MatchState::spawnMapGeometry() {
    // Spawn static ground plane
    TransformComponent groundXform;
    groundXform.setPosition({ 0.0f, 0.0f, 0.0f });
    groundXform.setScale({ 50.0f, 0.1f, 50.0f });

    m_engine.entityManager().createStaticMesh(groundXform, 0 /*meshId=ground*/);

    // Spawn cover objects
    for (int i = 0; i < 8; ++i) {
        TransformComponent coverXform;
        const float angle = static_cast<float>(i) * (6.2832f / 8.0f);
        coverXform.setPosition({
            std::cos(angle) * 12.0f,
            0.5f,
            std::sin(angle) * 12.0f
        });
        coverXform.setScale({ 1.5f, 1.5f, 0.5f });
        m_engine.entityManager().createStaticMesh(coverXform, 1 /*meshId=box*/);
    }

    LOG_INFO("MatchState: spawned map geometry");
}

// ─────────────────────────────────────────────────────────────────────────────
void MatchState::update(float deltaTime) {
    if (m_matchEnded) return;

    updateMatchTimer(deltaTime);
    handlePlayerInput(deltaTime);
    checkMatchEnd();

    // Handle respawn
    if (m_isRespawning) {
        m_respawnTimer -= deltaTime;
        if (m_respawnTimer <= 0.0f) {
            m_isRespawning = false;
            spawnLocalPlayer();
        }
    }

    // Pause on back button / pause button press
    const InputSnapshot& input = m_engine.inputManager().snapshot();
    if (input.isButtonPressed(VirtualButton::Pause)) {
        LOG_INFO("MatchState: pause requested");
        // m_engine.stateManager().push(std::make_unique<PauseState>(m_engine));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MatchState::handlePlayerInput(float deltaTime) {
    if (m_localPlayer == entt::null) return;
    if (!m_engine.entityManager().registry().valid(m_localPlayer)) return;

    const InputSnapshot& input = m_engine.inputManager().snapshot();
    auto& move = m_engine.entityManager().registry()
                         .get<MovementComponent>(m_localPlayer);
    auto& xform = m_engine.entityManager().registry()
                          .get<TransformComponent>(m_localPlayer);

    // ── Movement ────────────────────────────────────────────────────────────
    // Convert stick input from camera-relative to world-relative direction
    const float cosYaw = std::cos(m_cameraYaw * 0.01745f);  // deg→rad
    const float sinYaw = std::sin(m_cameraYaw * 0.01745f);

    move.inputDirection = {
        input.moveStick.x * cosYaw + input.moveStick.y * sinYaw,
        0.0f,
        input.moveStick.y * cosYaw - input.moveStick.x * sinYaw,
    };
    move.sprinting = input.isButtonHeld(VirtualButton::Sprint);
    move.jumping   = input.isButtonPressed(VirtualButton::Jump);

    // ── Camera rotation from right stick ────────────────────────────────────
    constexpr float kCameraSpeed = 120.0f;  // Degrees per second at full deflection
    m_cameraYaw   += input.aimStick.x * kCameraSpeed * deltaTime;
    m_cameraPitch += input.aimStick.y * kCameraSpeed * deltaTime;
    m_cameraPitch  = std::max(-89.0f, std::min(89.0f, m_cameraPitch)); // Clamp pitch

    // ── Update camera view matrix ────────────────────────────────────────────
    // Third-person follow camera: orbit around player position
    const Vec3& playerPos = xform.position;
    const float pitchRad  = m_cameraPitch * 0.01745f;
    const float yawRad    = m_cameraYaw   * 0.01745f;

    const float camX = playerPos.x + m_cameraDistance * std::cos(pitchRad) * std::sin(yawRad);
    const float camY = playerPos.y + m_cameraHeight   + m_cameraDistance * std::sin(pitchRad);
    const float camZ = playerPos.z + m_cameraDistance * std::cos(pitchRad) * std::cos(yawRad);

    // TODO: Build view/proj matrices and pass to RenderSystem::setCameraMatrices
    (void)camX; (void)camY; (void)camZ;

    // ── Fire input ───────────────────────────────────────────────────────────
    if (input.isButtonHeld(VirtualButton::Fire)) {
        // TODO: Raycast from camera forward → if hit, apply damage
    }

    (void)deltaTime;
}

void MatchState::updateMatchTimer(float deltaTime) {
    m_matchTimer += deltaTime;
}

void MatchState::checkMatchEnd() {
    if (m_matchTimer >= m_matchDuration) {
        m_matchEnded = true;
        LOG_INFO("MatchState: match ended! Score: local=%u enemy=%u",
                 m_localScore, m_enemyScore);
        // m_engine.stateManager().replace(
        //     std::make_unique<ResultsState>(m_engine, m_localScore, m_enemyScore));
    }
}

void MatchState::render(VulkanRenderer& renderer) {
    // RenderSystem::submitDrawCalls is called by EntityManager::submitRenderCommands
    // which is called directly by GameEngine::render()
    // This state's render function handles UI overlays (HUD) only
    (void)renderer;
}

} // namespace hs
