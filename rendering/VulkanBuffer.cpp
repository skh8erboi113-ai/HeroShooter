// app/src/main/cpp/rendering/VulkanBuffer.cpp
#include "VulkanBuffer.h"
#include "../utils/Logger.h"

#include <cstring>  // memcpy
#include <utility>  // std::swap

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept {
    m_buffer    = other.m_buffer;
    m_memory    = other.m_memory;
    m_mappedPtr = other.m_mappedPtr;
    m_size      = other.m_size;
    other.m_buffer    = VK_NULL_HANDLE;
    other.m_memory    = VK_NULL_HANDLE;
    other.m_mappedPtr = nullptr;
    other.m_size      = 0;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        std::swap(m_buffer,    other.m_buffer);
        std::swap(m_memory,    other.m_memory);
        std::swap(m_mappedPtr, other.m_mappedPtr);
        std::swap(m_size,      other.m_size);
    }
    return *this;
}

VulkanBuffer::~VulkanBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
        LOG_WARN("VulkanBuffer: destroyed without destroy() — "
                 "pass device handle to destroy() before destruction");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t VulkanBuffer::findMemoryType(
    VkPhysicalDevice        physicalDevice,
    uint32_t                typeFilter,
    VkMemoryPropertyFlags   properties)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    LOG_ERROR("VulkanBuffer: no suitable memory type found "
              "(filter=0x%08X, props=0x%08X)", typeFilter, properties);
    return UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanBuffer::allocate(
    VkDevice                device,
    VkPhysicalDevice        physicalDevice,
    VkDeviceSize            size,
    VkBufferUsageFlags      usage,
    VkMemoryPropertyFlags   memProps)
{
    m_size = size;

    // ── Create VkBuffer ────────────────────────────────────────────────────
    VkBufferCreateInfo bufferInfo {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &m_buffer);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateBuffer failed: %d", result);
        return false;
    }

    // ── Query memory requirements ──────────────────────────────────────────
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, m_buffer, &memReqs);

    const uint32_t memTypeIndex = findMemoryType(
        physicalDevice, memReqs.memoryTypeBits, memProps);
    if (memTypeIndex == UINT32_MAX) return false;

    // ── Allocate device memory ─────────────────────────────────────────────
    // In production, use a memory allocator (VMA) to sub-allocate from large
    // heaps — calling vkAllocateMemory per buffer quickly hits device limits.
    VkMemoryAllocateInfo allocInfo {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = memTypeIndex,
    };
    result = vkAllocateMemory(device, &allocInfo, nullptr, &m_memory);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkAllocateMemory failed: %d (size=%llu)", result,
                  static_cast<unsigned long long>(memReqs.size));
        return false;
    }

    // ── Bind buffer to memory ──────────────────────────────────────────────
    result = vkBindBufferMemory(device, m_buffer, m_memory, 0);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkBindBufferMemory failed: %d", result);
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanBuffer::createDeviceLocal(
    VkDevice            device,
    VkPhysicalDevice    physicalDevice,
    VkDeviceSize        size,
    VkBufferUsageFlags  usage)
{
    // Device-local memory is fastest for GPU reads but not CPU-mappable.
    // We always add TRANSFER_DST so staging uploads work.
    return allocate(device, physicalDevice, size,
                    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanBuffer::createHostVisible(
    VkDevice            device,
    VkPhysicalDevice    physicalDevice,
    VkDeviceSize        size,
    VkBufferUsageFlags  usage,
    bool                persistentMap)
{
    const bool ok = allocate(
        device, physicalDevice, size,
        usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        // HOST_COHERENT avoids explicit flush/invalidate calls
    );
    if (!ok) return false;

    if (persistentMap) {
        // Keep mapped for the lifetime of the buffer — valid for UBOs
        VkResult result = vkMapMemory(device, m_memory, 0, size, 0, &m_mappedPtr);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkMapMemory (persistent) failed: %d", result);
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanBuffer::uploadToDeviceLocal(
    VkDevice            device,
    VkPhysicalDevice    physicalDevice,
    VkCommandPool       commandPool,
    VkQueue             queue,
    VulkanBuffer&       destBuffer,
    const void*         data,
    VkDeviceSize        size,
    VkBufferUsageFlags  usage)
{
    // ── Create staging buffer ──────────────────────────────────────────────
    VulkanBuffer staging;
    if (!staging.createHostVisible(device, physicalDevice, size,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true))
    {
        LOG_ERROR("Failed to create staging buffer (%llu bytes)",
                  static_cast<unsigned long long>(size));
        return false;
    }

    // ── Upload to staging ──────────────────────────────────────────────────
    staging.write(data, size);

    // ── Create destination buffer ──────────────────────────────────────────
    if (!destBuffer.createDeviceLocal(device, physicalDevice, size, usage)) {
        staging.destroy(device);
        return false;
    }

    // ── Record and submit copy command ─────────────────────────────────────
    VkCommandBufferAllocateInfo allocInfo {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion { .size = size };
    vkCmdCopyBuffer(cmd, staging.handle(), destBuffer.handle(), 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    // Submit and wait synchronously — for loading, this is acceptable.
    // For runtime streaming, use a dedicated transfer queue + semaphore.
    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };
    VkFence fence;
    vkCreateFence(device, &fenceInfo, nullptr, &fence);

    VkSubmitInfo submitInfo {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    vkQueueSubmit(queue, 1, &submitInfo, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    staging.destroy(device);

    LOG_INFO("VulkanBuffer: uploaded %llu bytes to device-local buffer",
             static_cast<unsigned long long>(size));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanBuffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset) noexcept {
    if (!m_mappedPtr) {
        LOG_ERROR("VulkanBuffer::write called on unmapped buffer");
        return;
    }
    memcpy(static_cast<uint8_t*>(m_mappedPtr) + offset, data, size);
}

void VulkanBuffer::flush(VkDevice device, VkDeviceSize size, VkDeviceSize offset) noexcept {
    // Only needed when NOT using HOST_COHERENT_BIT (we always use coherent)
    VkMappedMemoryRange range {
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = m_memory,
        .offset = offset,
        .size   = size,
    };
    vkFlushMappedMemoryRanges(device, 1, &range);
}

void VulkanBuffer::destroy(VkDevice device) noexcept {
    if (m_mappedPtr) {
        vkUnmapMemory(device, m_memory);
        m_mappedPtr = nullptr;
    }
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
    m_size = 0;
}

} // namespace hs
