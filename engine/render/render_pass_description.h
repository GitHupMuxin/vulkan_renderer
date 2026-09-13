#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/config/pass_resource.h"

namespace engine::render
{
    enum class PassDrawType
    {
        SkyboxGeometry,
        SceneGeometry,
        FullscreenTriangle
    };

    enum class VertexLayout
    {
        None,
        Skybox,
        PbrModel
    };

    struct DescriptorBindingDescription
    {
        RenderResourceReference resource_;
        uint32_t binding_ = 0;
        VkDescriptorType descriptorType_ = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        VkShaderStageFlags shaderStages_ = 0;
        uint32_t descriptorCount_ = 1;
    };

    struct DescriptorSetDescription
    {
        uint32_t set_ = 0;
        std::vector<DescriptorBindingDescription> bindings_;
    };

    enum class BlendMode
    {
        Opaque,
        Alpha
    };

    struct GraphicsPipelineDescription
    {
        // 同一 Pass 可以有多个材质变体；key 由绘制阶段根据 RenderItem 选择。
        std::string_view key_;
        std::string_view vertexShader_;
        std::string_view fragmentShader_;
        VertexLayout vertexLayout_ = VertexLayout::None;
        VkCullModeFlags cullMode_ = VK_CULL_MODE_BACK_BIT;
        bool depthTestEnable_ = false;
        BlendMode blendMode_ = BlendMode::Opaque;
    };

    struct PassDrawDescription
    {
        PassDrawType type_ = PassDrawType::FullscreenTriangle;
    };

    struct RenderPassDescription
    {
        std::string_view name_;

        std::vector<PassInputResource> inputs_;
        std::vector<PassOutputResource> outputs_;

        // Descriptor set layout 和 Pipeline layout 的完整声明。
        std::vector<DescriptorSetDescription> descriptorSets_;
        std::vector<VkPushConstantRange> pushConstants_;

        std::vector<GraphicsPipelineDescription> pipelines_;
        PassDrawDescription draw_;
    };

}
