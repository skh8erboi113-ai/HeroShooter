// app/src/main/cpp/rendering/VulkanPipeline.cpp
#include "VulkanPipeline.h"
#include "../utils/Logger.h"

#include <utility>  // std::swap

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
VulkanPipeline::VulkanPipeline(VulkanPipeline&& other) noexcept {
    m_pipeline = other.m_pipeline;
    m_layout   = other.m_layout;
    other.m_pipeline = VK_NULL_HANDLE;
    other.m_layout   = VK_NULL_HANDLE;
}

VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& other) noexcept {
    if (this != &other) {
        std::swap(m_pipeline, other.m_pipeline);
        std::swap(m_layout,   other.m_layout);
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
VkShaderModule VulkanPipeline::createShaderModule(
    VkDevice        device,
    const uint32_t* spirv,
    uint32_t        sizeBytes) const noexcept
{
    VkShaderModuleCreateInfo createInfo {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeBytes,
        .pCode    = spirv,
    };
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateShaderModule failed: %d", result);
        return VK_NULL_HANDLE;
    }
    return module;
}

// ─────────────────────────────────────────────────────────────────────────────
bool VulkanPipeline::build(
    VkDevice                    device,
    VkDescriptorSetLayout       frameSetLayout,
    VkDescriptorSetLayout       materialSetLayout,
    const PipelineCreateDesc&   desc)
{
    // ── Shader stages ──────────────────────────────────────────────────────
    VkShaderModule vertModule = createShaderModule(
        device, desc.vertSpirv, desc.vertSpirvSize);
    VkShaderModule fragModule = createShaderModule(
        device, desc.fragSpirv, desc.fragSpirvSize);

    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages {{
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName  = "main",
        },
    }};

    // ── Vertex input ───────────────────────────────────────────────────────
    const auto bindingDesc   = Vertex::getBindingDescription();
    const auto attributeDesc = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDesc,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDesc.size()),
        .pVertexAttributeDescriptions    = attributeDesc.data(),
    };

    // ── Input assembly ─────────────────────────────────────────────────────
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // ── Viewport & scissor (dynamic — set per frame) ───────────────────────
    constexpr std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data(),
    };

    // Viewport and scissor are set dynamically — placeholder counts needed
    VkPipelineViewportStateCreateInfo viewportState {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    // ── Rasterizer ─────────────────────────────────────────────────────────
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = (desc.type == PipelineType::Wireframe)
                                    ? VK_POLYGON_MODE_LINE
                                    : VK_POLYGON_MODE_FILL,
        .cullMode                = desc.cullMode,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = (desc.type == PipelineType::ShadowDepth)
                                    ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = 1.25f,   // Shadow map bias to prevent acne
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 1.75f,
        .lineWidth               = 1.0f,
    };

    // ── Multisampling (no MSAA for mobile — too expensive) ─────────────────
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
    };

    // ── Depth / stencil ────────────────────────────────────────────────────
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = desc.enableDepthTest  ? VK_TRUE : VK_FALSE,
        .depthWriteEnable      = desc.enableDepthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    // ── Colour blending ────────────────────────────────────────────────────
    VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable    = desc.enableBlending ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments    = &blendAttachment,
    };

    // ── Pipeline layout ────────────────────────────────────────────────────
    // Set 0 = per-frame data (camera, lights)
    // Set 1 = per-material data (textures, params)
    const std::array<VkDescriptorSetLayout, 2> setLayouts = {
        frameSetLayout,
        materialSetLayout,
    };

    VkPushConstantRange pushConstantRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof(PushConstants),
    };

    VkPipelineLayoutCreateInfo layoutInfo {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = static_cast<uint32_t>(setLayouts.size()),
        .pSetLayouts            = setLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstantRange,
    };

    VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout);
    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreatePipelineLayout failed: %d", result);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // ── Graphics pipeline ──────────────────────────────────────────────────
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        // Derivative pipelines: allow driver to reuse shader compilation
        // between variants (opaque→alphaMask share the same vertex shader)
        .flags               = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
        .stageCount          = static_cast<uint32_t>(shaderStages.size()),
        .pStages             = shaderStages.data(),
        .pVertexInputState   = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState      = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pDepthStencilState  = &depthStencil,
        .pColorBlendState    = &colorBlending,
        .pDynamicState       = &dynamicState,
        .layout              = m_layout,
        .renderPass          = desc.renderPass,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1,
    };

    // Use VK_NULL_HANDLE for the pipeline cache — in production, persist
    // a VkPipelineCache to disk to eliminate first-run shader compilation stalls
    result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

    // Shader modules no longer needed after pipeline compilation
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        LOG_ERROR("vkCreateGraphicsPipelines failed: %d", result);
        return false;
    }

    LOG_INFO("VulkanPipeline: built type=%d", static_cast<int>(desc.type));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanPipeline::bind(VkCommandBuffer cmd) const noexcept {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void VulkanPipeline::pushConstants(
    VkCommandBuffer         cmd,
    const PushConstants&    pc) const noexcept
{
    vkCmdPushConstants(
        cmd,
        m_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants),
        &pc
    );
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanPipeline::destroy(VkDevice device) noexcept {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

VulkanPipeline::~VulkanPipeline() {
    // Device handle is not stored in the pipeline — caller must call destroy()
    // before the pipeline goes out of scope. This is by design to enforce
    // explicit lifetime management.
    if (m_pipeline != VK_NULL_HANDLE) {
        LOG_WARN("VulkanPipeline destroyed without explicit destroy() call — "
                 "this is a resource leak if the VkDevice is still alive");
    }
}

} // namespace hs
