// app/src/main/cpp/assets/AssetManager.h
#pragma once

#include "../rendering/VulkanBuffer.h"
#include "../rendering/VulkanRenderer.h"
#include "../rendering/ShaderManager.h"
#include "../ecs/systems/RenderSystem.h"
#include "../memory/LinearAllocator.h"
#include "../utils/Logger.h"

#include <android/asset_manager.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// TextureHandle — opaque handle to a GPU texture
// ─────────────────────────────────────────────────────────────────────────────
struct GpuTexture {
    VkImage         image       = VK_NULL_HANDLE;
    VkDeviceMemory  memory      = VK_NULL_HANDLE;
    VkImageView     imageView   = VK_NULL_HANDLE;
    VkSampler       sampler     = VK_NULL_HANDLE;
    uint32_t        width       = 0;
    uint32_t        height      = 0;
    uint32_t        mipLevels   = 1;
    VkFormat        format      = VK_FORMAT_UNDEFINED;
};

// ─────────────────────────────────────────────────────────────────────────────
// OBJ-like mesh data (CPU side, before GPU upload)
// In production: use glTF 2.0 (via cgltf) for PBR material support
// ─────────────────────────────────────────────────────────────────────────────
struct MeshData {
    std::vector<float>    vertices;   // Interleaved: pos(3)+normal(3)+uv(2)+tangent(4)
    std::vector<uint32_t> indices;
    float                 boundingRadius = 1.0f;
    std::string           name;
};

// ─────────────────────────────────────────────────────────────────────────────
// AssetManager
//
// Central registry for all game assets.
// Handles:
//   • Loading mesh/texture data from APK assets via AAssetManager
//   • Uploading to GPU via staging buffers
//   • Reference-counted caching (same asset loaded once, shared)
//   • Async loading interface (results via JobHandle)
// ─────────────────────────────────────────────────────────────────────────────
class AssetManager final {
public:
    AssetManager() noexcept = default;
    ~AssetManager()         = default;

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    [[nodiscard]] bool init(
        AAssetManager*   androidAssets,
        VkDevice         device,
        VkPhysicalDevice physDevice,
        VkCommandPool    commandPool,
        VkQueue          transferQueue,
        RenderSystem&    renderSystem
    );

    void shutdown(VkDevice device);

    // ── Synchronous loading ────────────────────────────────────────────────
    [[nodiscard]] uint32_t loadMesh(const std::string& path);
    [[nodiscard]] uint32_t loadTexture(const std::string& path, VkDevice device,
                                       VkPhysicalDevice physDevice,
                                       VkCommandPool commandPool,
                                       VkQueue queue);

    // ── Built-in procedural meshes (generated on CPU, uploaded to GPU) ─────
    [[nodiscard]] uint32_t createBoxMesh(float hx, float hy, float hz);
    [[nodiscard]] uint32_t createSphereMesh(float radius, int segments);
    [[nodiscard]] uint32_t createQuadMesh(float width, float height);
    [[nodiscard]] uint32_t createCapsuleMesh(float halfHeight, float radius, int segments);

    // ── Accessors ──────────────────────────────────────────────────────────
    [[nodiscard]] const GpuTexture* getTexture(uint32_t id) const;

private:
    [[nodiscard]] bool loadMeshFromAsset(const std::string& path, MeshData& out);
    [[nodiscard]] bool uploadMesh(const MeshData& data, uint32_t* outId);
    void generateTangents(MeshData& data);

    AAssetManager*  m_androidAssets     = nullptr;
    VkDevice        m_device            = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice       = VK_NULL_HANDLE;
    VkCommandPool   m_commandPool       = VK_NULL_HANDLE;
    VkQueue         m_transferQueue     = VK_NULL_HANDLE;
    RenderSystem*   m_renderSystem      = nullptr;

    // Frame-scoped scratch allocator for mesh processing
    LinearAllocator m_scratchAllocator { 8 * 1024 * 1024 }; // 8 MB scratch

    // Caches — keyed by asset path
    std::unordered_map<std::string, uint32_t>   m_meshCache;
    std::unordered_map<std::string, uint32_t>   m_textureCache;

    // Owned textures
    std::vector<GpuTexture> m_textures;
};

} // namespace hs
