// app/src/main/cpp/physics/PhysicsWorld.h
#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

// Jolt uses its own allocator — we must configure this before including Jolt
// Jolt is compiled with -fexceptions disabled on Android (our flag matches)
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "CollisionLayers.h"

#include <cstdint>
#include <memory>

namespace hs {

class JobSystem;

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsWorld
//
// Wraps JPH::PhysicsSystem with Android-appropriate configuration.
// Uses Jolt's built-in job system hooked to our ThreadPool for parallel
// broad-phase and constraint solving.
//
// For a multiplayer game:
//   • CROSS_PLATFORM_DETERMINISTIC=ON ensures identical simulation on all clients
//   • Fixed-timestep stepping (called from GameEngine::tick) is mandatory
//   • All body mutations happen on the game thread; Jolt jobs are internal
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsWorld final {
public:
    explicit PhysicsWorld(JobSystem& jobSystem) noexcept;
    ~PhysicsWorld();

    [[nodiscard]] bool init();
    void step(float deltaTime);
    void shutdown();

    // ── Body creation (returns Jolt BodyID as uint32_t) ──────────────────
    [[nodiscard]] uint32_t createBox(
        float halfExtentX, float halfExtentY, float halfExtentZ,
        float px, float py, float pz,
        bool isDynamic = true
    );

    [[nodiscard]] uint32_t createCapsule(
        float halfHeight, float radius,
        float px, float py, float pz,
        bool isKinematic = true
    );

    [[nodiscard]] uint32_t createStaticMesh(/* mesh data */);

    void destroyBody(uint32_t bodyId);

    // ── Query ──────────────────────────────────────────────────────────────
    bool raycast(
        float ox, float oy, float oz,
        float dx, float dy, float dz,
        float maxDistance,
        uint32_t* hitBodyId = nullptr
    );

    // ── Body state read-back (for ECS sync) ───────────────────────────────
    void getBodyTransform(uint32_t bodyId,
                          float* outPx, float* outPy, float* outPz,
                          float* outQx, float* outQy, float* outQz, float* outQw);

private:
    JobSystem& m_jobSystem;

    // Jolt allocates ~10 MB of temp memory for broadphase, constraint solving
    static constexpr uint32_t kTempAllocatorSize = 10 * 1024 * 1024; // 10 MB
    static constexpr uint32_t kMaxBodies         = 4096;
    static constexpr uint32_t kMaxBodyPairs      = 65536;
    static constexpr uint32_t kMaxContactConstraints = 16384;

    std::unique_ptr<JPH::TempAllocatorImpl>     m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool>   m_joltJobSystem;
    std::unique_ptr<JPH::PhysicsSystem>         m_physicsSystem;

    // Broad phase layer interface (maps object layers to broad-phase layers)
    // Implementation in CollisionLayers.h
    HsBPLayerInterface                          m_bpLayerInterface;
    HsObjectVsBroadPhaseLayerFilter             m_objectVsBPFilter;
    HsObjectLayerPairFilter                     m_objectLayerPairFilter;

    float   m_accumulator   = 0.0f;
    bool    m_initialised   = false;
};

} // namespace hs

#endif // ENGINE_PHYSICS_ENABLED
