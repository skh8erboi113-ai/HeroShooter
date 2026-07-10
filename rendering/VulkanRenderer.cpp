// app/src/main/cpp/rendering/VulkanRenderer.cpp
#include "VulkanRenderer.h"
#include "ShaderManager.h"
#include "../utils/Logger.h"
#include "../memory/LinearAllocator.h"

#include <android/asset_manager.h>

// Vulkan validation layer name
static constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

// Required device extensions
static constexpr const char* kRequiredDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    // Android-specific extensions
    "VK_KHR_maintenance1",              // Negative viewport height
    "VK_EXT_memory_priority",           // Let Jolt/us mark buffers as high priority
};

#ifdef ENABLE_VALIDATION_LAYERS
// Validation layer debug messenger callback
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       /*pUserData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_ERROR("VK Validation: %s", pCallbackData->pMessage);
    } else {
        LOG_INFO("VK Validation: %s", pCallbackData->pMessage);
    }
    return VK_FALSE;    // Don't abort the Vulkan call
}
#endif

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
VulkanRenderer::VulkanRenderer(android_app* app) noexcept
    : m_app(app)
{}

VulkanRenderer::~VulkanRenderer() {
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::initDevice() {
    LOG_INFO("VulkanRenderer::initDevice()");

    if (!createInstance())       { LOG_ERROR("createInstance failed");       return false; }
    if (!selectPhysicalDevice()) { LOG_ERROR("selectPhysicalDevice failed"); return false; }
    if (!createLogicalDevice())  { LOG_ERROR("createLogicalDevice failed");  return false; }
    if (!createCommandPool())    { LOG_ERROR("createCommandPool failed");     return false; }
    if (!createDescriptorPool()) { LOG_ERROR("createDescriptorPool failed"); return false; }

    // Initialise Swappy for Vulkan frame pacing
    // SwappyVk requires the physical device and logical device to be ready
    SwappyVk_initAndGetRefreshCycleDuration(
        /* env          */ nullptr,          // JNI env not needed for Vulkan path
        /* activity     */ m_app->activity->javaGameActivity,
        m_physicalDevice,
        m_device,
        m_graphicsQueue,
        nullptr                             // refreshDuration (unused here)
    );
    SwappyVk_setWindow(m_device, m_swapchain, m_app->window);

    LOG_INFO("VulkanRenderer: device initialised");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createInstance() {
    VkApplicationInfo appInfo {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "HeroShooterEngine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName        = "HS Engine",
        .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion         = VK_API_VERSION_1_1,   // Vulkan 1.1 guaranteed on API 28+
    };

    // Required instance extensions for Android + validation
    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };

#ifdef ENABLE_VALIDATION_LAYERS
    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &appInfo,
        .enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data(),
    };

#ifdef ENABLE_VALIDATION_LAYERS
    // Verify validation layer is available
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    bool validationAvailable = false;
    for (const auto& layer : layers) {
        if (std::string_view(layer.layerName) == kValidationLayerName) {
            validationAvailable = true;
            break;
        }
    }

    if (validationAvailable) {
        createInfo.enabledLayerCount   = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayerName;

        // Attach validation messenger at instance creation time
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback,
        };
        createInfo.pNext = &debugCreateInfo;
        LOG_INFO("Vulkan validation layers ENABLED");
    } else {
        LOG_WARN("Validation layers requested but not available on this device");
    }
#endif

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateInstance failed: %d", result);
        return false;
    }

#ifdef ENABLE_VALIDATION_LAYERS
    if (validationAvailable) {
        auto createDebugFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createDebugFn) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo {
                .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                 | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                 | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = debugCallback,
            };
            createDebugFn(m_instance, &messengerInfo, nullptr, &m_debugMessenger);
        }
    }
