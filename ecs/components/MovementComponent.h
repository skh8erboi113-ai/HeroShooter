// app/src/main/cpp/ecs/components/MovementComponent.h
#pragma once

#include "TransformComponent.h"  // For Vec3

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// MovementComponent
//
// Stores kinematic movement data for entities that move each frame.
// The MovementSystem reads this component and writes to TransformComponent.
//
// Design notes:
//   • maxSpeed is used to clamp the resultant velocity vector.
//   • drag simulates air resistance / friction (exponential decay per frame).
//   • grounded tracks whether the character is on the ground for jump logic.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) MovementComponent {
    Vec3    velocity        { 0.0f, 0.0f, 0.0f };   // Current velocity (m/s)
    Vec3    acceleration    { 0.0f, 0.0f, 0.0f };   // Applied this frame (m/s²)
    Vec3    inputDirection  { 0.0f, 0.0f, 0.0f };   // Normalised input vector

    float   maxSpeed        = 8.0f;     // Maximum horizontal speed (m/s)
    float   jumpImpulse     = 5.0f;     // Vertical impulse for jumping (m/s)
    float   drag            = 0.85f;    // Velocity multiplier per frame [0..1]
    float   mass            = 80.0f;    // Entity mass (kg) for physics integration

    bool    grounded        = false;    // True when standing on a surface
    bool    jumping         = false;    // Jump requested this frame
    bool    sprinting       = false;    // Sprint input active
};

} // namespace hs
