// app/src/main/cpp/ecs/EntityManager.h
#pragma once

// EnTT — header-only, zero RTTI, zero exceptions
// EnTT uses type-erasure without RTTI by leveraging compile-time type IDs
#include <entt/entt.hpp>

// Components
#include "components/TransformComponent.h"
#include "components/MovementComponent.h"
#include "components/RenderComponent.h"
#include "components/PhysicsComponent.h"
#include "components/NetworkComponent.h"

// Systems (forward declared to avoid circular includes)
namespace hs {
    class MovementSystem;
    class PhysicsSystem;
    class RenderSystem;
    class NetworkSystem;
    class JobSystem;
    class VulkanRenderer;
}

#include <memory>
#include <cstdint>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// EntityManager
//
// Owns the EnTT registry and all ECS systems.
// Systems are updated in priority order each frame.
// The registry is NOT thread-safe; all mutations happen on the game thread.
// Read-only access for rendering can be parallelised safely.
// ─────────────────────────────────────────────────────────────────────────────
class EntityManager final {
public:
    explicit EntityManager(JobSystem& jobSystem) noexcept;
    ~EntityManager();

    [[nodiscard]] bool init();
    void update(float deltaTime);
    void submitRenderCommands(VulkanRenderer& renderer);
    void clear();

    // ── Entity factory helpers ─────────────────────────────────────────────
    [[nodiscard]] entt::entity createPlayer(
        const TransformComponent& transform,
        uint32_t networkId
    );

    [[nodiscard]] entt::entity createProjectile(
        const TransformComponent& transform,
        const MovementComponent&  movement
    );

    [[nodiscard]] entt::entity createStaticMesh(
        const TransformComponent& transform,
        MeshId                    meshId
    );

    void destroyEntity(entt::entity entity);

    // ── Direct registry access (use sparingly) ─────────────────────────────
    [[nodiscard]] entt::registry& registry() noexcept { return m_registry; }
    [[nodiscard]] const entt::registry& registry() const noexcept { return m_registry; }

private:
    entt::registry              m_registry;
    JobSystem&                  m_jobSystem;

    // Owned systems — updated in order
    std::unique_ptr<NetworkSystem>   m_networkSystem;
    std::unique_ptr<MovementSystem>  m_movementSystem;
    std::unique_ptr<PhysicsSystem>   m_physicsSystem;
    std::unique_ptr<RenderSystem>    m_renderSystem;
};

} // namespace hs
