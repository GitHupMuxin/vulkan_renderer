#include <array>
#include "engine/core/loader.h"
#include "engine/render/render_pass.h"
#include "engine/resource/resource_manager.h"
#include "engine/utils/log.h"

namespace engine::render
{

    DescriptorSetCount DescriptorSetCount::operator+(DescriptorSetCount other)
	{
		return DescriptorSetCount{
			.uniformBufferCount = this->uniformBufferCount + other.uniformBufferCount,
			.imageSamplerCount = this->imageSamplerCount + other.imageSamplerCount,
			.storageBufferCount = this->storageBufferCount + other.storageBufferCount,
			.maxSets = this->maxSets + other.maxSets

		};
	}

    

    RenderPass::RenderPass()
    {
        // Constructor implementation
    }

    RenderPass::~RenderPass()
    {
        // Destructor implementation
    }

    void SkyBoxRenderPass::PreProcess()
	{
		std::string assetPath = resource::ResourceManager::assetPath_;

		this->SetUpDescriptorSetLayout();

		this->SetUpPipeline(assetPath + "shaders/skybox.vert.spv", assetPath + "shaders/skybox.frag.spv");
	}
	
	void SkyBoxRenderPass::SetUpDescriptorSetLayout()
	{
		auto& device = core::Device::Instance();
		VkDescriptorPool& descriptorPool = *(this->initInfo_.descriptorPool_);

    	std::vector<VkDescriptorSetLayoutBinding> bindings = 
		{
  	    	{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
   	    	{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        	{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    	};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    	descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    	descriptorSetLayoutCI.pBindings = bindings.data();
    	descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
    	SUCCESS_OR_LOG(
			vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &this->descriptorSetLayouts_.skybox) == VK_SUCCESS,
			"SkyBoxRenderPass: Failed to create descriptor set layout."
		);

		// Skybox (fixed set)
		for (auto i = 0; i < this->descriptorSets_.size(); i++) 
		{
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &this->descriptorSetLayouts_.skybox;
			descriptorSetAllocInfo.descriptorSetCount = 1;

			SUCCESS_OR_LOG(
				vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &this->descriptorSets_[i].skybox) == VK_SUCCESS,
				"SkyBoxRenderPass: Failed to allocate descriptor sets."
			);

			std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = this->descriptorSets_[i].skybox;
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &this->matricesUBOBuffer_[i].descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = this->descriptorSets_[i].skybox;
			writeDescriptorSets[1].dstBinding = 1;
			writeDescriptorSets[1].pBufferInfo = &this->initInfo_.scene_->ParamsUBOBuffers_[i].descriptor;

			writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[2].descriptorCount = 1;
			writeDescriptorSets[2].dstSet = this->descriptorSets_[i].skybox;
			writeDescriptorSets[2].dstBinding = 2;
			writeDescriptorSets[2].pImageInfo = &this->initInfo_.scene_->cubeMap_->prefilteredCube_.descriptor_;

			vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void SkyBoxRenderPass::SetUpPipeline(const std::string vertexShader, const std::string fragmentShader)
	{
		auto& device = core::Device::Instance();
		VkPipelineCache& pipelineCache = *(this->initInfo_.pipelineCache_);
		VkRenderPass& mainRenderPass = *(this->initInfo_.mainRenderPass_);

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
				this->descriptorSetLayouts_.skybox
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
		pipelineCI.renderPass = mainRenderPass;
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

	SkyBoxRenderPass::SkyBoxRenderPass()
	{
		uint32_t frameCount = core::Device::Instance().GetSetting().frameCount_;
		this->matricesUBOBuffer_.resize(frameCount);
		this->descriptorSets_.resize(frameCount);

		for (int i = 0; i < this->matricesUBOBuffer_.size(); i++)
			this->matricesUBOBuffer_[i].Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(this->matrices_));
	}

	SkyBoxRenderPass::~SkyBoxRenderPass()
	{
		this->Cleanup();
	}

    void SkyBoxRenderPass::UpdateUniformData(uint32_t frameIndex) 
	{
		auto& device = core::Device::Instance();
		this->matrices_.projection = this->initInfo_.scene_->GetCamera()->matrices_.perspective_;
		this->matrices_.view = this->initInfo_.scene_->GetCamera()->matrices_.view_;
		this->matrices_.model = glm::mat4(glm::mat3(this->initInfo_.scene_->GetCamera()->matrices_.view_));

		memcpy(this->matricesUBOBuffer_[frameIndex].mapped, &this->matrices_, sizeof(this->matrices_));
	}

	DescriptorSetCount SkyBoxRenderPass::GetDescriptorSetCount()
	{
		auto& device = core::Device::Instance();
		uint32_t frameCount = device.GetSetting().frameCount_;

		DescriptorSetCount resource
		{
			.uniformBufferCount = 2 * frameCount,
			.imageSamplerCount = 1 * frameCount,
			.storageBufferCount = 0,
			.maxSets = frameCount
		};

		return resource;
	}

	void SkyBoxRenderPass::Init(const RenderPassInitInfo& initInfo) 
	{
		this->initInfo_ = initInfo;
	}

    void SkyBoxRenderPass::ExecutePreProcess() 
	{
		LOG_INFO("SkyBoxRenderPass: start to execute pre-process...");
		this->PreProcess();
	}

	void SkyBoxRenderPass::Execute(VkCommandBuffer currentCB, uint32_t frameIndex) 
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

		VkDeviceSize offsets[1] = { 0 };

		vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_, 0, 1, &this->descriptorSets_[frameIndex].skybox, 0, nullptr);
		vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelines_["skybox"]);
		this->initInfo_.scene_->skybox_->Draw(currentCB);

		if (endLable)
		{
			endLable(currentCB);
		}
	}

