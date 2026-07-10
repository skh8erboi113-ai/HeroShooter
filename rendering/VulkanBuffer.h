// app/src/main/cpp/rendering/VulkanBuffer.h
#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstddef>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// VulkanBuffer
//
// RAII wrapper for a VkBuffer + VkDeviceMemory pair.
// Supports:
//   • Device-local buffers (GPU-only, fastest — vertex/index/uniform)
//   • Host-visible buffers (CPU mappable — staging buffers)
//   • Persistent mapping for frequently updated UBOs
//
// Upload workflow for device-local geometry:
//   1. Create a host-visible staging buffer
//   2. memcpy data into the mapped staging buffer
//   3. Submit a vkCmdCopyBuffer command to copy to device-local buffer
//   4. Destroy the staging buffer after submission completes
//
// For per-frame UBOs (FrameUBO), use persistent mapping:
//   buffer.mapPersistent() at init, then memcpy every frame — zero overhead.
// ─────────────────────────────────────────────────────────────────────────────
class VulkanBuffer final {
public:
    VulkanBuffer()  noexcept = default;
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&)            = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&)                 noexcept;
    VulkanBuffer& operator=(VulkanBuffer&&)      noexcept;

    // ── Creation ───────────────────────────────────────────────────────────
    [[nodiscard]] bool createDeviceLocal(
        VkDevice            device,
        VkPhysicalDevice    physicalDevice,
        VkDeviceSize        size,
        VkBufferUsageFlags  usage           // e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );

    [[nodiscard]] bool createHostVisible(
        VkDevice            device,
        VkPhysicalDevice    physicalDevice,
        VkDeviceSize        size,
        VkBufferUsageFlags  usage,
        bool                persistentMap   = false
    );

    // ── Staging upload helper (convenience) ───────────────────────────────
    // Creates a staging buffer, copies data, records transfer command,
    // then cleans up the staging buffer after fence signals.
    [[nodiscard]] static bool uploadToDeviceLocal(
        VkDevice            device,
        VkPhysicalDevice    physicalDevice,
        VkCommandPool       commandPool,
        VkQueue             queue,
        VulkanBuffer&       destBuffer,
        const void*         data,
        VkDeviceSize        size,
        VkBufferUsageFlags  usage
    );

    // ── Data access ───────────────────────────────────────────────────────
    // For persistently mapped buffers — no map/unmap needed
    void  write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0) noexcept;
    void  flush(VkDevice device, VkDeviceSize size, VkDeviceSize offset = 0) noexcept;
    void* mappedPtr() const noexcept { return m_mappedPtr; }

    // ── Destroy ────────────────────────────────────────────────────────────
    void destroy(VkDevice device) noexcept;

    // ── Accessors ──────────────────────────────────────────────────────────
    [[nodiscard]] VkBuffer       handle()     const noexcept { return m_buffer;  }
    [[nodiscard]] VkDeviceMemory memory()     const noexcept { return m_memory;  }
    [[nodiscard]] VkDeviceSize   size()       const noexcept { return m_size;    }
    [[nodiscard]] bool           isValid()    const noexcept {
        return m_buffer != VK_NULL_HANDLE;
    }

private:
    [[nodiscard]] static uint32_t findMemoryType(
        VkPhysicalDevice            physicalDevice,
        uint32_t                    typeFilter,
        VkMemoryPropertyFlags       properties
    );

    [[nodiscard]] bool allocate(
        VkDevice                device,
        VkPhysicalDevice        physicalDevice,
        VkDeviceSize            size,
        VkBufferUsageFlags      usage,
        VkMemoryPropertyFlags   memProps
    );

    VkBuffer        m_buffer        = VK_NULL_HANDLE;
    VkDeviceMemory  m_memory        = VK_NULL_HANDLE;
    void*           m_mappedPtr     = nullptr;
    VkDeviceSize    m_size          = 0;
};

} // namespace hs
