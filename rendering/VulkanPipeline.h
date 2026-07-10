// app/src/main/cpp/rendering/VulkanPipeline.h
#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>
#include <string_view>
#include <array>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Vertex layout — interleaved, 16-byte aligned for NEON-friendly uploads
// Total size: 48 bytes per vertex
// ─────────────────────────────────────────────────────────────────────────────
struct Vertex {
    float position[3];  // 12 bytes — xyz
    float normal[3];    // 12 bytes — xyz
    float texCoord[2];  // 8 bytes  — uv
    float tangent[4];   // 16 bytes — xyzw (w = handedness ±1)

    static constexpr uint32_t kStride = sizeof(Vertex);

    static VkVertexInputBindingDescription getBindingDescription() noexcept {
        return {
            .binding   = 0,
            .stride    = kStride,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
    }

    static std::array<VkVertexInputAttributeDescription, 4>
    getAttributeDescriptions() noexcept {
        return {{
            // location=0: position
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, position) },
            // location=1: normal
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, normal)   },
            // location=2: texcoord
            { 2, 0, VK_FORMAT_R32G32_SFLOAT,        offsetof(Vertex, texCoord) },
            // location=3: tangent (float4 for handedness)
            { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT,  offsetof(Vertex, tangent)  },
        }};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Push constant block — updated per draw call (fastest GPU path)
// Must be <= 128 bytes (guaranteed by Vulkan spec for all implementations)
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) PushConstants {
    float modelMatrix[16];      // 64 bytes — world transform
    uint32_t materialIndex;     // 4  bytes — index into material buffer
    uint32_t entityId;          // 4  bytes — for picking / debug
    float    uvScale[2];        // 8  bytes — tiling override
};                              // = 80 bytes total

static_assert(sizeof(PushConstants) <= 128,
    "PushConstants exceeds Vulkan minimum push constant range");

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame uniform buffer — uploaded once per frame via staging buffer
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(256) FrameUBO {  // 256-byte aligned for descriptor offset requirements
    float     view[16];         // View matrix
    float     projection[16];   // Projection matrix
    float     viewProjection[16]; // Combined VP (saves GPU multiply per vertex)
    float     cameraWorldPos[4];// xyz + padding
    float     ambientLight[4];  // rgb + intensity
    float     time;             // Engine time (for shader animations)
    float     deltaTime;        // Frame delta (for motion blur)
    uint32_t  frameIndex;       // For temporal effects
    float     _pad;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline configuration descriptor
// Supports multiple pipeline variants (opaque, alpha-test, transparent, shadow)
// ─────────────────────────────────────────────────────────────────────────────
enum class PipelineType : uint8_t {
    Opaque       = 0,
    AlphaTest    = 1,
    Transparent  = 2,
    ShadowDepth  = 3,
    Wireframe    = 4,   // Debug only
    Count
};

struct PipelineCreateDesc {
    PipelineType    type                = PipelineType::Opaque;
    VkRenderPass    renderPass          = VK_NULL_HANDLE;
    VkExtent2D      extent              = {};
    const uint32_t* vertSpirv           = nullptr;
    uint32_t        vertSpirvSize       = 0;    // Bytes
    const uint32_t* fragSpirv           = nullptr;
    uint32_t        fragSpirvSize       = 0;
    bool            enableDepthTest     = true;
    bool            enableDepthWrite    = true;
    bool            enableBlending      = false;
    VkCullModeFlags cullMode            = VK_CULL_MODE_BACK_BIT;
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanPipeline
//
// Encapsulates a single VkPipeline + its associated layout and descriptor sets.
// The engine maintains a small cache of pipeline variants (opaque/transparent/etc.)
// ─────────────────────────────────────────────────────────────────────────────
class VulkanPipeline final {
public:
    VulkanPipeline()  noexcept = default;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&)            = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;
    VulkanPipeline(VulkanPipeline&&)                 noexcept;
    VulkanPipeline& operator=(VulkanPipeline&&)      noexcept;

    // ── Build ──────────────────────────────────────────────────────────────
    [[nodiscard]] bool build(
        VkDevice                    device,
        VkDescriptorSetLayout       frameSetLayout,
        VkDescriptorSetLayout       materialSetLayout,
        const PipelineCreateDesc&   desc
    );

    void destroy(VkDevice device) noexcept;

    // ── Bind ───────────────────────────────────────────────────────────────
    void bind(VkCommandBuffer cmd) const noexcept;

    void pushConstants(
        VkCommandBuffer         cmd,
        const PushConstants&    pc
    ) const noexcept;

    // ── Accessors ──────────────────────────────────────────────────────────
    [[nodiscard]] VkPipeline       handle() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return m_layout;   }
    [[nodiscard]] bool             isValid() const noexcept {
        return m_pipeline != VK_NULL_HANDLE;
    }

private:
    [[nodiscard]] VkShaderModule createShaderModule(
        VkDevice         device,
        const uint32_t*  spirv,
        uint32_t         sizeBytes
    ) const noexcept;

    VkPipeline          m_pipeline  = VK_NULL_HANDLE;
    VkPipelineLayout    m_layout    = VK_NULL_HANDLE;
};

} // namespace hs
