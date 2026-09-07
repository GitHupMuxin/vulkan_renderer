#include "engine/render/render_pass.h"

#include <array>

#include "engine/core/descriptor_allocator.h"
#include "engine/core/descriptor_layout_registry.h"
#include "engine/core/device.h"
#include "engine/core/loader.h"
#include "engine/core/schema.h"
#include "engine/core/swapchain.h"
#include "engine/render/frame_graph.h"
#include "engine/render/pipeline_cache_file.h"
#include "engine/resource/resource_manager.h"
#include "engine/utils/log.h"

namespace engine::render
{
    void ToneMappingRenderPass::RecreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        this->renderResources_ = &resources;
        RenderPass::RecreateFramebuffers(resources, swapChain);
        this->SetUpDescriptorSets();
    }

    ToneMappingRenderPass::ToneMappingRenderPass() = default;

    ToneMappingRenderPass::~ToneMappingRenderPass()
    {
        this->Cleanup();
    }

    void ToneMappingRenderPass::CreateSampler()
    {
        if (this->sampler_ != VK_NULL_HANDLE)
        {
            return;
        }

        VkSamplerCreateInfo samplerCI{};
        samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCI.magFilter = VK_FILTER_LINEAR;
        samplerCI.minFilter = VK_FILTER_LINEAR;
        samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.minLod = 0.0f;
        samplerCI.maxLod = 0.0f;

        auto& device = core::Device::Instance();
        SUCCESS_OR_LOG(
            vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerCI, nullptr, &this->sampler_) == VK_SUCCESS,
            "ToneMappingRenderPass: failed to create sampler."
        );
        device.SetObjectName(
            VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(this->sampler_), "ToneMapping Sampler"
        );
    }

    void ToneMappingRenderPass::SetUpDescriptorSets()
    {
        for (VkDescriptorSet descriptorSet : this->descriptorSets_)
        {
            core::DescriptorAllocator::Instance().FreePersistent(descriptorSet);
        }
        this->descriptorSets_.clear();

        const auto& images = this->renderResources_->GetImages(RenderResourceId::SceneColorHdr);
        const VkDescriptorSetLayout descriptorSetLayout =
            core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kToneMappingSet);
        this->descriptorSets_.resize(images.size(), VK_NULL_HANDLE);

        for (uint32_t i = 0; i < images.size(); ++i)
        {
            this->descriptorSets_[i] =
                core::DescriptorAllocator::Instance().AllocatePersistent(descriptorSetLayout);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = this->sampler_;
            imageInfo.imageView = images[i].imageView_;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = this->descriptorSets_[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(
                core::Device::Instance().GetLogicalDeviceHandle(), 1, &write, 0, nullptr
            );
        }
    }

    void ToneMappingRenderPass::SetUpPipeline(const std::string& vertexShader, const std::string& fragmentShader)
    {
        if (this->pipeline_ != VK_NULL_HANDLE)
        {
            return;
        }

        auto& device = core::Device::Instance();
        const VkDescriptorSetLayout descriptorSetLayout =
            core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kToneMappingSet);

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.size = sizeof(ToneMappingPushConstant);

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

        SUCCESS_OR_LOG(
            vkCreatePipelineLayout(
                device.GetLogicalDeviceHandle(), &pipelineLayoutCI, nullptr, &this->pipelineLayout_
            ) == VK_SUCCESS,
            "ToneMappingRenderPass: failed to create pipeline layout."
        );

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
        colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendStateCI.attachmentCount = 1;
        colorBlendStateCI.pAttachments = &blendAttachmentState;

        VkPipelineViewportStateCreateInfo viewportStateCI{};
        viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCI.viewportCount = 1;
        viewportStateCI.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
        multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
        depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        const std::array dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateCI.pDynamicStates = dynamicStates.data();

        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        const std::array shaderStages = {
            core::Loader::LoadShader(device.GetLogicalDeviceHandle(), vertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            core::Loader::LoadShader(device.GetLogicalDeviceHandle(), fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();
        pipelineCI.pVertexInputState = &vertexInputStateCI;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.layout = this->pipelineLayout_;
        pipelineCI.renderPass = this->renderPass_;

        SUCCESS_OR_LOG(
            vkCreateGraphicsPipelines(
                device.GetLogicalDeviceHandle(), PipelineCache::Instance().GetHandle(), 1,
                &pipelineCI, nullptr, &this->pipeline_
            ) == VK_SUCCESS,
            "ToneMappingRenderPass: failed to create graphics pipeline."
        );
        device.SetObjectName(
            VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(this->pipeline_), "ToneMapping Pipeline"
        );

        for (const auto& shaderStage : shaderStages)
        {
            vkDestroyShaderModule(device.GetLogicalDeviceHandle(), shaderStage.module, nullptr);
        }
    }

    std::string_view ToneMappingRenderPass::GetName() const noexcept
    {
        return "ToneMappingRenderPass";
    }

    std::span<const PassInputResource> ToneMappingRenderPass::GetInputResources() const noexcept
    {
        static constexpr std::array inputs = {
            PassInputResource{
                RenderResourceId::SceneColorHdr,
                PassResourceUsage{
                    ResourceUsage::SampledImage,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        };

        return inputs;
    }

    std::span<const PassOutputResource> ToneMappingRenderPass::GetOutputResources() const noexcept
    {
        static constexpr std::array outputs = {
            PassOutputResource{
                RenderResourceId::BackBuffer,
                PassColorAttachment{
                    .loadOp_ = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp_ = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .clearValue_ = {{0.0f, 0.0f, 0.0f, 1.0f}}
                }
            }
        };

        return outputs;
    }

    PassResourceUsage ToneMappingRenderPass::GetResourceUsage(RenderResourceId resourceId) const noexcept
    {
        switch (resourceId)
        {
            case RenderResourceId::SceneColorHdr:
                return PassResourceUsage{
                    ResourceUsage::SampledImage,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };
            default:
                LOG_ERROR("ToneMappingRenderPass: unknown resource ID.");
                return PassResourceUsage{};
        }
    }

    void ToneMappingRenderPass::ExecutePreProcess(const RenderScene&)
    {
        LOG_INFO("ToneMappingRenderPass: start to execute pre-process...");

        this->CreateSampler();
        this->SetUpDescriptorSets();

        const std::string assetPath = resource::ResourceManager::assetPath_;
        this->SetUpPipeline(
            assetPath + "shaders/tone_mapping.vert.spv",
            assetPath + "shaders/tone_mapping.frag.spv"
        );
    }

    void ToneMappingRenderPass::Execute(VkCommandBuffer currentCB, uint32_t, uint32_t imageIndex, const RenderScene& renderScene)
    {

        vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline_);
        vkCmdBindDescriptorSets(
            currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_,
            0, 1, &this->descriptorSets_[imageIndex], 0, nullptr
        );

        const ToneMappingPushConstant pushConstant{renderScene.environment.exposure};
        vkCmdPushConstants(
            currentCB, this->pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(pushConstant), &pushConstant
        );
        vkCmdDraw(currentCB, 3, 1, 0, 0);
    }

    void ToneMappingRenderPass::Cleanup()
    {
        auto& device = core::Device::Instance();

        for (VkDescriptorSet descriptorSet : this->descriptorSets_)
        {
            core::DescriptorAllocator::Instance().FreePersistent(descriptorSet);
        }
        this->descriptorSets_.clear();

        if (this->pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device.GetLogicalDeviceHandle(), this->pipeline_, nullptr);
            this->pipeline_ = VK_NULL_HANDLE;
        }
        if (this->pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device.GetLogicalDeviceHandle(), this->pipelineLayout_, nullptr);
            this->pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (this->sampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device.GetLogicalDeviceHandle(), this->sampler_, nullptr);
            this->sampler_ = VK_NULL_HANDLE;
        }
    }
}
