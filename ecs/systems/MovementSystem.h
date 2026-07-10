// app/src/main/cpp/ecs/systems/MovementSystem.h
#pragma once

#include <entt/entt.hpp>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// MovementSystem
//
// Iterates all entities with (TransformComponent, MovementComponent) and
// integrates velocity into position using semi-implicit Euler integration.
//
// The EnTT view<TransformComponent, MovementComponent>() provides a
// cache-friendly, SoA-style iteration that is significantly faster than
// iterating a list of entity IDs and doing map lookups.
// ─────────────────────────────────────────────────────────────────────────────
class MovementSystem final {
public:
    explicit MovementSystem(entt::registry& registry) noexcept;

    void update(float deltaTime);

private:
    entt::registry& m_registry;
};

} // namespace hs
