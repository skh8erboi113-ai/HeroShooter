// app/src/main/cpp/physics/PhysicsWorld.cpp
#include "PhysicsWorld.h"

#ifdef ENGINE_PHYSICS_ENABLED

#include "../threading/JobSystem.h"
#include "../utils/Logger.h"

// Jolt core
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/ContactListener.h>

// Standard
#include <cstdarg>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Jolt trace callback — routes Jolt debug output to our logger
// ─────────────────────────────────────────────────────────────────────────────
static void JoltTraceImpl(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LOG_INFO("[Jolt] %s", buf);
}

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssertFailed(const char* expr, const char* msg,
                              const char* file, uint32_t line)
{
    LOG_ERROR("[Jolt Assert] %s:%u — %s (%s)", file, line, msg ? msg : "", expr);
    return true;    // Return true = trigger breakpoint in debug builds
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Contact listener — receives collision callbacks on the physics thread
// We forward relevant events (hit detection) to the ECS/gameplay layer
// via a lock-free event queue (not implemented here for brevity).
// ─────────────────────────────────────────────────────────────────────────────
class HsContactListener final : public JPH::ContactListener {
public:
    JPH::ValidateResult OnContactValidate(
        const JPH::Body& /*body1*/,
        const JPH::Body& /*body2*/,
        JPH::RVec3Arg    /*baseOffset*/,
        const JPH::CollideShapeResult& /*result*/) override
    {
        // Allow all contacts — gameplay filtering happens in ObjectLayerPairFilter
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(
        const JPH::Body&              body1,
        const JPH::Body&              body2,
        const JPH::ContactManifold&   /*manifold*/,
        JPH::ContactSettings&         /*settings*/) override
    {
        // TODO: Push (bodyId1, bodyId2) onto a lock-free SPSC queue
        // that the game thread drains each frame for hit registration
        (void)body1; (void)body2;
    }

    void OnContactPersisted(
        const JPH::Body&            /*body1*/,
        const JPH::Body&            /*body2*/,
        const JPH::ContactManifold& /*manifold*/,
        JPH::ContactSettings&       /*settings*/) override {}

    void OnContactRemoved(
        const JPH::SubShapeIDPair& /*pair*/) override {}
};

// ─────────────────────────────────────────────────────────────────────────────
// Body activation listener — for dirty tracking
// ─────────────────────────────────────────────────────────────────────────────
class HsBodyActivationListener final : public JPH::BodyActivationListener {
public:
    void OnBodyActivated(const JPH::BodyID& /*id*/, uint64_t /*userData*/) override {}
    void OnBodyDeactivated(const JPH::BodyID& /*id*/, uint64_t /*userData*/) override {}
};

// ─────────────────────────────────────────────────────────────────────────────
PhysicsWorld::PhysicsWorld(JobSystem& jobSystem) noexcept
    : m_jobSystem(jobSystem)
{}

PhysicsWorld::~PhysicsWorld() {
    if (m_initialised) shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
bool PhysicsWorld::init() {
    LOG_INFO("PhysicsWorld::init()");

    // ── Register Jolt trace and assert callbacks ───────────────────────────
    JPH::Trace = JoltTraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertFailed;
#endif

    // ── Create Jolt factory and register all types ─────────────────────────
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    // ── Allocators ────────────────────────────────────────────────────────
    // TempAllocatorImpl uses a pre-allocated stack-like region — O(1) alloc
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kTempAllocatorSize);

    // Jolt job system: uses our thread pool's worker count
    // Jolt internally creates barriers and jobs — we just provide the threads
    const uint32_t workerCount = static_cast<uint32_t>(
        std::thread::hardware_concurrency() - 2);
    m_joltJobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        static_cast<int>(workerCount > 0 ? workerCount : 1)
    );

    // ── Create the physics system ──────────────────────────────────────────
    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(
        kMaxBodies,
        0,                  // numBodyMutexes (0 = auto)
        kMaxBodyPairs,
        kMaxContactConstraints,
        m_bpLayerInterface,
        m_objectVsBPFilter,
        m_objectLayerPairFilter
    );

    // ── Gravity (standard Earth gravity downward) ──────────────────────────
    m_physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    // ── Attach listeners ───────────────────────────────────────────────────
    static HsContactListener        contactListener;
    static HsBodyActivationListener activationListener;
    m_physicsSystem->SetContactListener(&contactListener);
    m_physicsSystem->SetBodyActivationListener(&activationListener);

    m_initialised = true;
    LOG_INFO("PhysicsWorld: Jolt Physics initialised "
             "(maxBodies=%u, workers=%u)", kMaxBodies, workerCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void PhysicsWorld::step(float deltaTime) {
    if (!m_initialised) return;

    // Jolt requires a fixed number of collision steps per Update call.
    // We use 1 step at 60Hz (called with a fixed deltaTime from GameEngine).
    // For slower update rates (e.g. 30Hz), increase to 2 steps.
    constexpr int kCollisionSteps = 1;

    m_physicsSystem->Update(
        deltaTime,
        kCollisionSteps,
        m_tempAllocator.get(),
        m_joltJobSystem.get()
    );
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t PhysicsWorld::createBox(
    float hx, float hy, float hz,
    float px, float py, float pz,
    bool isDynamic)
{
    auto& bodyInterface = m_physicsSystem->GetBodyInterface();

    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(hx, hy, hz));
    shapeSettings.mConvexRadius = 0.01f;    // Small convex radius for stability

    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        LOG_ERROR("PhysicsWorld::createBox shape error: %s",
                  shapeResult.GetError().c_str());
        return UINT32_MAX;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(px, py, pz),
        JPH::Quat::sIdentity(),
        isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
        isDynamic ? Layers::MOVING : Layers::NON_MOVING
    );

    // Layer-appropriate settings
    if (isDynamic) {
        bodySettings.mLinearDamping  = 0.05f;
        bodySettings.mAngularDamping = 0.05f;
    }

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        LOG_ERROR("PhysicsWorld::createBox: body creation failed "
                  "(pool exhausted? maxBodies=%u)", kMaxBodies);
        return UINT32_MAX;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);

    const uint32_t id = body->GetID().GetIndexAndSequenceNumber();
    LOG_INFO("PhysicsWorld: created box body id=%u at (%.2f,%.2f,%.2f)",
             id, px, py, pz);
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t PhysicsWorld::createCapsule(
    float halfHeight, float radius,
    float px, float py, float pz,
    bool isKinematic)
{
    auto& bodyInterface = m_physicsSystem->GetBodyInterface();

    // CapsuleShape: cylinder with hemispherical ends — ideal for character controllers
    JPH::CapsuleShapeSettings shapeSettings(halfHeight, radius);
    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        LOG_ERROR("PhysicsWorld::createCapsule shape error: %s",
                  shapeResult.GetError().c_str());
        return UINT32_MAX;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(px, py, pz),
        JPH::Quat::sIdentity(),
        isKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
        Layers::MOVING
    );

    // Kinematic bodies are moved explicitly via SetPosition — no forces applied
    bodySettings.mGravityFactor = isKinematic ? 0.0f : 1.0f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        LOG_ERROR("PhysicsWorld::createCapsule: body creation failed");
        return UINT32_MAX;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

// ─────────────────────────────────────────────────────────────────────────────
void PhysicsWorld::destroyBody(uint32_t bodyId) {
    if (!m_initialised || bodyId == UINT32_MAX) return;

    auto& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID joltId(bodyId);

    bodyInterface.RemoveBody(joltId);
    bodyInterface.DestroyBody(joltId);
}

// ─────────────────────────────────────────────────────────────────────────────
void PhysicsWorld::getBodyTransform(
    uint32_t bodyId,
    float* outPx, float* outPy, float* outPz,
    float* outQx, float* outQy, float* outQz, float* outQw)
{
    const auto& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID joltId(bodyId);

    const JPH::RVec3 pos  = bodyInterface.GetPosition(joltId);
    const JPH::Quat  rot  = bodyInterface.GetRotation(joltId);

    *outPx = pos.GetX(); *outPy = pos.GetY(); *outPz = pos.GetZ();
    *outQx = rot.GetX(); *outQy = rot.GetY();
    *outQz = rot.GetZ(); *outQw = rot.GetW();
}

// ─────────────────────────────────────────────────────────────────────────────
bool PhysicsWorld::raycast(
    float ox, float oy, float oz,
    float dx, float dy, float dz,
    float maxDistance,
    uint32_t* hitBodyId)
{
    JPH::RRayCast ray {
        JPH::RVec3(ox, oy, oz),
        JPH::Vec3(dx, dy, dz) * maxDistance
    };

    JPH::RayCastResult result;
    const bool hit = m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, result);

    if (hit && hitBodyId) {
        *hitBodyId = result.mBodyID.GetIndexAndSequenceNumber();
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
void PhysicsWorld::shutdown() {
    LOG_INFO("PhysicsWorld::shutdown()");
    m_physicsSystem.reset();
    m_joltJobSystem.reset();
    m_tempAllocator.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_initialised = false;
}

} // namespace hs

#endif // ENGINE_PHYSICS_ENABLED
