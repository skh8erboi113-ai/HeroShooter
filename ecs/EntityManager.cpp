// app/src/main/cpp/ecs/EntityManager.cpp
#include "EntityManager.h"

#include "systems/MovementSystem.h"
#include "systems/PhysicsSystem.h"
#include "systems/RenderSystem.h"
#include "systems/NetworkSystem.h"
#include "../threading/JobSystem.h"
#include "../rendering/VulkanRenderer.h"
#include "../utils/Logger.h"

namespace hs {

EntityManager::EntityManager(JobSystem& jobSystem) noexcept
    : m_jobSystem(jobSystem)
{}

EntityManager::~EntityManager() {
    clear();
}

bool EntityManager::init() {
    LOG_INFO("EntityManager::init()");

    // Construct systems — each is given a reference to the registry
    m_networkSystem  = std::make_unique<NetworkSystem>(m_registry);
    m_movementSystem = std::make_unique<MovementSystem>(m_registry);
    m_physicsSystem  = std::make_unique<PhysicsSystem>(m_registry);
    m_renderSystem   = std::make_unique<RenderSystem>(m_registry);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — called once per frame from GameEngine::updateECS()
//
// System execution order:
//   1. NetworkSystem  — apply received snapshots, run prediction rollback
//   2. MovementSystem — integrate velocity → position using input + physics
//   3. PhysicsSystem  — sync ECS transforms with Jolt body transforms
//   4. RenderSystem   — update per-entity GPU uniform buffers (transform matrices)
//
// Systems 2 & 3 are parallelisable for non-overlapping entity groups
// (e.g., projectiles vs. player characters), but serialised here for clarity.
// ─────────────────────────────────────────────────────────────────────────────
void EntityManager::update(float deltaTime) {
    m_networkSystem->update(deltaTime);
    m_movementSystem->update(deltaTime);
    m_physicsSystem->update(deltaTime);
    m_renderSystem->update(deltaTime);
}

void EntityManager::submitRenderCommands(VulkanRenderer& renderer) {
    m_renderSystem->submitDrawCalls(renderer);
}

void EntityManager::clear() {
    m_registry.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity factory helpers
// ─────────────────────────────────────────────────────────────────────────────

entt::entity EntityManager::createPlayer(
    const TransformComponent& transform,
    uint32_t networkId)
{
    const entt::entity e = m_registry.create();

    m_registry.emplace<TransformComponent>(e, transform);
    m_registry.emplace<MovementComponent>(e);
    m_registry.emplace<RenderComponent>(e,
        RenderComponent{ .meshId = 1, .materialId = 0 }  // C++20 designated init
    );
    m_registry.emplace<PhysicsComponent>(e,
        PhysicsComponent{ .isKinematic = true }
    );
    m_registry.emplace<NetworkComponent>(e,
        NetworkComponent{ .networkId = networkId }
    );

    LOG_INFO("Created player entity [%u] networkId=%u",
             static_cast<uint32_t>(e), networkId);
    return e;
}

entt::entity EntityManager::createProjectile(
    const TransformComponent& transform,
    const MovementComponent&  movement)
{
    const entt::entity e = m_registry.create();

    m_registry.emplace<TransformComponent>(e, transform);
    m_registry.emplace<MovementComponent>(e, movement);
    m_registry.emplace<RenderComponent>(e,
        RenderComponent{ .meshId = 2, .castsShadow = false }
    );
    m_registry.emplace<PhysicsComponent>(e,
        PhysicsComponent{ .restitution = 0.0f, .friction = 0.0f }
    );
    return e;
}

entt::entity EntityManager::createStaticMesh(
    const TransformComponent& transform,
    MeshId meshId)
{
    const entt::entity e = m_registry.create();

    m_registry.emplace<TransformComponent>(e, transform);
    m_registry.emplace<RenderComponent>(e,
        RenderComponent{ .meshId = meshId }
    );
    return e;
}

void EntityManager::destroyEntity(entt::entity entity) {
    if (m_registry.valid(entity)) {
        m_registry.destroy(entity);
    }
}

} // namespace hs
