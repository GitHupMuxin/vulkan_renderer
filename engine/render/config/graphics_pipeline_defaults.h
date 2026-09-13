#pragma once

#include <array>

#include <vulkan/vulkan.h>

namespace engine::render::graphics_pipeline_defaults
{
    // 当前 Graphics Pipeline 的默认状态，尚未完成配置化。
    // 后续接入 Shader 反射与管线配置时，在此整理默认值及覆盖规则。
    // 创建时复制这些结构，再用已有 description 覆盖管线差异；采样数以附件为准。

    // 内部渲染图像的默认采样规则，沿用 ToneMapping 的线性过滤与边缘钳制。
    // VkSampler 由 RenderResourceRegistry 创建和持有；外部纹理沿用资源自带的 sampler。
    inline constexpr VkSamplerCreateInfo kSampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod = 0.0f,
        .maxLod = 0.0f
    };

    inline constexpr VkPipelineInputAssemblyStateCreateInfo kInputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    // cullMode 由 GraphicsPipelineDescription::cullMode_ 覆盖。
    inline constexpr VkPipelineRasterizationStateCreateInfo kRasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };

    // 沿用当前约定：depthTestEnable_ 同时决定深度测试与深度写入。
    inline constexpr VkPipelineDepthStencilStateCreateInfo kDepthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .stencilTestEnable = VK_FALSE
    };

    inline constexpr VkColorComponentFlags kColorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    inline constexpr VkPipelineColorBlendAttachmentState kOpaqueBlendAttachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = kColorWriteMask
    };

    // 保留现有 PBR 的颜色与 Alpha 通道混合规则。
    inline constexpr VkPipelineColorBlendAttachmentState kAlphaBlendAttachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = kColorWriteMask
    };

    // 当前三个 Pass 均只有一个颜色附件；按 blendMode_ 选择上面的混合状态。
    inline constexpr VkPipelineColorBlendStateCreateInfo kColorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &kOpaqueBlendAttachment
    };

    // viewport 和 scissor 的具体值由命令录制阶段设置。
    inline constexpr VkPipelineViewportStateCreateInfo kViewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    inline constexpr std::array kDynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    inline constexpr VkPipelineDynamicStateCreateInfo kDynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(kDynamicStates.size()),
        .pDynamicStates = kDynamicStates.data()
    };

    // 1x 是默认值；创建管线时必须与当前 Pass 的附件采样数保持一致。
    inline constexpr VkPipelineMultisampleStateCreateInfo kMultisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
}
