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
	void SkyBoxRenderPass::SetUpDescriptorSetLayout(const RenderScene& renderScene)
	{
		auto& device = core::Device::Instance();

		VkDescriptorSetLayout skyboxSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kSkyboxSet);

		// Skybox (fixed set)
		for (auto i = 0; i < this->skyboxSets_.size(); i++)
		{
			this->skyboxSets_[i] = core::DescriptorAllocator::Instance().AllocatePersistent(skyboxSetLayout);

			std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{};

			// binding=0：相机矩阵 UBO（Renderer 共享 matricesUBO，布局 UBOMatricesUpload）
			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = this->skyboxSets_[i];
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &this->renderResources_->GetBuffers(RenderResourceId::MainCamera)[i].descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = this->skyboxSets_[i];
			writeDescriptorSets[1].dstBinding = 2;
			writeDescriptorSets[1].pImageInfo = &resource::ResourceManager::Instance().GetEnvironmentCubeMap(renderScene.environment.environmentCubeMap)->prefilteredCube_.descriptor_;

			vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void SkyBoxRenderPass::SetUpPipeline(const std::string vertexShader, const std::string fragmentShader)
	{
		auto& device = core::Device::Instance();

		VkDescriptorSetLayout skyboxSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kSkyboxSet);

		VkPipelineCache pipelineCache = PipelineCache::Instance().GetHandle();

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
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
		colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.front = depthStencilStateCI.back;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineViewportStateCreateInfo viewportStateCI{};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
		multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

		// rasterizationSamples 必须始终是合法值：MSAA 开 → sampleCount_，关 → 1x
		multisampleStateCI.rasterizationSamples = device.GetSetting().multiSampling_ ? device.GetSetting().sampleCount_ : VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicStateCI{};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// Pipeline layout (created once, shared by all pipeline sets)
		if (this->pipelineLayout_ == VK_NULL_HANDLE) {
			const std::vector<VkDescriptorSetLayout> setLayouts = {
				skyboxSetLayout
			};
			VkPipelineLayoutCreateInfo pipelineLayoutCI{};
			pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
			pipelineLayoutCI.pSetLayouts = setLayouts.data();
			SUCCESS_OR_LOG(
				vkCreatePipelineLayout(device.GetLogicalDeviceHandle(), &pipelineLayoutCI, nullptr, &this->pipelineLayout_) == VK_SUCCESS,
				"Renderer: Failed to create pipeline layout."
			);
		}

		// Vertex bindings and attributes
		VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(resource::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(resource::Model::Vertex, pos)},
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(resource::Model::Vertex, normal) },
			{ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(resource::Model::Vertex, uv0) }
		};

		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

		// Pipelines
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = this->pipelineLayout_;
		pipelineCI.renderPass = this->renderPass_;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		shaderStages[0] = core::Loader::LoadShader(device.GetLogicalDeviceHandle(), vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = core::Loader::LoadShader(device.GetLogicalDeviceHandle(), fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

		VkPipeline pipeline{};

		SUCCESS_OR_LOG(
			vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), pipelineCache, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
			"Renderer: Failed to create graphics pipeline."
		);


		this->pipelines_.insert(std::make_pair("skybox", pipeline));

		for (auto shaderStage : shaderStages) {
			vkDestroyShaderModule(device.GetLogicalDeviceHandle(), shaderStage.module, nullptr);
		}
	}

    std::string_view SkyBoxRenderPass::GetName() const noexcept
    {
        return "SkyBoxRenderPass";
    }

    std::span<const PassInputResource> SkyBoxRenderPass::GetInputResources() const noexcept
    {
        static constexpr std::array inputs = {
            PassInputResource{
                RenderResourceId::MainCamera,
                PassResourceUsage{
                    ResourceUsage::UniformBuffer
                }
            },
            PassInputResource{
                RenderResourceId::PrefilteredMap,
                PassResourceUsage{
                    ResourceUsage::SampledImage,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        };

        return inputs;
    }

    std::span<const PassOutputResource> SkyBoxRenderPass::GetOutputResources() const noexcept
    {
        static constexpr std::array outputs = {
            PassOutputResource{
                RenderResourceId::SceneColorHdr,
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

    PassResourceUsage SkyBoxRenderPass::GetResourceUsage(RenderResourceId resourceId) const noexcept
    {
        switch (resourceId)
        {
            case RenderResourceId::MainCamera:
                return PassResourceUsage{
                    ResourceUsage::UniformBuffer
                };
            case RenderResourceId::PrefilteredMap:
                return PassResourceUsage{
                    ResourceUsage::SampledImage,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };
			case RenderResourceId::SceneColorHdr:
				return PassResourceUsage{
					ResourceUsage::ColorAttachment,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				};
            default:
                LOG_ERROR("SkyBoxRenderPass: unknown resource ID.");
                return PassResourceUsage{};
        }
    }

	SkyBoxRenderPass::SkyBoxRenderPass()
	{
		uint32_t frameCount = core::Device::Instance().GetSetting().frameCount_;
		this->skyboxSets_.resize(frameCount);
	}

	SkyBoxRenderPass::~SkyBoxRenderPass()
	{
		this->Cleanup();
	}

    void SkyBoxRenderPass::ExecutePreProcess(const RenderScene& renderScene)
	{
		LOG_INFO("SkyBoxRenderPass: start to execute pre-process...");

		std::string assetPath = resource::ResourceManager::assetPath_;
		this->SetUpDescriptorSetLayout(renderScene);
		this->SetUpPipeline(assetPath + "shaders/skybox.vert.spv", assetPath + "shaders/skybox.frag.spv");
	}

	void SkyBoxRenderPass::Execute(VkCommandBuffer currentCB, uint32_t frameIndex, uint32_t, const RenderScene&)
	{
		static auto beginLable = core::Device::Instance().GetCmdBeginDebugUtilsLabel();
		static auto endLable = core::Device::Instance().GetCmdEndDebugUtilsLabel();
		if (beginLable)
		{
			VkDebugUtilsLabelEXT labelInfo{};
			labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
			labelInfo.pLabelName = "SkyBoxRenderPass";
			labelInfo.color[0] = 1.0f;
			labelInfo.color[1] = 1.0f;
			labelInfo.color[2] = 0.0f;
			labelInfo.color[3] = 1.0f;
			beginLable(currentCB, &labelInfo);
		}

		static resource::Model* skyboxModel = resource::ResourceManager::Instance().skybox_.get();

		vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_, 0, 1, &this->skyboxSets_[frameIndex], 0, nullptr);
		vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelines_["skybox"]);
		skyboxModel->Draw(currentCB);

		if (endLable)
		{
			endLable(currentCB);
		}
	}

	void SkyBoxRenderPass::Cleanup()
	{
		auto& device = core::Device::Instance();

		if (this->pipelineLayout_ != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device.GetLogicalDeviceHandle(), this->pipelineLayout_, nullptr);
			this->pipelineLayout_ = VK_NULL_HANDLE;
		}

		for (auto& pipeline : this->pipelines_)
		{
			if (pipeline.second != VK_NULL_HANDLE) {
				vkDestroyPipeline(device.GetLogicalDeviceHandle(), pipeline.second, nullptr);
				pipeline.second = VK_NULL_HANDLE;
			}
		}
		this->pipelines_.clear();

		for (auto& skyboxSet : this->skyboxSets_)
			core::DescriptorAllocator::Instance().FreePersistent(skyboxSet);
	}
}
