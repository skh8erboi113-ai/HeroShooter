// app/src/main/cpp/physics/CollisionLayers.h
#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Collision layer definitions for the hero shooter.
// Object layers: logical category of each physics body.
// Broad-phase layers: coarse groupings for the AABB broad phase.
// ─────────────────────────────────────────────────────────────────────────────

// Object layers
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING   = 0;  // Static world geometry
    static constexpr JPH::ObjectLayer MOVING       = 1;  // Dynamic: players, projectiles
    static constexpr JPH::ObjectLayer SENSOR       = 2;  // Trigger volumes
    static constexpr JPH::ObjectLayer PROJECTILE   = 3;  // Bullets (tiny, fast-moving)
    static constexpr JPH::ObjectLayer NUM_LAYERS   = 4;
}

// Broad-phase layers
namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING { 0 };
    static constexpr JPH::BroadPhaseLayer MOVING     { 1 };
    static constexpr uint32_t             NUM_LAYERS   = 2;
}

// ─────────────────────────────────────────────────────────────────────────────
// BroadPhase layer interface — maps object layer → broad-phase layer
// ─────────────────────────────────────────────────────────────────────────────
class HsBPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    HsBPLayerInterface() {
        m_objectToBPLayer[Layers::NON_MOVING]  = BPLayers::NON_MOVING;
        m_objectToBPLayer[Layers::MOVING]      = BPLayers::MOVING;
        m_objectToBPLayer[Layers::SENSOR]      = BPLayers::MOVING;
        m_objectToBPLayer[Layers::PROJECTILE]  = BPLayers::MOVING;
    }

    uint32_t GetNumBroadPhaseLayers() const override {
        return BPLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return m_objectToBPLayer[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BPLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
#endif

private:
    JPH::BroadPhaseLayer m_objectToBPLayer[Layers::NUM_LAYERS] {};
};

// ─────────────────────────────────────────────────────────────────────────────
// Object vs broad-phase layer filter — can object layer collide with BP layer?
// ─────────────────────────────────────────────────────────────────────────────
class HsObjectVsBroadPhaseLayerFilter final
    : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer,
                       JPH::BroadPhaseLayer bpLayer) const override {
        switch (layer) {
            case Layers::NON_MOVING:
                return bpLayer == BPLayers::MOVING;   // Static only collides with moving
            case Layers::MOVING:
            case Layers::PROJECTILE:
                return true;                           // Moving collides with everything
            case Layers::SENSOR:
                return bpLayer == BPLayers::MOVING;   // Sensors detect moving objects
            default:
                return false;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Object layer pair filter — can two object layers collide?
// ─────────────────────────────────────────────────────────────────────────────
class HsObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        switch (a) {
            case Layers::NON_MOVING:
                return b == Layers::MOVING || b == Layers::PROJECTILE;
            case Layers::MOVING:
                return b != Layers::SENSOR;
            case Layers::PROJECTILE:
                return b == Layers::NON_MOVING || b == Layers::MOVING;
            case Layers::SENSOR:
                return b == Layers::MOVING;
            default:
                return false;
        }
    }
};

} // namespace hs

#endif // ENGINE_PHYSICS_ENABLED