	void SkyBoxRenderPass::Cleanup() 
	{

		auto& device = core::Device::Instance();

		if (this->descriptorSetLayouts_.skybox != VK_NULL_HANDLE) 
		{
        	vkDestroyDescriptorSetLayout(device.GetLogicalDeviceHandle(), this->descriptorSetLayouts_.skybox, nullptr);
            this->descriptorSetLayouts_.skybox = VK_NULL_HANDLE;
		}

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

	}

	resource::Texture2D PBRRenderPass::PreComputeTexture(const FullScreenPassConfig& config)
    {
        FullScreenPass pass;
        return std::move(pass.Execute(*(this->initInfo_.pipelineCache_), config));
    }

    void PBRRenderPass::PreProcess() 
	{
		std::string assetPath = resource::ResourceManager::assetPath_;
		this->SetUpDescriptorSetLayout();
		this->SetUpPipeline(assetPath + "shaders/pbr.vert.spv", assetPath + "shaders/material_pbr.frag.spv");
	}


    void PBRRenderPass::SetUpDescriptorSetLayout()
	{
		auto& device = core::Device::Instance();
		uint32_t frameCount = device.GetSetting().frameCount_;
		VkDescriptorPool& descriptorPool = *(this->initInfo_.descriptorPool_);
		resource::Texture2D* emptyTexture = resource::ResourceManager::Instance().emptyTexture2D_.get();
		// Scene (matrices and environment maps)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = 
            {
				{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			SUCCESS_OR_LOG(
                vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &this->descriptorSetLayouts_.scene) == VK_SUCCESS,
                "Renderer: Failed to create descriptor set layout."
            );

			for (auto i = 0; i < descriptorSets_.size(); i++) 
			{

				VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
				descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				descriptorSetAllocInfo.descriptorPool = descriptorPool;
				descriptorSetAllocInfo.pSetLayouts = &(this->descriptorSetLayouts_.scene);
				descriptorSetAllocInfo.descriptorSetCount = 1;

				SUCCESS_OR_LOG(
                    vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &this->descriptorSets_[i].scene) == VK_SUCCESS,
                    "Renderer: Failed to allocate descriptor sets."
                );

				std::array<VkWriteDescriptorSet, 7> writeDescriptorSets{};

				writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				writeDescriptorSets[0].descriptorCount = 1;
				writeDescriptorSets[0].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[0].dstBinding = 0;
				writeDescriptorSets[0].pBufferInfo = &this->initInfo_.scene_->MatricesUBOBuffers_[i].descriptor;

				writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				writeDescriptorSets[1].descriptorCount = 1;
				writeDescriptorSets[1].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[1].dstBinding = 1;
				writeDescriptorSets[1].pBufferInfo = &this->initInfo_.scene_->ParamsUBOBuffers_[i].descriptor;

				writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[2].descriptorCount = 1;
				writeDescriptorSets[2].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[2].dstBinding = 2;
				writeDescriptorSets[2].pImageInfo = &this->initInfo_.scene_->cubeMap_->irradianceCube_.descriptor_;

				writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[3].descriptorCount = 1;
				writeDescriptorSets[3].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[3].dstBinding = 3;
				writeDescriptorSets[3].pImageInfo = &this->initInfo_.scene_->cubeMap_->prefilteredCube_.descriptor_;

				writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[4].descriptorCount = 1;
				writeDescriptorSets[4].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[4].dstBinding = 4;
				writeDescriptorSets[4].pImageInfo = &this->textureList_.LUT_.descriptor_;

				writeDescriptorSets[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[5].descriptorCount = 1;
				writeDescriptorSets[5].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[5].dstBinding = 5;
				writeDescriptorSets[5].pImageInfo = &this->textureList_.Eu_.descriptor_;

				writeDescriptorSets[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[6].descriptorCount = 1;
				writeDescriptorSets[6].dstSet = this->descriptorSets_[i].scene;
				writeDescriptorSets[6].dstBinding = 6;
				writeDescriptorSets[6].pImageInfo = &this->textureList_.Eavg_.descriptor_;

				vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
			}
		}

		// Per-model descriptors: material samplers (set=1), material SSBO (set=3), mesh data (set=2)
		// Set layouts describe the *structure* -> created once and shared by all models
		if (this->descriptorSetLayouts_.material == VK_NULL_HANDLE)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
				{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());

			SUCCESS_OR_LOG(
                vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &this->descriptorSetLayouts_.material) == VK_SUCCESS,
                "Renderer: Failed to create descriptor set layout."
            );
		}