#endif

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        LOG_ERROR("No Vulkan physical devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // On Android, there is typically only one physical device.
    // In production: score devices by featureSet, maxImageDimension2D, etc.
    m_physicalDevice = devices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    LOG_INFO("Selected GPU: %s (API: %u.%u.%u)",
             props.deviceName,
             VK_VERSION_MAJOR(props.apiVersion),
             VK_VERSION_MINOR(props.apiVersion),
             VK_VERSION_PATCH(props.apiVersion));

    // Find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsQueueFamily = i;
            m_presentQueueFamily  = i;   // On Android, graphics == present
            break;
        }
    }

    if (m_graphicsQueueFamily == UINT32_MAX) {
        LOG_ERROR("No graphics queue family found");
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createLogicalDevice() {
    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_graphicsQueueFamily,
        .queueCount       = 1,
        .pQueuePriorities = &queuePriority,
    };

    // Request physical device features needed for the game
    VkPhysicalDeviceFeatures deviceFeatures {};
    deviceFeatures.samplerAnisotropy      = VK_TRUE;
    deviceFeatures.fillModeNonSolid       = VK_TRUE;    // Wireframe debug mode
    deviceFeatures.multiDrawIndirect      = VK_TRUE;    // Indirect draw for instancing
    deviceFeatures.drawIndirectFirstInstance = VK_TRUE;

    VkDeviceCreateInfo createInfo {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueCreateInfo,
        .enabledExtensionCount   = sizeof(kRequiredDeviceExtensions) / sizeof(char*),
        .ppEnabledExtensionNames = kRequiredDeviceExtensions,
        .pEnabledFeatures        = &deviceFeatures,
    };

    VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDevice failed: %d", result);
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamily,  0, &m_presentQueue);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createSurface(ANativeWindow* window) {
    LOG_INFO("VulkanRenderer::createSurface()");
    m_window = window;

    // Create Vulkan surface from ANativeWindow
    VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo {
        .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = window,
    };
    VkResult result = vkCreateAndroidSurfaceKHR(
        m_instance, &surfaceCreateInfo, nullptr, &m_surface);

    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateAndroidSurfaceKHR failed: %d", result);
        return;
    }

    // Now that we have a surface, build the swapchain and pipeline
    if (createSwapchain()      &&
        createRenderPass()     &&
        createFramebuffers()   &&
        createCommandBuffers() &&
        createSyncObjects()    &&
        createPipeline())
    {
        // Notify Swappy of the swapchain
        SwappyVk_setWindow(m_device, m_swapchain, window);
        m_surfaceReady = true;
        LOG_INFO("VulkanRenderer: surface ready, swapchain extent=%ux%u",
                 m_swapchainExtent.width, m_swapchainExtent.height);
    } else {
        LOG_ERROR("Swapchain/pipeline creation failed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createSwapchain() {
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_physicalDevice, m_surface, &capabilities);

    // Choose surface format — prefer RGBA8 SRGB
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_R8G8B8A8_SRGB &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = fmt;
            break;
        }
    }
    m_swapchainFormat = chosenFormat.format;

    // Present mode: FIFO is the only guaranteed mode on Android.
    // Swappy overrides this internally for optimal pacing.
    const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Swapchain extent = window size
    m_swapchainExtent = capabilities.currentExtent;
    if (m_swapchainExtent.width == 0 || m_swapchainExtent.height == 0) {
        LOG_WARN("Swapchain extent is 0 — deferring creation");
        return false;
    }

    // Request double-buffering (minImageCount+1 for triple, but Swappy handles this)
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = m_surface,
        .minImageCount    = imageCount,
        .imageFormat      = chosenFormat.format,
        .imageColorSpace  = chosenFormat.colorSpace,
        .imageExtent      = m_swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,    // Same queue for graphics+present
        .preTransform     = capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
    };

    VkResult result = vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateSwapchainKHR failed: %d", result);
        return false;
    }

    // Retrieve swapchain images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    // Create image views
    m_swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo {
            .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image      = m_swapchainImages[i],
            .viewType   = VK_IMAGE_VIEW_TYPE_2D,
            .format     = m_swapchainFormat,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createRenderPass() {
    // ── Colour attachment ──────────────────────────────────────────────────
    VkAttachmentDescription colorAttachment {
        .format         = m_swapchainFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    // ── Depth attachment ───────────────────────────────────────────────────
    VkAttachmentDescription depthAttachment {
        .format         = VK_FORMAT_D32_SFLOAT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    const VkAttachmentReference colorRef {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference depthRef {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &colorRef,
        .pDepthStencilAttachment = &depthRef,
    };

    // Subpass dependency to ensure colour is written before present
    VkSubpassDependency dependency {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                       | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                       | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                       | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment, depthAttachment
    };

    VkRenderPassCreateInfo renderPassInfo {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments    = attachments.data(),
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateRenderPass failed: %d", result);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        // RESET_COMMAND_BUFFER_BIT allows per-frame re-recording
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_graphicsQueueFamily,
    };
    VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateCommandPool failed: %d", result);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = m_commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    VkResult result = vkAllocateCommandBuffers(
        m_device, &allocInfo, m_commandBuffers.data());
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkAllocateCommandBuffers failed: %d", result);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // Start signalled (no first-frame stall)
    };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                              &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                              &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr,
                          &m_inFlightFences[i]) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create sync objects for frame %u", i);
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::beginFrame() {
    // Wait for the previous use of this frame slot to finish
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame],
                    VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image
    VkResult result = vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &m_currentImageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    // Reset and begin the command buffer for this frame
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass
    const std::array<VkClearValue, 2> clearValues {{
        { .color = {{ 0.05f, 0.05f, 0.08f, 1.0f }} },  // Dark blue-grey sky
        { .depthStencil = { 1.0f, 0 } }
    }};

    VkRenderPassBeginInfo renderPassInfo {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = m_renderPass,
        .framebuffer     = m_swapchainFramebuffers[m_currentImageIndex],
        .renderArea      = { .offset = {0, 0}, .extent = m_swapchainExtent },
        .clearValueCount = static_cast<uint32_t>(clearValues.size()),
        .pClearValues    = clearValues.data(),
    };
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

// Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    // Set viewport and scissor (dynamic state)
    VkViewport viewport {
        .x        = 0.0f,
        .y        = static_cast<float>(m_swapchainExtent.height),  // Flip Y for Vulkan
        .width    = static_cast<float>(m_swapchainExtent.width),
        .height   = -static_cast<float>(m_swapchainExtent.height), // Negative height
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor { .offset = {0, 0}, .extent = m_swapchainExtent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd,  0, 1, &scissor);

    m_pendingDrawCalls.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::submitDrawCall(const DrawCall& drawCall) {
    m_pendingDrawCalls.push_back(drawCall);
}

//
─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::endFrame() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // TODO: Actually bind vertex/index buffers and issue vkCmdDrawIndexed
    // per pending draw call. For brevity, the draw call processing is outlined
    // but not fully implemented — this is the integration point for VulkanBuffer.
    for (const auto& dc : m_pendingDrawCalls) {
        // Push constant: model matrix
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(dc.modelMatrix), dc.modelMatrix);
        // vkCmdBindVertexBuffers(...)
        // vkCmdBindIndexBuffer(...)
        // vkCmdDrawIndexed(...)
        (void)dc;
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
// ── Submit to graphics queue ───────────────────────────────────────────
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &m_imageAvailableSemaphores[m_currentFrame],
        .pWaitDstStageMask    = &waitStage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &m_renderFinishedSemaphores[m_currentFrame],
    };
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);

    // ── Present via SwappyVk (handles frame pacing) ────────────────────────
    VkPresentInfoKHR presentInfo {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &m_renderFinishedSemaphores[m_currentFrame],
        .swapchainCount     = 1,
        .pSwapchains        = &m_swapchain,
        .pImageIndices      = &m_currentImageIndex,
};
    // SwappyVk_queuePresent replaces vkQueuePresentKHR
    // It inserts frame pacing delays to hit the target refresh rate
    VkResult result = SwappyVk_queuePresent(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR
        || m_framebufferResized)
    {
        m_framebufferResized = false;
        recreateSwapchain();
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::destroySurface() {
    vkDeviceWaitIdle(m_device);
    m_surfaceReady = false;
    cleanupSwapchain();
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::recreateSwapchain() {
  vkDeviceWaitIdle(m_device);
    cleanupSwapchain();
    createSwapchain();
    createFramebuffers();
    LOG_INFO("Swapchain recreated: %ux%u",
             m_swapchainExtent.width, m_swapchainExtent.height);
}

void VulkanRenderer::onWindowResize() {
    m_framebufferResized = true;
}

void VulkanRenderer::releaseNonEssentialResources() {
    // Release texture mip-maps, mesh LOD caches, etc.
    // For now, just log — implementation depends on resource manager
    LOG_INFO("VulkanRenderer: releasing non-essential resources (low memory)");
}
void VulkanRenderer::cleanupSwapchain() {
    for (auto& fb : m_swapchainFramebuffers) {
        if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_swapchainFramebuffers.clear();

    for (auto& iv : m_swapchainImageViews) {
        if (iv) vkDestroyImageView(m_device, iv, nullptr);
    }
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(m_device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
}

    if (m_graphicsPipeline) vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    if (m_pipelineLayout)   vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_renderPass)       vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_commandPool)      vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_descriptorPool)   vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    cleanupSwapchain();

    SwappyVk_destroySwapchain(m_device, m_swapchain);
    SwappyVk_destroyDevice(m_device);

#ifdef ENABLE_VALIDATION_LAYERS
    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn) destroyFn(m_instance, m_debugMessenger, nullptr);
    }
