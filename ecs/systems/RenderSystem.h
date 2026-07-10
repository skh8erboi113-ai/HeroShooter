// app/src/main/cpp/ecs/systems/RenderSystem.h
#pragma once

#include <entt/entt.hpp>
#include "../../rendering/VulkanBuffer.h"

#include <cstdint>
#include <vector>
#include <array>

namespace hs {

class VulkanRenderer;

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuffer — GPU-resident mesh data
// Uploaded once at load time; indexed by MeshId
// ─────────────────────────────────────────────────────────────────────────────
struct MeshBuffer {
    VulkanBuffer    vertexBuffer;
    VulkanBuffer    indexBuffer;
    uint32_t        indexCount      = 0;
    uint32_t        vertexCount     = 0;
    float           boundingRadius  = 1.0f; // For frustum culling
};

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — 6 planes for view frustum culling
// Planes are in world space: dot(plane.xyz, point) + plane.w > 0 = inside
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) FrustumPlane {
    float x, y, z, w;  // Normal xyz + distance w
};

struct ViewFrustum {
    FrustumPlane planes[6]; // Left, Right, Bottom, Top, Near, Far

    // Returns true if the sphere at worldPos with given radius is inside (or
    // intersects) the frustum. Uses NEON-friendly sequential dot products.
    [[nodiscard]] bool testSphere(
        float px, float py, float pz, float radius) const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// RenderSystem
//
// Performs:
//   1. Frustum culling: reject entities whose bounds are outside the view
//   2. LOD selection: choose the right mesh LOD based on camera distance
//   3. Draw call batching: sort by material to minimise pipeline state changes
//   4. Instancing: group identical meshes+materials into instanced draws
//   5. Frame UBO upload: update camera matrices via persistently mapped buffer
// ─────────────────────────────────────────────────────────────────────────────
class RenderSystem final {
public:
    explicit RenderSystem(entt::registry& registry) noexcept;
    ~RenderSystem();

    RenderSystem(const RenderSystem&)            = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;

    [[nodiscard]] bool init(
        VkDevice         device,
        VkPhysicalDevice physicalDevice,
        uint32_t         maxFramesInFlight
    );

    // Called once per frame to update per-entity GPU data (dirty transforms)
    void update(float deltaTime);

    // Called after update() to record draw calls into the active command buffer
    void submitDrawCalls(VulkanRenderer& renderer);

    // ── Camera ────────────────────────────────────────────────────────────
    void setCameraMatrices(
        const float* viewMatrix4x4,         // Row-major float[16]
        const float* projMatrix4x4,         // Row-major float[16]
        float        camPosX,
        float        camPosY,
        float        camPosZ
    );

    // ── Mesh registry ──────────────────────────────────────────────────────
    [[nodiscard]] uint32_t registerMesh(MeshBuffer&& mesh);
    void destroy(VkDevice device);

private:
    void cullAndSort();
    void buildInstanceBatches();
    void uploadFrameUBO(uint32_t frameIndex);

    // ── Frustum ────────────────────────────────────────────────────────────
    void extractFrustumPlanes();

    entt::registry& m_registry;

    // ── GPU resources ──────────────────────────────────────────────────────
    // Per-frame UBO (double-buffered to avoid stalls)
    std::vector<VulkanBuffer>   m_frameUBOs;    // One per frame in flight
    uint32_t                    m_maxFrames = 2;

    // Mesh data registry (indexed by MeshId)
    std::vector<MeshBuffer>     m_meshRegistry;

    // ── Frame state ────────────────────────────────────────────────────────
    float   m_viewMatrix[16]        {};
    float   m_projMatrix[16]        {};
    float   m_viewProjMatrix[16]    {};
    float   m_cameraPos[3]          {};
    float   m_engineTime            = 0.0f;

    ViewFrustum m_frustum;

    // ── Sorted draw list (rebuilt each frame) ─────────────────────────────
    struct SortedDrawCall {
        uint32_t    materialId;     // Sort key (primary)
        uint32_t    meshId;         // Sort key (secondary)
        uint32_t    entityIdxInView;// Index into culled entity list
        float       distSq;         // Camera distance squared (for LOD/sort)
    };
    std::vector<SortedDrawCall> m_sortedDrawCalls;
    std::vector<SortedDrawCall> m_transparentDrawCalls; // Back-to-front sorted

    // ── Instance buffer (for batched draws) ───────────────────────────────
    static constexpr uint32_t kMaxInstances = 1024;
    VulkanBuffer m_instanceBuffer;  // Per-instance model matrix array (GPU)
};

} // namespace hs
