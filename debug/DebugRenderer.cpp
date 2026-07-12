// app/src/main/cpp/debug/DebugRenderer.cpp
#include "DebugRenderer.h"

#ifdef ENGINE_DEBUG_BUILD

#include <cmath>
#include <algorithm>
#include <cstring>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
bool DebugRenderer::init(
    VkDevice         device,
    VkPhysicalDevice physDevice,
    VkRenderPass     /*renderPass*/,
    uint32_t         maxFrames)
{
    m_maxFrames = maxFrames;
    m_lineVertexBuffers.resize(maxFrames);
    m_lineData.resize(maxFrames);

    for (uint32_t i = 0; i < maxFrames; ++i) {
        m_lineData[i].reserve(kMaxDebugLines * 2); // 2 vertices per line

        if (!m_lineVertexBuffers[i].createHostVisible(
                device, physDevice,
                kMaxDebugLines * 2 * sizeof(LineVertex),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                true /* persistent map */))
        {
            LOG_ERROR("DebugRenderer: failed to create line vertex buffer %u", i);
            return false;
        }
    }

    m_initialised = true;
    LOG_INFO("DebugRenderer: initialised (%u frames, %u max lines)",
             maxFrames, kMaxDebugLines);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::beginFrame(float deltaTime) {
    // Age out persistent lines
    m_persistentLines.erase(
        std::remove_if(m_persistentLines.begin(), m_persistentLines.end(),
            [deltaTime](DebugLine& line) {
                line.duration -= deltaTime;
                return line.duration <= 0.0f;
            }),
        m_persistentLines.end()
    );
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::drawLine(
    float x1, float y1, float z1,
    float x2, float y2, float z2,
    float r,  float g,  float b, float a,
    float duration) noexcept
{
    if (duration > 0.0f) {
        m_persistentLines.push_back({ x1,y1,z1, x2,y2,z2, r,g,b,a, duration });
        return;
    }
    // Immediate line — added to current frame's buffer in flush()
    // For now just store in persistent list with 0 duration
    m_persistentLines.push_back({ x1,y1,z1, x2,y2,z2, r,g,b,a, -1.0f });
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::drawAABB(
    float cx, float cy, float cz,
    float hx, float hy, float hz,
    float r, float g, float b, float a) noexcept
{
    // Draw 12 edges of an axis-aligned bounding box
    // Vertices of the box:
    const float x0 = cx-hx, x1 = cx+hx;
    const float y0 = cy-hy, y1 = cy+hy;
    const float z0 = cz-hz, z1 = cz+hz;

    // Bottom face (y0)
    drawLine(x0,y0,z0, x1,y0,z0, r,g,b,a);
    drawLine(x1,y0,z0, x1,y0,z1, r,g,b,a);
    drawLine(x1,y0,z1, x0,y0,z1, r,g,b,a);
    drawLine(x0,y0,z1, x0,y0,z0, r,g,b,a);
    // Top face (y1)
    drawLine(x0,y1,z0, x1,y1,z0, r,g,b,a);
    drawLine(x1,y1,z0, x1,y1,z1, r,g,b,a);
    drawLine(x1,y1,z1, x0,y1,z1, r,g,b,a);
    drawLine(x0,y1,z1, x0,y1,z0, r,g,b,a);
    // Vertical edges
    drawLine(x0,y0,z0, x0,y1,z0, r,g,b,a);
    drawLine(x1,y0,z0, x1,y1,z0, r,g,b,a);
    drawLine(x1,y0,z1, x1,y1,z1, r,g,b,a);
    drawLine(x0,y0,z1, x0,y1,z1, r,g,b,a);
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::drawSphere(
    float cx, float cy, float cz,
    float radius, float r, float g, float b, float a,
    int segments) noexcept
{
    const float step = 6.2832f / static_cast<float>(segments);

    // Three circles: XY, XZ, YZ planes
    for (int i = 0; i < segments; ++i) {
        const float a0 = step * i;
        const float a1 = step * (i + 1);
        const float c0 = std::cos(a0) * radius;
        const float s0 = std::sin(a0) * radius;
        const float c1 = std::cos(a1) * radius;
        const float s1 = std::sin(a1) * radius;

        // XY plane
        drawLine(cx+c0, cy+s0, cz,    cx+c1, cy+s1, cz,    r,g,b,a);
        // XZ plane
        drawLine(cx+c0, cy,    cz+s0, cx+c1, cy,    cz+s1, r,g,b,a);
        // YZ plane
        drawLine(cx,    cy+c0, cz+s0, cx,    cy+c1, cz+s1, r,g,b,a);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::drawCross(
    float cx, float cy, float cz, float size,
    float r, float g, float b, float a) noexcept
{
    drawLine(cx-size, cy, cz,  cx+size, cy, cz,  r,g,b,a);
    drawLine(cx, cy-size, cz,  cx, cy+size, cz,  r,g,b,a);
    drawLine(cx, cy, cz-size,  cx, cy, cz+size,  r,g,b,a);
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::flush(
    VkCommandBuffer cmd,
    VkPipelineLayout layout,
    const float*    viewProjMatrix,
    uint32_t        frameIndex)
{
    const uint32_t fi = frameIndex % m_maxFrames;
    auto& lineData = m_lineData[fi];
    lineData.clear();

    // Collect all active lines (immediate + persistent)
    for (const auto& line : m_persistentLines) {
        if (lineData.size() + 2 > kMaxDebugLines * 2) break;

        lineData.push_back({
            { line.startX, line.startY, line.startZ },
            { line.r, line.g, line.b, line.a }
        });
        lineData.push_back({
            { line.endX, line.endY, line.endZ },
            { line.r, line.g, line.b, line.a }
        });
    }

    // Remove single-frame persistent lines
    m_persistentLines.erase(
        std::remove_if(m_persistentLines.begin(), m_persistentLines.end(),
            [](const DebugLine& l) { return l.duration < 0.0f; }),
        m_persistentLines.end()
    );

    if (lineData.empty()) return;

    // Upload to GPU
    const size_t dataSize = lineData.size() * sizeof(LineVertex);
    m_lineVertexBuffers[fi].write(lineData.data(), dataSize);

    // Push the ViewProjection matrix as a push constant
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(float) * 16, viewProjMatrix);

    // Bind vertex buffer and draw
    VkDeviceSize offsets[] = { 0 };
    VkBuffer vb = m_lineVertexBuffers[fi].handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
    vkCmdDraw(cmd, static_cast<uint32_t>(lineData.size()), 1, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::displayStats(
    float fps, float frameTimeMs,
    size_t entityCount, float physicsMs,
    uint32_t drawCalls) noexcept
{
    LOG_INFO("[STATS] FPS=%.1f | Frame=%.2fms | Entities=%zu | "
             "Physics=%.2fms | DrawCalls=%u",
             fps, frameTimeMs, entityCount, physicsMs, drawCalls);
}

// ─────────────────────────────────────────────────────────────────────────────
void DebugRenderer::shutdown(VkDevice device) {
    for (auto& buf : m_lineVertexBuffers) buf.destroy(device);
    m_lineVertexBuffers.clear();
    m_persistentLines.clear();
    m_initialised = false;
}

} // namespace hs

#endif // ENGINE_DEBUG_BUILD
