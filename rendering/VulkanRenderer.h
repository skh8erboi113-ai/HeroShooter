// app/src/main/cpp/rendering/VulkanRenderer.h
#pragma once

// Vulkan headers — NDK provides vulkan.h and Android extension headers
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

// AGDK game activity (for native window handle)
#include <game-activity/native_app_glue/android_native_app_glue.h>

// Swappy Vulkan frame pacing
#include <swappy/swappyVk.h>

#include <cstdint>
#include <vector>
#include <array>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// Maximum number of frames processed concurrently.
// 2 = double-buffering (safe, lower latency)
// 3 = triple-buffering (higher throughput at cost of latency)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

namespace hs {

// Forward declarations
class VulkanDevice;
class VulkanSwapchain;
class VulkanPipeline;

// ─────────────────────────────────────────────────────────────────────────────
// DrawCall — submitted by RenderSystem, executed by VulkanRenderer
// ─────────────────────────────────────────────────────────────────────────────
struct DrawCall {
    uint32_t    meshId;
    uint32_t    materialId;
    float       modelMatrix[16];        // Row-major model matrix (memcpy to UBO)
    uint32_t    instanceCount   = 1;
    uint32_t    instanceOffset  = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanRenderer
//
// Manages the Vulkan rendering pipeline for the game.
// Responsibilities:
//   • Instance, device, and surface lifecycle
//   • Swapchain creation/recreation on resize
//   • Frame synchronisation (semaphores, fences)
//   • Command buffer recording and submission
//   • Integration with SwappyVk for frame pacing
// ─────────────────────────────────────────────────────────────────────────────
class VulkanRenderer final {
public:
    explicit VulkanRenderer(android_app* app) noexcept;
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&)            = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // ── Two-phase initialisation ───────────────────────────────────────────
    // initDevice():   Create VkInstance, pick physical device, create VkDevice.
    //                 Can be called before the ANativeWindow is available.
    // createSurface(): Create VkSurfaceKHR from ANativeWindow, build swapchain.
    //                  Called when APP_CMD_INIT_WINDOW fires.
    [[nodiscard]] bool initDevice();
    void createSurface(ANativeWindow* window);
    void destroySurface();
    void shutdown();

    // ── Per-frame API ─────────────────────────────────────────────────────
    void beginFrame();
    void submitDrawCall(const DrawCall& drawCall);
    void endFrame();    // Present + Swappy frame pacing

    // ── Resource management ────────────────────────────────────────────────
    [[nodiscard]] uint32_t uploadMesh(
        const void* vertexData, uint32_t vertexDataSize,
        const void* indexData,  uint32_t indexCount
    );
    void releaseNonEssentialResources();
    void onWindowResize();

    // ── State queries ──────────────────────────────────────────────────────
    [[nodiscard]] bool isSurfaceReady()  const noexcept { return m_surfaceReady; }
    [[nodiscard]] VkDevice device()      const noexcept { return m_device; }
    [[nodiscard]] VkInstance instance()  const noexcept { return m_instance; }

private:
    [[nodiscard]] bool createInstance();
    [[nodiscard]] bool selectPhysicalDevice();
    [[nodiscard]] bool createLogicalDevice();
    [[nodiscard]] bool createSwapchain();
    [[nodiscard]] bool createRenderPass();
    [[nodiscard]] bool createFramebuffers();
    [[nodiscard]] bool createCommandPool();
    [[nodiscard]] bool createCommandBuffers();
    [[nodiscard]] bool createSyncObjects();
    [[nodiscard]] bool createPipeline();
    [[nodiscard]] bool createDescriptorPool();

    void recreateSwapchain();
    void cleanupSwapchain();

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

    // ── Vulkan object handles ──────────────────────────────────────────────
    android_app*        m_app               = nullptr;
    ANativeWindow*      m_window            = nullptr;

    VkInstance          m_instance          = VK_NULL_HANDLE;
    VkPhysicalDevice    m_physicalDevice    = VK_NULL_HANDLE;
    VkDevice            m_device            = VK_NULL_HANDLE;
    VkSurfaceKHR        m_surface           = VK_NULL_HANDLE;
    VkSwapchainKHR      m_swapchain         = VK_NULL_HANDLE;
    VkRenderPass        m_renderPass        = VK_NULL_HANDLE;
    VkPipelineLayout    m_pipelineLayout    = VK_NULL_HANDLE;
    VkPipeline          m_graphicsPipeline  = VK_NULL_HANDLE;
    VkCommandPool       m_commandPool       = VK_NULL_HANDLE;
    VkDescriptorPool    m_descriptorPool    = VK_NULL_HANDLE;

    // Queue family indices
    uint32_t            m_graphicsQueueFamily = UINT32_MAX;
    uint32_t            m_presentQueueFamily  = UINT32_MAX;
    VkQueue             m_graphicsQueue     = VK_NULL_HANDLE;
    VkQueue             m_presentQueue      = VK_NULL_HANDLE;

    // Swapchain images
    std::vector<VkImage>        m_swapchainImages;
    std::vector<VkImageView>    m_swapchainImageViews;
    std::vector<VkFramebuffer>  m_swapchainFramebuffers;
    VkFormat                    m_swapchainFormat   = VK_FORMAT_UNDEFINED;
    VkExtent2D                  m_swapchainExtent   = {0, 0};

    // Depth buffer
    VkImage         m_depthImage        = VK_NULL_HANDLE;
    VkDeviceMemory  m_depthImageMemory  = VK_NULL_HANDLE;
    VkImageView     m_depthImageView    = VK_NULL_HANDLE;

    // Per-frame resources (double/triple buffered)
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_commandBuffers {};
    std::array<VkSemaphore,     MAX_FRAMES_IN_FLIGHT> m_imageAvailableSemaphores {};
    std::array<VkSemaphore,     MAX_FRAMES_IN_FLIGHT> m_renderFinishedSemaphores {};
    std::array<VkFence,         MAX_FRAMES_IN_FLIGHT> m_inFlightFences {};

    // Draw call list — cleared every beginFrame()
    std::vector<DrawCall>   m_pendingDrawCalls;

    // Frame state
    uint32_t    m_currentFrame      = 0;
    uint32_t    m_currentImageIndex = 0;
    bool        m_surfaceReady      = false;
    bool        m_framebufferResized= false;

#ifdef ENABLE_VALIDATION_LAYERS
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
#endif
};

} // namespace hs