		if (this->descriptorSetLayouts_.materialBuffer == VK_NULL_HANDLE)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());

			SUCCESS_OR_LOG(
                vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &this->descriptorSetLayouts_.materialBuffer) == VK_SUCCESS,
                "Renderer: Failed to create descriptor set layout."
            );
		}

		if (this->descriptorSetLayouts_.meshDataBuffer == VK_NULL_HANDLE)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());

			SUCCESS_OR_LOG(
                vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &this->descriptorSetLayouts_.meshDataBuffer) == VK_SUCCESS,
                "Renderer: Failed to create descriptor set layout."
            );
		}

		// Descriptor sets are *instances* -> one set of descriptors per model
		for (auto& sceneObject : this->initInfo_.scene_->sceneObjects_)
		{
			resource::Model* model = sceneObject.model;

			// Material (samplers) — set=1
			{
				// Per-Material descriptor sets
				auto& materials = model->GetMaterialArray();
				for (auto &material : materials) {
				VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
				descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				descriptorSetAllocInfo.descriptorPool = descriptorPool;
				descriptorSetAllocInfo.pSetLayouts = &this->descriptorSetLayouts_.material;
				descriptorSetAllocInfo.descriptorSetCount = 1;

				SUCCESS_OR_LOG(
                    vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &material.descriptorSet) == VK_SUCCESS,
                    "Renderer: Failed to allocate descriptor sets."
                );

				std::vector<VkDescriptorImageInfo> imageDescriptors = {
					emptyTexture->descriptor_,
					emptyTexture->descriptor_,
					material.normalTexture ? material.normalTexture->descriptor_ : emptyTexture->descriptor_,
					material.occlusionTexture ? material.occlusionTexture->descriptor_ : emptyTexture->descriptor_,
					material.emissiveTexture ? material.emissiveTexture->descriptor_ : emptyTexture->descriptor_
				};

				if (material.pbrWorkflows.metallicRoughness) {
					if (material.baseColorTexture) {
						imageDescriptors[0] = material.baseColorTexture->descriptor_;
					}
					if (material.metallicRoughnessTexture) {
						imageDescriptors[1] = material.metallicRoughnessTexture->descriptor_;
					}
				} else {
					if (material.pbrWorkflows.specularGlossiness) {
						if (material.extension.diffuseTexture) {
							imageDescriptors[0] = material.extension.diffuseTexture->descriptor_;
						}
						if (material.extension.specularGlossinessTexture) {
							imageDescriptors[1] = material.extension.specularGlossinessTexture->descriptor_;
						}
					}
				}

				std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
				for (size_t i = 0; i < imageDescriptors.size(); i++) {
					writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					writeDescriptorSets[i].descriptorCount = 1;
					writeDescriptorSets[i].dstSet = material.descriptorSet;
					writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
					writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
				}

				vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
				}
			}

			// Material buffer — set=3 (per-model descriptor set, owned by the model)
			{
				VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
				descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				descriptorSetAllocInfo.descriptorPool = descriptorPool;
				descriptorSetAllocInfo.pSetLayouts = &this->descriptorSetLayouts_.materialBuffer;
				descriptorSetAllocInfo.descriptorSetCount = 1;

				SUCCESS_OR_LOG(
                    vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &model->GetDescriptorSetMaterial()) == VK_SUCCESS,
                    "Renderer: Failed to allocate descriptor sets."
                );

				VkWriteDescriptorSet writeDescriptorSet{};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writeDescriptorSet.descriptorCount = 1;
				writeDescriptorSet.dstSet = model->GetDescriptorSetMaterial();
				writeDescriptorSet.dstBinding = 0;
				writeDescriptorSet.pBufferInfo = &model->GetMaterialShaderBuffer().descriptor;
				vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), 1, &writeDescriptorSet, 0, nullptr);
			}

			// Mesh data buffer — set=2 (per-model, per-frame)
			{
				for (auto i = 0; i < model->GetDescriptorSetsMeshData().size(); i++) {
					VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
					descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
					descriptorSetAllocInfo.descriptorPool = descriptorPool;
					descriptorSetAllocInfo.pSetLayouts = &this->descriptorSetLayouts_.meshDataBuffer;
					descriptorSetAllocInfo.descriptorSetCount = 1;

					SUCCESS_OR_LOG(
                        vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &model->GetDescriptorSetsMeshData()[i]) == VK_SUCCESS,
                        "Renderer: Failed to allocate descriptor sets."
                    );

					VkWriteDescriptorSet writeDescriptorSet{};
					writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					writeDescriptorSet.descriptorCount = 1;
					writeDescriptorSet.dstSet = model->GetDescriptorSetsMeshData()[i];
					writeDescriptorSet.dstBinding = 0;
					writeDescriptorSet.pBufferInfo = &model->GetMeshShaderBuffer()[i].descriptor;
					vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), 1, &writeDescriptorSet, 0, nullptr);
				}
			}
		}	
	}
	
	void PBRRenderPass::RenderNode(resource::Model* model, resource::Node *node, VkCommandBuffer currentCB, uint32_t cbIndex, resource::Material::AlphaMode alphaMode)
	{
		if (node->mesh) {
			// Render mesh primitives
			for (resource::Primitive * primitive : node->mesh->primitives) {
				if (primitive->material.alphaMode == alphaMode) {
					std::string pipelineName = "pbr";
					std::string pipelineVariant = "";

					if (primitive->material.unlit) {
						// KHR_materials_unlit
						pipelineName = "unlit";
					};

					// Material properties define if we e.g. need to bind a pipeline variant with culling disabled (double sided)
					if (alphaMode == resource::Material::ALPHAMODE_BLEND) {
						pipelineVariant = "_alpha_blending";
					} else {
						if (primitive->material.doubleSided) {
							pipelineVariant = "_double_sided";
						}
					}

					const VkPipeline pipeline = this->pipelines_[pipelineName + pipelineVariant];

					if (pipeline != this->boundPipeline_) {
						vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
						this->boundPipeline_ = pipeline;
					}

					const std::vector<VkDescriptorSet> descriptorsets = {
						this->descriptorSets_[cbIndex].scene,
						primitive->material.descriptorSet,
						// @todo: per frame-in-flight
						model->GetDescriptorSetsMeshData()[cbIndex],
						// this->descriptorSetsMeshData_[cbIndex],
						model->GetDescriptorSetMaterial()
					};
					vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_, 0, static_cast<uint32_t>(descriptorsets.size()), descriptorsets.data(), 0, NULL);

					// Pass material index for this primitive using a push constant, the shader uses this to index into the material buffer
					MeshPushConstantBlock pushConstantBlock{};
					// @todo: index
					pushConstantBlock.meshIndex = node->mesh->index;
					pushConstantBlock.materialIndex = primitive->material.index;
					vkCmdPushConstants(currentCB, this->pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstantBlock), &pushConstantBlock);

					if (primitive->hasIndices) {
						vkCmdDrawIndexed(currentCB, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
					} else {
						vkCmdDraw(currentCB, primitive->vertexCount, 1, 0, 0);
					}
				}
			}

		};
		for (auto child : node->children) {
			this->RenderNode(model, child, currentCB, cbIndex, alphaMode);
		}
	}

    void PBRRenderPass::SetUpPipeline(const std::string vertexShader, const std::string fragmentShader)
	{
		auto& device = core::Device::Instance();
		VkPipelineCache& pipelineCache = *(this->initInfo_.pipelineCache_);
		VkRenderPass& mainRenderPass = *(this->initInfo_.mainRenderPass_);

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
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
		depthStencilStateCI.depthTestEnable = VK_TRUE;
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
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
				this->descriptorSetLayouts_.scene, this->descriptorSetLayouts_.material, this->descriptorSetLayouts_.meshDataBuffer, this->descriptorSetLayouts_.materialBuffer
			};
			VkPipelineLayoutCreateInfo pipelineLayoutCI{};
			pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
			pipelineLayoutCI.pSetLayouts = setLayouts.data();
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.size = sizeof(MeshPushConstantBlock);
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pipelineLayoutCI.pushConstantRangeCount = 1;
			pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

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
			{ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(resource::Model::Vertex, uv0) },
			{ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(resource::Model::Vertex, uv1) },
			{ 4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(resource::Model::Vertex, joint0) },
			{ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(resource::Model::Vertex, weight0) },
			{ 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(resource::Model::Vertex, color) }
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
		pipelineCI.renderPass = mainRenderPass;
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
		// Default pipeline with back-face culling

		SUCCESS_OR_LOG(
			vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), pipelineCache, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
			"Renderer: Failed to create graphics pipeline."
		);


		this->pipelines_.insert(std::make_pair("pbr", pipeline));


		// Double sided
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;

		SUCCESS_OR_LOG(
			vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), pipelineCache, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
			"Renderer: Failed to create graphics pipelines."
		);

		this->pipelines_.insert(std::make_pair("pbr_double_sided", pipeline));

		// Alpha blending
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

		SUCCESS_OR_LOG(
			vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), pipelineCache, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
			"Renderer: Failed to create graphics pipeline."
		);

		this->pipelines_.insert(std::make_pair("pbr_alpha_blending", pipeline));

		for (auto shaderStage : shaderStages) {
			vkDestroyShaderModule(device.GetLogicalDeviceHandle(), shaderStage.module, nullptr);
		}
	}

    PBRRenderPass::PBRRenderPass() 
    {
		this->descriptorSets_.resize(core::Device::Instance().GetSetting().frameCount_);
    }

    PBRRenderPass::~PBRRenderPass()
    {
        this->Cleanup();
    }


	void PBRRenderPass::UpdateUniformData(uint32_t frameIndex) 
	{

	}

    DescriptorSetCount PBRRenderPass::GetDescriptorSetCount()
	{
	    auto& device = core::Device::Instance();
		uint32_t frameCount = device.GetSetting().frameCount_;
		auto& sceneObjects = this->initInfo_.scene_->sceneObjects_;

		// set=0 (scene): one set per frame, each holding 2 UBO + 5 samplers
		uint32_t uniformBufferCount = 2 * frameCount;
		uint32_t imageSamplerCount = 5 * frameCount;
		uint32_t maxSets = frameCount;

		uint32_t storageBufferCount = 0;

		for (auto& sceneObject : sceneObjects)
		{
			resource::Model* model = sceneObject.model;
			uint32_t materialCount = model->GetMaterialCount();

			// set=1 (material): one set per material, each holding 5 samplers
			imageSamplerCount += 5 * materialCount;
			maxSets += materialCount;

			// set=3 (materialBuffer): one set per model, each holding 1 SSBO
			storageBufferCount += 1;
			maxSets += 1;

			// set=2 (meshData): one set per model per frame, each holding 1 SSBO
			// descriptorSetsMeshData_ is resized to frameCount in CreateMeshDataBuffer()
			storageBufferCount += static_cast<uint32_t>(model->GetDescriptorSetsMeshData().size());
			maxSets += static_cast<uint32_t>(model->GetDescriptorSetsMeshData().size());
		}

		DescriptorSetCount resource
		{
			.uniformBufferCount = uniformBufferCount,
			.imageSamplerCount = imageSamplerCount,
			.storageBufferCount = storageBufferCount,
			.maxSets = maxSets
		};

		return resource;
	}

    void PBRRenderPass::Init(const RenderPassInitInfo& initInfo)
    {
		this->initInfo_ = initInfo;
    }

	void PBRRenderPass::ExecutePreProcess()
	{
		LOG_INFO("PBRRenderPass: start to execute pre-process...");
        engine::render::FullScreenPassConfig config;
		std::string assetPath = resource::ResourceManager::assetPath_;
        config.vertShader = assetPath + "shaders/genbrdflut.vert.spv";
        config.fragShader = assetPath + "shaders/genbrdflut.frag.spv";
        config.outputFormat = VK_FORMAT_R16G16_SFLOAT;
        this->textureList_.LUT_ = this->PreComputeTexture(config);
        
        config.vertShader = assetPath + "shaders/genEuIS.vert.spv";
        config.fragShader = assetPath + "shaders/genEuIS.frag.spv";
        config.outputFormat = VK_FORMAT_R16G16_SFLOAT;
        this->textureList_.Eu_ = this->PreComputeTexture(config);

        config.vertShader = assetPath + "shaders/genEavg.vert.spv";
        config.fragShader = assetPath + "shaders/genEavg.frag.spv";
        config.outputFormat = VK_FORMAT_R16G16_SFLOAT;
		config.inputTexture = &this->textureList_.Eu_;
    	this->textureList_.Eavg_ = this->PreComputeTexture(config);

		this->PreProcess();
	}

    void PBRRenderPass::Execute(VkCommandBuffer currentCB, uint32_t frameIndex)
    {
		static auto beginLable = core::Device::Instance().GetCmdBeginDebugUtilsLabel();
		static auto endLable = core::Device::Instance().GetCmdEndDebugUtilsLabel();

		if (beginLable) 
		{
			VkDebugUtilsLabelEXT labelInfo{};
			labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
			labelInfo.pLabelName = "PBRRenderPass";
			labelInfo.color[0] = 1.0f;
			labelInfo.color[1] = 1.0f;
			labelInfo.color[2] = 0.0f;
			labelInfo.color[3] = 1.0f;
			beginLable(currentCB, &labelInfo);
		}

		VkDeviceSize offsets[1] = { 0 };
		auto& sceneObjects = this->initInfo_.scene_->sceneObjects_;

		this->boundPipeline_ = VK_NULL_HANDLE;

		for (auto& sceneObject : sceneObjects) {
			resource::Model* model = sceneObject.model;
			// 每个 model 绑定自己的 VBO/IBO
			VkBuffer vbo = model->GetVertexBuffer();
			vkCmdBindVertexBuffers(currentCB, 0, 1, &vbo, offsets);
			VkBuffer ibo = model->GetIndexBuffer();
			if (ibo != VK_NULL_HANDLE) {
				vkCmdBindIndexBuffer(currentCB, ibo, 0, VK_INDEX_TYPE_UINT32);
			}

			// Opaque primitives first
			for (auto node : model->GetNodes()) {
				this->RenderNode(model, node, currentCB, frameIndex, resource::Material::ALPHAMODE_OPAQUE);
			}
			// Alpha masked primitives
			for (auto node : model->GetNodes())
			{
				this->RenderNode(model, node, currentCB, frameIndex, resource::Material::ALPHAMODE_MASK);
			}
			// Transparent primitives
			// TODO: Correct depth sorting
			for (auto node : model->GetNodes()) {
				this->RenderNode(model, node, currentCB, frameIndex, resource::Material::ALPHAMODE_BLEND);
			}
		}

		if (endLable)
		{
			endLable(currentCB);
		}
    }

    void PBRRenderPass::Cleanup()
    {
        auto& device = core::Device::Instance();

		auto DestroyLayout = [&](VkDescriptorSetLayout& layout) 
		{
  	    	if (layout != VK_NULL_HANDLE) 
			{
   	        	vkDestroyDescriptorSetLayout(device.GetLogicalDeviceHandle(), layout, nullptr);
            	layout = VK_NULL_HANDLE;
        	}
    	};

    	DestroyLayout(descriptorSetLayouts_.scene);
    	DestroyLayout(descriptorSetLayouts_.material);
    	DestroyLayout(descriptorSetLayouts_.materialBuffer);
    	DestroyLayout(descriptorSetLayouts_.meshDataBuffer);

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

    }
    
}


