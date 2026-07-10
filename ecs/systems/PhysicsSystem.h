// app/src/main/cpp/ecs/systems/PhysicsSystem.h
#pragma once

#include <entt/entt.hpp>

namespace hs {

class PhysicsWorld;

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsSystem
//
// Bridges the ECS layer and the Jolt physics world:
//
//   WRITE path (ECS → Jolt):
//     For kinematic bodies (character controllers), we read the desired
//     position from the ECS TransformComponent and push it into Jolt via
//     SetKinematicTarget(). Jolt then performs CCD collision detection.
//
//   READ path (Jolt → ECS):
//     For dynamic bodies (ragdolls, physics props), we read the Jolt body
//     transform and write it back to the TransformComponent.
//
// This runs AFTER MovementSystem so that kinematic targets reflect the
// movement integration for this frame.
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsSystem final {
public:
    PhysicsSystem(entt::registry& registry) noexcept;

    void setPhysicsWorld(PhysicsWorld* world) noexcept { m_physicsWorld = world; }
    void update(float deltaTime);

private:
    void syncKinematicToJolt();    // ECS position → Jolt kinematic target
    void syncJoltToDynamic();      // Jolt result → ECS transform (dynamic bodies)

    entt::registry& m_registry;
    PhysicsWorld*   m_physicsWorld  = nullptr;
    float           m_accumulator   = 0.0f;
};

} // namespace hs
