// app/src/main/cpp/ecs/systems/RenderSystem.cpp
#include "RenderSystem.h"

#include "../components/TransformComponent.h"
#include "../components/RenderComponent.h"
#include "../../rendering/VulkanRenderer.h"
#include "../../rendering/VulkanPipeline.h"
#include "../../utils/Logger.h"

#include <algorithm>    // std::sort
#include <cmath>        // std::sqrt, std::abs
#include <cstring>      // memcpy

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// ViewFrustum sphere test
// ─────────────────────────────────────────────────────────────────────────────
bool ViewFrustum::testSphere(float px, float py, float pz, float radius) const noexcept {
    for (int i = 0; i < 6; ++i) {
        const float dist =   planes[i].x * px
                           + planes[i].y * py
                           + planes[i].z * pz
                           + planes[i].w;
        if (dist < -radius) {
            return false;   // Sphere is fully outside this plane
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
RenderSystem::RenderSystem(entt::registry& registry) noexcept
    : m_registry(registry)
{
    m_sortedDrawCalls.reserve(2048);
    m_transparentDrawCalls.reserve(128);
    m_meshRegistry.reserve(256);
}

RenderSystem::~RenderSystem() {}

// ─────────────────────────────────────────────────────────────────────────────
bool RenderSystem::init(
    VkDevice         device,
    VkPhysicalDevice physicalDevice,
    uint32_t         maxFramesInFlight)
{
    m_maxFrames = maxFramesInFlight;
    m_frameUBOs.resize(maxFramesInFlight);

    // Create persistently mapped UBOs — one per frame in flight
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        if (!m_frameUBOs[i].createHostVisible(
                device, physicalDevice,
                sizeof(FrameUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                true /* persistentMap */))
        {
            LOG_ERROR("RenderSystem: failed to create frame UBO %u", i);
            return false;
        }
    }

    // Instance buffer: max 1024 instances × mat4 (64 bytes each)
    if (!m_instanceBuffer.createHostVisible(
            device, physicalDevice,
            kMaxInstances * sizeof(float) * 16,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            true /* persistentMap */))
    {
        LOG_ERROR("RenderSystem: failed to create instance buffer");
        return false;
    }

    LOG_INFO("RenderSystem: initialised with %u frame UBOs", maxFramesInFlight);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void RenderSystem::setCameraMatrices(
    const float* view,
    const float* proj,
    float camX, float camY, float camZ)
{
    memcpy(m_viewMatrix, view, sizeof(m_viewMatrix));
    memcpy(m_projMatrix, proj, sizeof(m_projMatrix));
    m_cameraPos[0] = camX;
    m_cameraPos[1] = camY;
    m_cameraPos[2] = camZ;

    // Pre-compute ViewProjection = Projection * View
    // Row-major matrix multiplication (matches GLSL layout(row_major))
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m_projMatrix[r * 4 + k] * m_viewMatrix[k * 4 + c];
            }
            m_viewProjMatrix[r * 4 + c] = sum;
        }
    }

    // Rebuild frustum planes from the new VP matrix
    extractFrustumPlanes();
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract 6 frustum planes from the ViewProjection matrix.
// Uses the Gribb/Hartmann method — each plane is a linear combination of VP rows.
// ─────────────────────────────────────────────────────────────────────────────
void RenderSystem::extractFrustumPlanes() {
    const float* vp = m_viewProjMatrix;

    auto normalisePlane = [](FrustumPlane& p) {
        const float len = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
        if (len > 1e-6f) {
            p.x /= len; p.y /= len; p.z /= len; p.w /= len;
        }
    };

    // Left:   row3 + row0
    m_frustum.planes[0] = { vp[12]+vp[0], vp[13]+vp[1], vp[14]+vp[2], vp[15]+vp[3] };
    // Right:  row3 - row0
    m_frustum.planes[1] = { vp[12]-vp[0], vp[13]-vp[1], vp[14]-vp[2], vp[15]-vp[3] };
    // Bottom: row3 + row1
    m_frustum.planes[2] = { vp[12]+vp[4], vp[13]+vp[5], vp[14]+vp[6], vp[15]+vp[7] };
    // Top:    row3 - row1
    m_frustum.planes[3] = { vp[12]-vp[4], vp[13]-vp[5], vp[14]-vp[6], vp[15]-vp[7] };
    // Near:   row3 + row2
    m_frustum.planes[4] = { vp[12]+vp[8], vp[13]+vp[9], vp[14]+vp[10], vp[15]+vp[11] };
    // Far:    row3 - row2
    m_frustum.planes[5] = { vp[12]-vp[8], vp[13]-vp[9], vp[14]-vp[10], vp[15]-vp[11] };

    for (auto& plane : m_frustum.planes) normalisePlane(plane);
}

// ─────────────────────────────────────────────────────────────────────────────
void RenderSystem::update(float deltaTime) {
    m_engineTime += deltaTime;
    m_sortedDrawCalls.clear();
    m_transparentDrawCalls.clear();

    cullAndSort();
}

// ─────────────────────────────────────────────────────────────────────────────
// cullAndSort — the hot path:
//   • Iterate all (Transform, Render) pairs via EnTT view
//   • Frustum cull each entity's bounding sphere
//   • Select LOD mesh based on camera distance
//   • Bucket into opaque / transparent draw lists
// ─────────────────────────────────────────────────────────────────────────────
void RenderSystem::cullAndSort() {
    auto view = m_registry.view<const TransformComponent, const RenderComponent>();

    view.each([this](const TransformComponent& xform, const RenderComponent& render) {
        if (!render.visible) return;

        const Vec3& pos = xform.position;

        // ── Camera distance ────────────────────────────────────────────────
        const float dx  = pos.x - m_cameraPos[0];
        const float dy  = pos.y - m_cameraPos[1];
        const float dz  = pos.z - m_cameraPos[2];
        const float distSq = dx*dx + dy*dy + dz*dz;

        // ── Frustum cull ───────────────────────────────────────────────────
        // Retrieve bounding radius from mesh registry
        float boundingRadius = 1.0f;
        if (render.meshId < m_meshRegistry.size()) {
            boundingRadius = m_meshRegistry[render.meshId].boundingRadius;
        }
        if (!m_frustum.testSphere(pos.x, pos.y, pos.z, boundingRadius)) {
            return; // Culled — no draw call emitted
        }

        // ── LOD selection ──────────────────────────────────────────────────
        uint32_t selectedMesh = render.meshId;
        for (int lod = 3; lod > 0; --lod) {
            const float threshold = render.lodDistances[lod];
            if (render.lodMeshIds[lod] != INVALID_MESH_ID &&
                distSq > threshold * threshold)
            {
                selectedMesh = render.lodMeshIds[lod];
                break;
            }
        }

        // ── Bucket by blend mode ────────────────────────────────────────────
        SortedDrawCall dc {
            .materialId         = render.materialId,
            .meshId             = selectedMesh,
            .entityIdxInView    = 0,    // Set after collection
            .distSq             = distSq,
        };

        m_sortedDrawCalls.push_back(dc);
    });

    // ── Sort opaque: front-to-back (minimises overdraw via early-Z) ────────
    std::sort(m_sortedDrawCalls.begin(), m_sortedDrawCalls.end(),
        [](const SortedDrawCall& a, const SortedDrawCall& b) {
            // Primary: material (minimise pipeline/descriptor changes)
            if (a.materialId != b.materialId) return a.materialId < b.materialId;
            // Secondary: mesh (minimise VB rebinds)
            if (a.meshId != b.meshId)         return a.meshId < b.meshId;
            // Tertiary: distance (front-to-back for early-Z rejection)
            return a.distSq < b.distSq;
        });

    // ── Sort transparent: back-to-front (correct alpha blending) ──────────
    std::sort(m_transparentDrawCalls.begin(), m_transparentDrawCalls.end(),
        [](const SortedDrawCall& a, const SortedDrawCall& b) {
            return a.distSq > b.distSq; // Reverse: far to near
        });
}

// ─────────────────────────────────────────────────────────────────────────────
void RenderSystem::submitDrawCalls(VulkanRenderer& renderer) {
    // For each sorted draw call, emit a DrawCall to the renderer.
    // Adjacent draw calls with the same mesh+material are instanced.
    // For simplicity, we emit individual calls here; see buildInstanceBatches()
    // for the full instancing implementation.
    for (const auto& sdc : m_sortedDrawCalls) {
        DrawCall dc {};
        dc.meshId     = sdc.meshId;
        dc.materialId = sdc.materialId;
        // modelMatrix is filled by RenderSystem before submitting
        // TODO: copy from entity's TransformComponent.getWorldMatrix()
        renderer.submitDrawCall(dc);
    }
}

uint32_t RenderSystem::registerMesh(MeshBuffer&& mesh) {
    const uint32_t id = static_cast<uint32_t>(m_meshRegistry.size());
    m_meshRegistry.push_back(std::move(mesh));
    return id;
}

void RenderSystem::destroy(VkDevice device) {
    for (auto& ubo : m_frameUBOs) ubo.destroy(device);
    m_instanceBuffer.destroy(device);
    for (auto& mesh : m_meshRegistry) {
        mesh.vertexBuffer.destroy(device);
        mesh.indexBuffer.destroy(device);
    }
}

} // namespace hs
