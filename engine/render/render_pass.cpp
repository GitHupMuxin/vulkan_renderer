#include "engine/render/render_pass.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "engine/core/device.h"
#include "engine/core/descriptor_allocator.h"
#include "engine/core/descriptor_layout_registry.h"
#include "engine/core/loader.h"
#include "engine/core/swapchain.h"
#include "engine/render/config/graphics_pipeline_defaults.h"
#include "engine/render/pipeline_cache_file.h"
#include "engine/render/render_context.h"
#include "engine/resource/model.h"
#include "engine/resource/resource_manager.h"
#include "engine/utils/log.h"

namespace engine::render
{
    namespace
    {
        std::span<const VkVertexInputAttributeDescription> GetVertexInputAttributes(VertexLayout layout)
        {
            using Vertex = resource::Model::Vertex;
            static constexpr std::array<VkVertexInputAttributeDescription, 7> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv0)},
                {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv1)},
                {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joint0)},
                {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0)},
                {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)}
            }};

            switch (layout)
            {
                case VertexLayout::None:     return {};
                case VertexLayout::Skybox:   return {attributes.data(), 3};
                case VertexLayout::PbrModel: return attributes;
            }
            throw std::invalid_argument("Unknown vertex layout in graphics pipeline description");
        }

        VkFormat ResolveAttachmentFormat(const PassOutputResource& output, core::SwapChain& swapChain)
        {
            const auto* resource = FindRenderResourceDescription(output.resource_.id_);
            const auto& image = std::get<RenderImageDescription>(resource->description_);
            if (image.format_ != VK_FORMAT_UNDEFINED)
            {
                return image.format_;
            }

            return std::holds_alternative<PassColorAttachment>(output.attachment_)
                ? swapChain.GetColorFormat()
                : swapChain.GetDepthFormat();
        }

        VkAttachmentDescription BuildAttachmentDescription(const PassOutputResource& output, core::SwapChain& swapChain)
        {
            const auto* resource = FindRenderResourceDescription(output.resource_.id_);
            const auto& image = std::get<RenderImageDescription>(resource->description_);

            VkAttachmentDescription attachment{};
            attachment.format = ResolveAttachmentFormat(output, swapChain);
            attachment.samples = image.samples_;

            if (const auto* color = std::get_if<PassColorAttachment>(&output.attachment_))
            {
                attachment.loadOp = color->loadOp_;
                attachment.storeOp = color->storeOp_;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = color->initialLayout_;
                attachment.finalLayout = color->finalLayout_;
            }
            else
            {
                const auto& depth = std::get<PassDepthAttachment>(output.attachment_);
                attachment.loadOp = depth.loadOp_;
                attachment.storeOp = depth.storeOp_;
                attachment.stencilLoadOp = depth.stencilLoadOp_;
                attachment.stencilStoreOp = depth.stencilStoreOp_;
                attachment.initialLayout = depth.initialLayout_;
                attachment.finalLayout = depth.finalLayout_;
            }

            return attachment;
        }

        VkClearValue BuildClearValue(const PassOutputResource& output)
        {
            VkClearValue clear{};
            if (const auto* color = std::get_if<PassColorAttachment>(&output.attachment_))
            {
                clear.color = color->clearValue_;
            }
            else
            {
                clear.depthStencil = std::get<PassDepthAttachment>(output.attachment_).clearValue_;
            }
            return clear;
        }
    }

    RenderPass::RenderPass(const RenderPassDescription& description)
        : description_(description)
    {
    }

    void RenderPass::PrepareDescriptorResources(RenderResourceRegistry& resources)
    {
        // Pass 只准备自己持有的 Set 0；Model/Material 的 Set 由原所有者准备。
        for (const auto& binding : this->description_.descriptorSets_.at(0).bindings_)
            resources.RequireResource(binding.resource_);
    }

    void RenderPass::SetUpDescriptorSetLayouts()
    {
        auto& registry = core::DescriptorLayoutRegistry::Instance();
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.reserve(this->description_.descriptorSets_.size());

        for (const auto& set : this->description_.descriptorSets_)
        {
            if (set.set_ != layouts.size())
                throw std::invalid_argument("Descriptor sets must be declared in consecutive order starting at 0");

            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bindings.reserve(set.bindings_.size());
            for (const auto& binding : set.bindings_)
            {
                VkDescriptorSetLayoutBinding layoutBinding{};
                layoutBinding.binding = binding.binding_;
                layoutBinding.descriptorType = binding.descriptorType_;
                layoutBinding.descriptorCount = binding.descriptorCount_;
                layoutBinding.stageFlags = binding.shaderStages_;
                bindings.push_back(layoutBinding);
            }

            layouts.push_back(registry.GetOrCreate(bindings));
        }

        this->descriptorSetLayouts_ = std::move(layouts);
    }

    void RenderPass::AllocateDescriptorSets()
    {
        if (!this->descriptorSets_.empty())
            throw std::logic_error("Descriptor sets have already been allocated for this RenderPass");

        // SetUpDescriptorSetLayouts must run first. Under the current protocol,
        // set 0 belongs to the Pass; Model/Material keep their existing sets.
        const VkDescriptorSetLayout layout = this->descriptorSetLayouts_.at(0);
        const uint32_t frameCount = core::Device::Instance().GetSetting().frameCount_;
        auto& allocator = core::DescriptorAllocator::Instance();
        this->descriptorSets_.reserve(frameCount);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            this->descriptorSets_.push_back(allocator.AllocatePersistent(layout));
        }
    }

    void RenderPass::UpdateDescriptorSets(uint32_t frameIndex)
    {
        const auto& set = this->description_.descriptorSets_.at(0);
        const VkDescriptorSet descriptorSet = this->descriptorSets_.at(frameIndex);
        if (this->renderResources_ == nullptr)
            throw std::logic_error("Render resources must be provided before updating descriptor sets");

        // Pre-size the backing arrays to keep all descriptor info pointers stable.
        std::vector<VkWriteDescriptorSet> writes(set.bindings_.size());
        std::vector<VkDescriptorBufferInfo> bufferInfos(set.bindings_.size());
        std::vector<VkDescriptorImageInfo> imageInfos(set.bindings_.size());

        for (size_t i = 0; i < set.bindings_.size(); ++i)
        {
            const auto& binding = set.bindings_[i];
            if (binding.descriptorCount_ != 1)
                throw std::invalid_argument("Pass set 0 currently supports one descriptor per binding");
            const auto* resource = FindRenderResourceDescription(binding.resource_.id_);
            if (resource == nullptr)
                throw std::invalid_argument("Unknown resource in Pass descriptor declaration");
            auto& write = writes[i];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = binding.binding_;
            write.descriptorType = binding.descriptorType_;
            write.descriptorCount = binding.descriptorCount_;

            switch (binding.descriptorType_)
            {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                {
                    const auto& buffers = this->renderResources_->GetBuffers(binding.resource_.id_);
                    const uint32_t index = resource->lifetime_ == RenderResourceLifetime::Frame ? frameIndex : 0;
                    bufferInfos[i] = buffers.at(index).descriptor;
                    write.pBufferInfo = &bufferInfos[i];
                    break;
                }
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                {
                    const auto image = this->renderResources_->GetCurrentImage(binding.resource_);
                    const auto usage = this->GetResourceUsage(binding.resource_);
                    if (image.sampler_ == VK_NULL_HANDLE || usage.requiredLayout_ == VK_IMAGE_LAYOUT_UNDEFINED)
                        throw std::logic_error("Sampled image requires a sampler and a declared layout");
                    imageInfos[i] = {image.sampler_, image.imageView_, usage.requiredLayout_};
                    write.pImageInfo = &imageInfos[i];
                    break;
                }
                default:
                    throw std::invalid_argument("Unsupported descriptor type in Pass set 0");
            }
        }

        vkUpdateDescriptorSets(core::Device::Instance().GetLogicalDeviceHandle(),
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void RenderPass::SetUpPipelineLayout()
    {
        if (this->pipelineLayout_ != VK_NULL_HANDLE)
            return;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(this->descriptorSetLayouts_.size());
        layoutInfo.pSetLayouts = this->descriptorSetLayouts_.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(this->description_.pushConstants_.size());
        layoutInfo.pPushConstantRanges = this->description_.pushConstants_.data();

        const VkResult result = vkCreatePipelineLayout(
            core::Device::Instance().GetLogicalDeviceHandle(), &layoutInfo, nullptr, &this->pipelineLayout_
        );
        if (result != VK_SUCCESS)
            throw std::runtime_error("RenderPass: failed to create pipeline layout");
    }

    void RenderPass::SetUpPipelines()
    {
        if (!this->pipelines_.empty())
            return;
        if (this->pipelineLayout_ == VK_NULL_HANDLE || this->renderPass_ == VK_NULL_HANDLE)
            throw std::logic_error("Render target and pipeline layout must exist before creating pipelines");

        // 与 RenderPass 附件使用同一份资源声明，避免另从 Device 设置读取采样数。
        const auto& outputs = this->description_.outputs_;
        const auto* firstResource = FindRenderResourceDescription(outputs.at(0).resource_.id_);
        const auto samples = std::get<RenderImageDescription>(firstResource->description_).samples_;
        uint32_t colorAttachmentCount = 0;
        for (const auto& output : outputs)
        {
            const auto* resource = FindRenderResourceDescription(output.resource_.id_);
            if (std::get<RenderImageDescription>(resource->description_).samples_ != samples)
                throw std::invalid_argument("Current Pass attachments must have matching sample counts");
            if (std::holds_alternative<PassColorAttachment>(output.attachment_))
                ++colorAttachmentCount;
        }
        if (colorAttachmentCount != 1)
            throw std::invalid_argument("Current graphics pipeline defaults require one color attachment");

        auto& device = core::Device::Instance();
        const VkDevice logicalDevice = device.GetLogicalDeviceHandle();
        const std::string& assetPath = resource::ResourceManager::assetPath_;
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

        try
        {
            for (const auto& pipeline : this->description_.pipelines_)
            {
                auto [entry, inserted] = this->pipelines_.try_emplace(std::string(pipeline.key_), VK_NULL_HANDLE);
                if (!inserted)
                    throw std::invalid_argument("Graphics pipeline keys must be unique within a Pass");

                // 顶点输入：Skybox 使用公共顶点的前三个属性，PBR 使用全部属性。
                const auto attributes = GetVertexInputAttributes(pipeline.vertexLayout_);
                const VkVertexInputBindingDescription binding{
                    0, sizeof(resource::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX
                };
                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                if (!attributes.empty())
                {
                    vertexInput.vertexBindingDescriptionCount = 1;
                    vertexInput.pVertexBindingDescriptions = &binding;
                    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                    vertexInput.pVertexAttributeDescriptions = attributes.data();
                }

                // 固定状态来自默认文件，仅覆盖现有 description 声明的差异。
                const auto& inputAssembly = graphics_pipeline_defaults::kInputAssembly;
                auto rasterization = graphics_pipeline_defaults::kRasterization;
                rasterization.cullMode = pipeline.cullMode_;
                auto depthStencil = graphics_pipeline_defaults::kDepthStencil;
                depthStencil.depthTestEnable = pipeline.depthTestEnable_;
                depthStencil.depthWriteEnable = pipeline.depthTestEnable_;

                auto colorBlend = graphics_pipeline_defaults::kColorBlend;
                colorBlend.pAttachments = pipeline.blendMode_ == BlendMode::Alpha
                    ? &graphics_pipeline_defaults::kAlphaBlendAttachment
                    : &graphics_pipeline_defaults::kOpaqueBlendAttachment;
                const auto& viewport = graphics_pipeline_defaults::kViewport;
                const auto& dynamicState = graphics_pipeline_defaults::kDynamicState;
                auto multisample = graphics_pipeline_defaults::kMultisample;
                multisample.rasterizationSamples = samples;

                shaderStages[0] = core::Loader::LoadShader(
                    logicalDevice, assetPath + std::string(pipeline.vertexShader_), VK_SHADER_STAGE_VERTEX_BIT
                );
                shaderStages[1] = core::Loader::LoadShader(
                    logicalDevice, assetPath + std::string(pipeline.fragmentShader_), VK_SHADER_STAGE_FRAGMENT_BIT
                );
                if (shaderStages[0].module == VK_NULL_HANDLE || shaderStages[1].module == VK_NULL_HANDLE)
                    throw std::runtime_error("RenderPass: failed to load pipeline shaders");

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pRasterizationState = &rasterization;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlend;
                pipelineInfo.pViewportState = &viewport;
                pipelineInfo.pDynamicState = &dynamicState;
                pipelineInfo.pMultisampleState = &multisample;
                pipelineInfo.layout = this->pipelineLayout_;
                pipelineInfo.renderPass = this->renderPass_;
                pipelineInfo.subpass = 0;

                const VkResult result = vkCreateGraphicsPipelines(
                    logicalDevice, PipelineCache::Instance().GetHandle(), 1,
                    &pipelineInfo, nullptr, &entry->second
                );
                for (auto& stage : shaderStages)
                {
                    vkDestroyShaderModule(logicalDevice, stage.module, nullptr);
                    stage.module = VK_NULL_HANDLE;
                }
                if (result != VK_SUCCESS)
                    throw std::runtime_error("RenderPass: failed to create graphics pipeline");

                device.SetObjectName(
                    VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(entry->second), entry->first.c_str()
                );
            }
        }
        catch (...)
        {
            for (const auto& stage : shaderStages)
            {
                if (stage.module != VK_NULL_HANDLE)
                    vkDestroyShaderModule(logicalDevice, stage.module, nullptr);
            }
            this->DestroyPipelines();
            throw;
        }
    }

    void RenderPass::DestroyPipelines()
    {
        if (this->pipelines_.empty() && this->pipelineLayout_ == VK_NULL_HANDLE)
            return;

        const VkDevice logicalDevice = core::Device::Instance().GetLogicalDeviceHandle();
        for (const auto& [key, pipeline] : this->pipelines_)
        {
            if (pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(logicalDevice, pipeline, nullptr);
        }
        this->pipelines_.clear();
        if (this->pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(logicalDevice, this->pipelineLayout_, nullptr);
            this->pipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    RenderPass::~RenderPass()
    {
        for (VkDescriptorSet descriptorSet : this->descriptorSets_)
            core::DescriptorAllocator::Instance().FreePersistent(descriptorSet);
        this->descriptorSets_.clear();
        this->DestroyPipelines();
        this->DestroyRenderTarget();
    }


    std::string_view RenderPass::GetName() const noexcept
    {
        return this->description_.name_;
    }

    std::span<const PassInputResource> RenderPass::GetInputResources() const noexcept
    {
        return this->description_.inputs_;
    }

    std::span<const PassOutputResource> RenderPass::GetOutputResources() const noexcept
    {
        return this->description_.outputs_;
    }

    PassResourceUsage RenderPass::GetResourceUsage(RenderResourceId resourceId) const noexcept
    {
        return this->GetResourceUsage(RenderResourceReference{resourceId});
    }

    PassResourceUsage RenderPass::GetResourceUsage(RenderResourceReference reference) const noexcept
    {
        for (const auto& input : this->GetInputResources())
        {
            if (input.resource_.id_ == reference.id_ &&
                input.resource_.environmentTexture_ == reference.environmentTexture_)
            {
                return input.usage_;
            }
        }

        for (const auto& output : this->GetOutputResources())
        {
            if (output.resource_.id_ == reference.id_ &&
                output.resource_.environmentTexture_ == reference.environmentTexture_)
            {
                return output.usage_;
            }
        }

        LOG_ERROR(this->GetName() << ": resource reference is not declared in Input/Output.");
        return {};
    }

    void RenderPass::Prepare(const CompiledPass& compiledPass, RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        this->renderResources_ = &resources;
        this->PrepareDescriptorResources(resources);
        this->CreateRenderTarget(compiledPass, resources, swapChain);
        this->SetUpDescriptorSetLayouts();
        this->AllocateDescriptorSets();
        // 每份帧 Set 首次填写一次，后续 Buffer 内容更新不需要重写绑定。
        for (uint32_t frameIndex = 0; frameIndex < this->descriptorSets_.size(); ++frameIndex)
            this->UpdateDescriptorSets(frameIndex);
        this->SetUpPipelineLayout();
        this->SetUpPipelines();
    }


    void RenderPass::Execute(VkCommandBuffer currentCB, uint32_t frameIndex, const RenderScene& renderScene)
    {
        const auto beginLabel = core::Device::Instance().GetCmdBeginDebugUtilsLabel();
        const auto endLabel = core::Device::Instance().GetCmdEndDebugUtilsLabel();
        if (beginLabel && endLabel)
        {
            const std::string name(this->GetName());
            VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
            label.pLabelName = name.c_str();
            label.color[0] = 1.0f;
            label.color[1] = 1.0f;
            label.color[3] = 1.0f;
            beginLabel(currentCB, &label);
        }

        switch (this->description_.draw_.type_)
        {
            case PassDrawType::SceneGeometry:
                this->DrawSceneGeometry(currentCB, frameIndex, renderScene);
                break;
            case PassDrawType::SkyboxGeometry:
                this->DrawSkyboxGeometry(currentCB, frameIndex);
                break;
            case PassDrawType::FullscreenTriangle:
                this->DrawFullscreenTriangle(currentCB, frameIndex, renderScene);
                break;
            default:
                throw std::invalid_argument("Unknown draw type in RenderPass description");
        }

        if (beginLabel && endLabel)
        {
            endLabel(currentCB);
        }
    }

    VkPipeline RenderPass::SelectScenePipeline(PipelineVariant variant) const
    {
        const auto& descriptions = this->description_.pipelines_;
        if (variant == PipelineVariant::Unlit)
        {
            return this->pipelines_.at("unlit");
        }
        if (variant == PipelineVariant::Pbr)
        {
            return this->pipelines_.at(std::string(descriptions.at(0).key_));
        }

        const auto selected = std::find_if(descriptions.begin(), descriptions.end(), [variant](const auto& pipeline) {
            if (variant == PipelineVariant::AlphaBlending)
            {
                return pipeline.blendMode_ == BlendMode::Alpha;
            }
            return variant == PipelineVariant::DoubleSided &&
                pipeline.blendMode_ == BlendMode::Opaque && pipeline.cullMode_ == VK_CULL_MODE_NONE;
        });
        if (selected == descriptions.end())
        {
            throw std::invalid_argument("Scene draw has no matching pipeline variant");
        }
        return this->pipelines_.at(std::string(selected->key_));
    }

    void RenderPass::DrawSceneGeometry(VkCommandBuffer cb, uint32_t frameIndex, const RenderScene& renderScene)
    {
        this->DrawQueue(renderScene.opaqueItems, cb, frameIndex);
        this->DrawQueue(renderScene.maskedItems, cb, frameIndex);
        this->DrawQueue(renderScene.transparentItems, cb, frameIndex);
    }

    void RenderPass::DrawQueue(std::span<const RenderItem> items, VkCommandBuffer cb, uint32_t frameIndex)
    {
        auto& manager = resource::ResourceManager::Instance();
        resource::Model* currentModel = nullptr;
        VkPipeline currentPipeline = VK_NULL_HANDLE;
        const VkDeviceSize offset = 0;

        for (const RenderItem& item : items)
        {
            resource::Model* model = manager.GetModel(item.modelHandle);
            if (model == nullptr)
            {
                continue;
            }

            if (model != currentModel)
            {
                const VkBuffer vertexBuffer = model->GetVertexBuffer();
                vkCmdBindVertexBuffers(cb, 0, 1, &vertexBuffer, &offset);
                if (model->GetIndexBuffer() != VK_NULL_HANDLE)
                {
                    vkCmdBindIndexBuffer(cb, model->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                }
                currentModel = model;
            }

            const VkPipeline pipeline = this->SelectScenePipeline(item.pipeline);
            if (pipeline != currentPipeline)
            {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                currentPipeline = pipeline;
            }

            const std::array<VkDescriptorSet, 4> sets{
                this->descriptorSets_.at(frameIndex),
                item.materialDescriptorSet,
                model->GetDescriptorSetsMeshData().at(frameIndex),
                model->GetDescriptorSetMaterial()
            };
            vkCmdBindDescriptorSets(
                cb, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_,
                0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr
            );

            // 沿用模型绘制协议：两个 int32 分别索引 MeshData 和 Material SSBO。
            struct MeshPushConstant
            {
                int32_t meshIndex;
                int32_t materialIndex;
            };
            const MeshPushConstant indices{
                static_cast<int32_t>(item.meshIndex), static_cast<int32_t>(item.materialIndex)
            };
            vkCmdPushConstants(
                cb, this->pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(indices), &indices
            );

            if (item.hasIndices)
            {
                vkCmdDrawIndexed(cb, item.indexCount, 1, item.firstIndex, 0, 0);
            }
            else
            {
                vkCmdDraw(cb, item.vertexCount, 1, 0, 0);
            }
        }
    }

    void RenderPass::DrawSkyboxGeometry(VkCommandBuffer cb, uint32_t frameIndex)
    {
        const VkPipeline pipeline = this->pipelines_.at(std::string(this->description_.pipelines_.at(0).key_));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_,
            0, 1, &this->descriptorSets_.at(frameIndex), 0, nullptr
        );
        resource::ResourceManager::Instance().skybox_->Draw(cb);
    }

    void RenderPass::DrawFullscreenTriangle(VkCommandBuffer cb, uint32_t frameIndex, const RenderScene& renderScene)
    {
        const VkPipeline pipeline = this->pipelines_.at(std::string(this->description_.pipelines_.at(0).key_));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_,
            0, 1, &this->descriptorSets_.at(frameIndex), 0, nullptr
        );

        // 当前全屏绘制的 push constant 协议只有 exposure；无声明时无需填写。
        if (!this->description_.pushConstants_.empty())
        {
            const float exposure = renderScene.environment.exposure;
            vkCmdPushConstants(cb, this->pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(exposure), &exposure);
        }
        vkCmdDraw(cb, 3, 1, 0, 0);
    }

    void RenderPass::CreateRenderTarget(const CompiledPass& compiledPass, const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        const auto outputs = this->GetOutputResources();
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> colorReferences;
        attachments.reserve(outputs.size());
        colorReferences.reserve(outputs.size());
        this->clearValues_.clear();
        this->clearValues_.reserve(outputs.size());

        VkAttachmentReference depthReference{};
        bool hasDepthAttachment = false;
        for (uint32_t index = 0; index < outputs.size(); ++index)
        {
            const auto& output = outputs[index];
            attachments.push_back(BuildAttachmentDescription(output, swapChain));
            this->clearValues_.push_back(BuildClearValue(output));

            if (std::holds_alternative<PassColorAttachment>(output.attachment_))
            {
                colorReferences.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            }
            else
            {
                depthReference = {index, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                hasDepthAttachment = true;
            }
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
        subpass.pColorAttachments = colorReferences.data();
        subpass.pDepthStencilAttachment = hasDepthAttachment ? &depthReference : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        for (const auto& compiledDependency : compiledPass.attachmentDependencies_)
        {
            const auto output = std::find_if(outputs.begin(), outputs.end(), [&compiledDependency](const auto& candidate) {
                return candidate.resource_.id_ == compiledDependency.resource_.id_ &&
                    candidate.resource_.environmentTexture_ == compiledDependency.resource_.environmentTexture_;
            });
            if (output == outputs.end()) continue;

            PassResourceUsage srcUsage =  compiledDependency.srcUsage_;
            PassResourceUsage dstUsage =  compiledDependency.dstUsage_;

            // src 侧：上一个 Pass 怎么写这个资源（按生产者的实际用法）
            switch (srcUsage.type_)
            {
                case ResourceUsage::ColorAttachment:
                    dependency.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    dependency.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    break;
                case ResourceUsage::DepthAttachment:
                    dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    dependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;
                default:
                    LOG_ERROR(this->GetName() << ": attachment dependency has non-attachment source usage.");
                    break;
            }

            // dst 侧：本 Pass 怎么用这个资源（按消费者的实际用法）
            switch (dstUsage.type_)
            {
                case ResourceUsage::ColorAttachment:
                    dependency.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    break;
                case ResourceUsage::DepthAttachment:
                    dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;
                default:
                    LOG_ERROR(this->GetName() << ": attachment dependency has non-attachment destination usage.");
                    break;
            }
        }

        const bool hasAttachmentDependency = dependency.srcStageMask != 0;
        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassCI.pAttachments = attachments.data();
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpass;
        renderPassCI.dependencyCount = hasAttachmentDependency ? 1u : 0u;
        renderPassCI.pDependencies = hasAttachmentDependency ? &dependency : nullptr;

        SUCCESS_OR_LOG(
            vkCreateRenderPass(core::Device::Instance().GetLogicalDeviceHandle(), &renderPassCI, nullptr, &this->renderPass_) == VK_SUCCESS,
            this->GetName() << ": failed to create render pass."
        );

        this->CreateFramebuffers(resources, swapChain);
    }

    void RenderPass::CreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        const auto outputs = this->GetOutputResources();
        const VkExtent2D extent = swapChain.GetExtent();
        this->framebuffers_.resize(swapChain.GetImageCount(), VK_NULL_HANDLE);

        for (uint32_t imageIndex = 0; imageIndex < this->framebuffers_.size(); ++imageIndex)
        {
            std::vector<VkImageView> views;
            views.reserve(outputs.size());
            for (const auto& output : outputs)
            {
                if (output.resource_.id_ != RenderResourceId::BackBuffer)
                {
                    const uint32_t index = resources.GetImageCount(output.resource_.id_) == 1 ? 0 : imageIndex;
                    views.push_back(resources.GetImage(output.resource_, index).imageView_);
                }
                else
                {
                    views.push_back(swapChain.GetSwapChainBuffer(imageIndex).view);
                }
            }

            VkFramebufferCreateInfo framebufferCI{};
            framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCI.renderPass = this->renderPass_;
            framebufferCI.attachmentCount = static_cast<uint32_t>(views.size());
            framebufferCI.pAttachments = views.data();
            framebufferCI.width = extent.width;
            framebufferCI.height = extent.height;
            framebufferCI.layers = 1;

            SUCCESS_OR_LOG(
                vkCreateFramebuffer(core::Device::Instance().GetLogicalDeviceHandle(), &framebufferCI, nullptr, &this->framebuffers_[imageIndex]) == VK_SUCCESS,
                this->GetName() << ": failed to create framebuffer."
            );
        }
    }

    void RenderPass::RecreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        this->DestroyFramebuffers();
        this->CreateFramebuffers(resources, swapChain);
    }

    void RenderPass::DestroyFramebuffers()
    {
        const VkDevice device = core::Device::Instance().GetLogicalDeviceHandle();
        for (VkFramebuffer framebuffer : this->framebuffers_)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        this->framebuffers_.clear();
    }

    void RenderPass::DestroyRenderTarget()
    {
        this->DestroyFramebuffers();
        if (this->renderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(core::Device::Instance().GetLogicalDeviceHandle(), this->renderPass_, nullptr);
            this->renderPass_ = VK_NULL_HANDLE;
        }
    }

    VkRenderPass RenderPass::GetHandle() const noexcept
    {
        return this->renderPass_;
    }

    VkFramebuffer RenderPass::GetFramebuffer(uint32_t imageIndex) const
    {
        return this->framebuffers_.at(imageIndex);
    }

    std::span<const VkClearValue> RenderPass::GetClearValues() const noexcept
    {
        return this->clearValues_;
    }
}
