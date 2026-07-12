// app/src/main/cpp/debug/DebugRenderer.h
#pragma once

#ifdef ENGINE_DEBUG_BUILD

#include <vulkan/vulkan.h>
#include "../rendering/VulkanBuffer.h"
#include "../utils/Logger.h"

#include <vector>
#include <array>
#include <cstdint>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// DebugLine — single line primitive for debug visualisation
// ─────────────────────────────────────────────────────────────────────────────
struct DebugLine {
    float startX, startY, startZ;
    float endX,   endY,   endZ;
    float r, g, b, a;           // Colour (linear)
    float duration;             // Seconds before disappearing (0 = one frame)
};

// ─────────────────────────────────────────────────────────────────────────────
// DebugRenderer
//
// Immediate-mode debug drawing system.
// Draw calls accumulate each frame and are flushed to a line list pipeline.
// Uses a separate debug pipeline that bypasses depth testing for visibility.
//
// Usage (from anywhere with access to DebugRenderer):
//   debug.drawLine({0,0,0}, {1,0,0}, {1,0,0,1});   // Red X axis
//   debug.drawBox(center, halfExtents, {0,1,0,1});  // Green AABB
//   debug.drawSphere(pos, radius, {0,0,1,1});       // Blue sphere
//   debug.drawText(screenPos, "Health: 100");        // Screen-space text
// ─────────────────────────────────────────────────────────────────────────────
class DebugRenderer final {
public:
    DebugRenderer()  noexcept = default;
    ~DebugRenderer() = default;

    [[nodiscard]] bool init(VkDevice device, VkPhysicalDevice physDevice,
                            VkRenderPass renderPass, uint32_t maxFrames);
    void shutdown(VkDevice device);

    // ── Immediate draw calls (valid between beginFrame/endFrame) ───────────
    void drawLine(
        float x1, float y1, float z1,
        float x2, float y2, float z2,
        float r, float g, float b, float a = 1.0f,
        float duration = 0.0f
    ) noexcept;

    void drawAABB(
        float cx, float cy, float cz,          // Centre
        float hx, float hy, float hz,          // Half-extents
        float r, float g, float b, float a = 1.0f
    ) noexcept;

    void drawSphere(
        float cx, float cy, float cz,
        float radius,
        float r, float g, float b, float a = 1.0f,
        int   segments = 16
    ) noexcept;

    void drawCross(
        float cx, float cy, float cz,
        float size,
        float r, float g, float b, float a = 1.0f
    ) noexcept;

    // ── Frame control ──────────────────────────────────────────────────────
    void beginFrame(float deltaTime);
    void flush(VkCommandBuffer cmd, VkPipelineLayout layout,
               const float* viewProjMatrix, uint32_t frameIndex);

    // ── Stats overlay ──────────────────────────────────────────────────────
    void displayStats(float fps, float frameTimeMs,
                      size_t entityCount, float physicsMs,
                      uint32_t drawCalls) noexcept;

private:
    static constexpr uint32_t kMaxDebugLines = 32768;

    struct LineVertex {
        float position[3];
        float colour[4];
    };

    // Per-frame line buffer (host-visible, persistently mapped)
    std::vector<VulkanBuffer>           m_lineVertexBuffers;
    std::vector<std::vector<LineVertex>> m_lineData;

    // Persistent lines (duration > 0)
    std::vector<DebugLine>              m_persistentLines;

    uint32_t    m_maxFrames     = 2;
    bool        m_initialised   = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScopedDebugGroup — RAII Vulkan debug label (visible in RenderDoc / Snapdragon Profiler)
// ─────────────────────────────────────────────────────────────────────────────
struct ScopedDebugGroup {
#ifdef ENABLE_VALIDATION_LAYERS
    ScopedDebugGroup(VkCommandBuffer cmd, const char* name,
                     float r=1, float g=1, float b=1, float a=1)
        : m_cmd(cmd)
    {
        VkDebugUtilsLabelEXT label {
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = name,
            .color      = { r, g, b, a },
        };
        auto fn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr,  // Stored globally in production
                                  "vkCmdBeginDebugUtilsLabelEXT"));
        if (fn) fn(cmd, &label);
    }
    ~ScopedDebugGroup() {
        auto fn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr, "vkCmdEndDebugUtilsLabelEXT"));
        if (fn) fn(m_cmd);
    }
private:
    VkCommandBuffer m_cmd;
#else
    ScopedDebugGroup(VkCommandBuffer, const char*,
                     float=1, float=1, float=1, float=1) {}
#endif
};

#define DEBUG_GPU_SCOPE(cmd, name) \
    hs::ScopedDebugGroup _dbgScope##__LINE__(cmd, name)

} // namespace hs

#endif // ENGINE_DEBUG_BUILD
