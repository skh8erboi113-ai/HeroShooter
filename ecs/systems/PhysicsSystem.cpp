// app/src/main/cpp/ecs/systems/PhysicsSystem.cpp
#include "PhysicsSystem.h"

#include "../components/TransformComponent.h"
#include "../components/MovementComponent.h"
#include "../components/PhysicsComponent.h"
#include "../../physics/PhysicsWorld.h"
#include "../../utils/Logger.h"

#ifdef ENGINE_PHYSICS_ENABLED
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#endif

namespace hs {

PhysicsSystem::PhysicsSystem(entt::registry& registry) noexcept
    : m_registry(registry)
{}

void PhysicsSystem::update(float /*deltaTime*/) {
    if (!m_physicsWorld) return;

    syncKinematicToJolt();
    syncJoltToDynamic();
}

// ─────────────────────────────────────────────────────────────────────────────
// For kinematic bodies: push the ECS-computed position into Jolt.
// Jolt's physics step will resolve collisions and update the body's position.
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::syncKinematicToJolt() {
#ifdef ENGINE_PHYSICS_ENABLED
    auto view = m_registry.view<
        const TransformComponent,
        const MovementComponent,
        PhysicsComponent>();

    view.each([this](
        const TransformComponent& xform,
        const MovementComponent&  /*move*/,
        PhysicsComponent&         phys)
    {
        if (phys.bodyId == UINT32_MAX) return;
        if (!phys.isKinematic) return;

        // SetKinematicTarget tells Jolt where we WANT the body to be.
        // Jolt moves it there while resolving overlaps (character vs world).
        // TODO: Call through PhysicsWorld's BodyInterface wrapper
        (void)xform;
        (void)phys;
    });
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// For dynamic bodies: read Jolt-computed transforms back into ECS.
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::syncJoltToDynamic() {
#ifdef ENGINE_PHYSICS_ENABLED
    auto view = m_registry.view<TransformComponent, const PhysicsComponent>();

    view.each([this](TransformComponent& xform, const PhysicsComponent& phys) {
        if (phys.bodyId == UINT32_MAX) return;
        if (phys.isKinematic) return;   // Kinematic bodies: ECS drives Jolt

        float px, py, pz, qx, qy, qz, qw;
        m_physicsWorld->getBodyTransform(
            phys.bodyId, &px, &py, &pz, &qx, &qy, &qz, &qw);

        xform.setPosition({ px, py, pz });
        xform.setRotation({ qx, qy, qz, qw });
    });
#endif
}

} // namespace hs