#endif

vkDestroyDevice(m_device, nullptr);
    vkDestroyInstance(m_instance, nullptr);

    m_device    = VK_NULL_HANDLE;
    m_instance  = VK_NULL_HANDLE;
    LOG_INFO("VulkanRenderer shutdown complete");
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::createFramebuffers() {
    m_swapchainFramebuffers.resize(m_swapchainImages.size());

    for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
        const std::array<VkImageView, 2> attachments = {
            m_swapchainImageViews[i],
            m_depthImageView
        };
        VkFramebufferCreateInfo fbInfo {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = m_renderPass,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments    = attachments.data(),
            .width           = m_swapchainExtent.width,
            .height          = m_swapchainExtent.height,
            .layers          = 1,
        };
        VkResult result = vkCreateFramebuffer(
m_device, &fbInfo, nullptr, &m_swapchainFramebuffers[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("vkCreateFramebuffer[%zu] failed: %d", i, result);
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::createDescriptorPool() {
    // Allocate descriptor sets for:
    //   • Per-frame uniform buffers (camera, lights): 1 set * MAX_FRAMES
    //   • Per-material texture samplers: up to 128 materials
    const std::array<VkDescriptorPoolSize, 2> poolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         MAX_FRAMES_IN_FLIGHT * 4 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128 },
    }};

    VkDescriptorPoolCreateInfo poolInfo {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT * 4 + 128,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes    = poolSizes.data(),
    };
    VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateDescriptorPool failed: %d"
result);
        return false;
    }
    return true;
}

bool VulkanRenderer::createPipeline() {
    // Shader loading is handled by ShaderManager
    // Placeholder — full implementation in ShaderManager.cpp
    LOG_INFO("VulkanRenderer::createPipeline() — pipeline creation deferred to ShaderManager");
    return true;
}

} // namespace hs, 
