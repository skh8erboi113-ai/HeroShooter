// app/src/main/cpp/ecs/components/PhysicsComponent.h
#pragma once

#include <cstdint>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Jolt Physics body handle wrapper.
// We store the BodyID as a uint32_t to avoid including Jolt headers
// in every translation unit (reduces compile time significantly).
// ─────────────────────────────────────────────────────────────────────────────
struct PhysicsComponent {
    uint32_t    bodyId          = UINT32_MAX;   // JPH::BodyID::cInvalidBodyID
    float       restitution     = 0.1f;         // Bounciness [0..1]
    float       friction        = 0.6f;         // Surface friction [0..1]
    bool        isKinematic     = false;        // True for character controllers
    bool        isSensor        = false;        // Trigger volumes (no collision response)
};

} // namespace hs
