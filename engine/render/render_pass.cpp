#include <array>
#include "engine/core/loader.h"
#include "engine/core/descriptor_allocator.h"
#include "engine/core/descriptor_layout_registry.h"
#include "engine/core/schema.h"
#include "engine/render/render_pass.h"
#include "engine/resource/resource_manager.h"
#include "engine/utils/log.h"

namespace engine::render
{

    // 收集 RenderScene 显式注册的 model handle（去重）。
    // RenderScene 提供 modelHandles（SceneExtractor 从场景填充），
    // descriptor 分配、就绪轮询等"资源存在性"遍历用它，而不是从 RenderItem 队列收集：
    // 上传在途（Uploading）的模型不产生 RenderItem（渲染门控），但必须分配 descriptor set。
    static std::vector<resource::ModelHandle> CollectUniqueModels(const RenderScene& renderScene)
    {
        std::vector<resource::ModelHandle> handles;
        for (const auto& h : renderScene.modelHandles)
        {
            bool found = false;
            for (const auto& existing : handles)
            {
                if (existing.index == h.index && existing.generation == h.generation)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                handles.push_back(h);
            }
        }
        return handles;
    }


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

		VkDescriptorSetLayout skyboxSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kSkyboxSet);

		// Skybox (fixed set)
		for (auto i = 0; i < this->skyboxSets_.size(); i++)
		{
			this->skyboxSets_[i] = core::DescriptorAllocator::Instance().AllocatePersistent(skyboxSetLayout);

			std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

			// binding=0：相机矩阵 UBO（Renderer 共享 matricesUBO，布局 UBOMatricesUpload）
			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = this->skyboxSets_[i];
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &(*this->initInfo_.matricesUBOBuffers_)[i].descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = this->skyboxSets_[i];
			writeDescriptorSets[1].dstBinding = 1;
			writeDescriptorSets[1].pBufferInfo = &(*this->initInfo_.paramsUBOBuffers_)[i].descriptor;

			writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[2].descriptorCount = 1;
			writeDescriptorSets[2].dstSet = this->skyboxSets_[i];
			writeDescriptorSets[2].dstBinding = 2;
			writeDescriptorSets[2].pImageInfo = &resource::ResourceManager::Instance().GetEnvironmentCubeMap(this->initInfo_.renderScene_->environment.environmentCubeMap)->prefilteredCube_.descriptor_;

			vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void SkyBoxRenderPass::SetUpPipeline(const std::string vertexShader, const std::string fragmentShader)
	{
		auto& device = core::Device::Instance();

		VkDescriptorSetLayout skyboxSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kSkyboxSet);

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
		this->skyboxSets_.resize(frameCount);
	}

	SkyBoxRenderPass::~SkyBoxRenderPass()
	{
		this->Cleanup();
	}

    void SkyBoxRenderPass::UpdateUniformData(uint32_t frameIndex) 
	{
		UBOMatricesUpload matrices;
		matrices.projection = this->initInfo_.renderScene_->camera.projection;
		// skybox 的 model = mat3(view)：保持相机旋转，去掉平移（天空盒跟随视角但不移动）
		matrices.model = glm::mat4(glm::mat3(this->initInfo_.renderScene_->camera.view));

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

		static resource::Model* skyboxModel = resource::ResourceManager::Instance().skybox_.get();

		VkDeviceSize offsets[1] = { 0 };

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
		resource::Texture2D* emptyTexture = resource::ResourceManager::Instance().emptyTexture2D_.get();
		// Scene (matrices and environment maps)
		{
			VkDescriptorSetLayout sceneSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kSceneSet);


			for (auto i = 0; i < sceneSets_.size(); i++)
			{
				this->sceneSets_[i] = core::DescriptorAllocator::Instance().AllocatePersistent(sceneSetLayout);

				std::array<VkWriteDescriptorSet, 7> writeDescriptorSets{};

				writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				writeDescriptorSets[0].descriptorCount = 1;
				writeDescriptorSets[0].dstSet = this->sceneSets_[i];
				writeDescriptorSets[0].dstBinding = 0;
				writeDescriptorSets[0].pBufferInfo = &(*this->initInfo_.matricesUBOBuffers_)[i].descriptor;

				writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				writeDescriptorSets[1].descriptorCount = 1;
				writeDescriptorSets[1].dstSet = this->sceneSets_[i];
				writeDescriptorSets[1].dstBinding = 1;
				writeDescriptorSets[1].pBufferInfo = &(*this->initInfo_.paramsUBOBuffers_)[i].descriptor;

				writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[2].descriptorCount = 1;
				writeDescriptorSets[2].dstSet = this->sceneSets_[i];
				writeDescriptorSets[2].dstBinding = 2;
				writeDescriptorSets[2].pImageInfo = &resource::ResourceManager::Instance().GetEnvironmentCubeMap(this->initInfo_.renderScene_->environment.environmentCubeMap)->irradianceCube_.descriptor_;

				writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[3].descriptorCount = 1;
				writeDescriptorSets[3].dstSet = this->sceneSets_[i];
				writeDescriptorSets[3].dstBinding = 3;
				writeDescriptorSets[3].pImageInfo = &resource::ResourceManager::Instance().GetEnvironmentCubeMap(this->initInfo_.renderScene_->environment.environmentCubeMap)->prefilteredCube_.descriptor_;

				writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[4].descriptorCount = 1;
				writeDescriptorSets[4].dstSet = this->sceneSets_[i];
				writeDescriptorSets[4].dstBinding = 4;
				writeDescriptorSets[4].pImageInfo = &this->textureList_.lut_.descriptor_;

				writeDescriptorSets[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[5].descriptorCount = 1;
				writeDescriptorSets[5].dstSet = this->sceneSets_[i];
				writeDescriptorSets[5].dstBinding = 5;
				writeDescriptorSets[5].pImageInfo = &this->textureList_.eu_.descriptor_;

				writeDescriptorSets[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[6].descriptorCount = 1;
				writeDescriptorSets[6].dstSet = this->sceneSets_[i];
				writeDescriptorSets[6].dstBinding = 6;
				writeDescriptorSets[6].pImageInfo = &this->textureList_.eavg_.descriptor_;

				vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
			}
		}

		// Descriptor sets are *instances* -> one set of descriptors per model
		auto* scene = this->initInfo_.renderScene_;
		auto modelHandles = CollectUniqueModels(*scene);
		for (const auto& modelHandle : modelHandles)
		{
			// PeekModel：资源存在即分配 descriptor set（buffer 已创建），与渲染就绪解耦
			resource::Model* model = resource::ResourceManager::Instance().PeekModel(modelHandle);
			if (model == nullptr)
			{
				continue;
			}

			// Material (samplers) — set=1
			{
				// Per-Material descriptor sets
				auto& materials = model->GetMaterialArray();
				for (auto &material : materials)
				{
					std::vector<VkDescriptorImageInfo> imageDescriptors = {
						emptyTexture->descriptor_,
						emptyTexture->descriptor_,
						material.normalTexture ? material.normalTexture->descriptor_ : emptyTexture->descriptor_,
						material.occlusionTexture ? material.occlusionTexture->descriptor_ : emptyTexture->descriptor_,
						material.emissiveTexture ? material.emissiveTexture->descriptor_ : emptyTexture->descriptor_
					};

					if (material.pbrWorkflows.metallicRoughness)
					{
						if (material.baseColorTexture)
						{
							imageDescriptors[0] = material.baseColorTexture->descriptor_;
						}
						if (material.metallicRoughnessTexture)
						{
							imageDescriptors[1] = material.metallicRoughnessTexture->descriptor_;
						}
					}
					else
					{
						if (material.pbrWorkflows.specularGlossiness)
						{
							if (material.extension.diffuseTexture)
							{
								imageDescriptors[0] = material.extension.diffuseTexture->descriptor_;
							}
							if (material.extension.specularGlossinessTexture)
							{
								imageDescriptors[1] = material.extension.specularGlossinessTexture->descriptor_;
							}
						}
					}

					std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
					for (size_t i = 0; i < imageDescriptors.size(); i++)
					{
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
				for (auto i = 0; i < model->GetDescriptorSetsMeshData().size(); i++)
				{
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
	
    VkPipeline PBRRenderPass::SelectPipeline(PipelineVariant variant)
    {
        switch (variant)
        {
            case PipelineVariant::Pbr:           return this->pipelines_["pbr"];
            case PipelineVariant::Unlit:         return this->pipelines_["pbr"];         // @todo: 尚无 unlit 管线，暂回退 pbr
            case PipelineVariant::DoubleSided:   return this->pipelines_["pbr_double_sided"];
            case PipelineVariant::AlphaBlending: return this->pipelines_["pbr_alpha_blending"];
        }
        return this->pipelines_["pbr"];
    }

    void PBRRenderPass::DrawQueue(const std::vector<RenderItem>& items, VkCommandBuffer cb, uint32_t frameIndex)
	{	
		auto& rm = resource::ResourceManager::Instance();
		resource::Model* currentModel = nullptr;
		VkPipeline currentPipeline = VK_NULL_HANDLE;

		VkDeviceSize offsets[1] = { 0 };

		for (const RenderItem& item : items)
		{
			resource::Model* model = rm.GetModel(item.modelHandle);
			if (!model) continue;

			// VBO/IBO 只在 model 切换时重绑
			if (model != currentModel)
			{
				VkBuffer vbo = model->GetVertexBuffer();
				vkCmdBindVertexBuffers(cb, 0, 1, &vbo, offsets);
				if (model->GetIndexBuffer()) vkCmdBindIndexBuffer(cb, model->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
				currentModel = model;
			}

			// pipeline 选择：从 item 枚举查表（不再现场判断材质属性）
			VkPipeline pipeline = SelectPipeline(item.pipeline);
			if (pipeline != currentPipeline)
			{
				vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				currentPipeline = pipeline;
			}

			// set 绑定：set0(scene) + set1(material) + set2(meshData) + set3(materialSSBO)
			const std::vector<VkDescriptorSet> descriptorSets = {
				this->sceneSets_[frameIndex],
				item.materialDescriptorSet,
				model->GetDescriptorSetsMeshData()[frameIndex],
				model->GetDescriptorSetMaterial()
			};
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipelineLayout_,
				0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);

			// push constant：shader 用它索引 SSBO（材质 + 骨骼矩阵）
			MeshPushConstantBlock pushConstantBlock{};
			pushConstantBlock.meshIndex = static_cast<int32_t>(item.meshIndex);
			pushConstantBlock.materialIndex = static_cast<int32_t>(item.materialIndex);
			vkCmdPushConstants(cb, this->pipelineLayout_,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(MeshPushConstantBlock), &pushConstantBlock);

			// draw：拍平后的索引区间
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

    void PBRRenderPass::SetUpPipeline(const std::string vertexShader, const std::string fragmentShader)
	{
		auto& device = core::Device::Instance();

		VkDescriptorSetLayout sceneSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kSceneSet);
		VkDescriptorSetLayout materialSetLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kMaterialSet);
		VkDescriptorSetLayout materialBufferLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kMaterialSSBO);
		VkDescriptorSetLayout meshDataLayout = core::DescriptorLayoutRegistry::Instance().GetOrCreate(schema::kMeshDataSSBO);

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
				sceneSetLayout, materialSetLayout, meshDataLayout, materialBufferLayout
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
		this->sceneSets_.resize(core::Device::Instance().GetSetting().frameCount_);
    }

    PBRRenderPass::~PBRRenderPass()
    {
        this->Cleanup();
    }


	void PBRRenderPass::UpdateUniformData(uint32_t frameIndex) 
	{

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
        this->textureList_.lut_ = this->PreComputeTexture(config);
        
        config.vertShader = assetPath + "shaders/genEuIS.vert.spv";
        config.fragShader = assetPath + "shaders/genEuIS.frag.spv";
        config.outputFormat = VK_FORMAT_R16G16_SFLOAT;
        this->textureList_.eu_ = this->PreComputeTexture(config);

        config.vertShader = assetPath + "shaders/genEavg.vert.spv";
        config.fragShader = assetPath + "shaders/genEavg.frag.spv";
        config.outputFormat = VK_FORMAT_R16G16_SFLOAT;
		config.inputTexture = &this->textureList_.eu_;
    	this->textureList_.eavg_ = this->PreComputeTexture(config);

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

		auto* scene = this->initInfo_.renderScene_;

		this->boundPipeline_ = VK_NULL_HANDLE;

		const RenderScene* renderScene = this->initInfo_.renderScene_;

		DrawQueue(renderScene->opaqueItems, currentCB, frameIndex);
		DrawQueue(renderScene->maskedItems, currentCB, frameIndex);
		DrawQueue(renderScene->transparentItems, currentCB, frameIndex);

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

		for (auto& sceneSet : this->sceneSets_)
		{
			core::DescriptorAllocator::Instance().FreePersistent(sceneSet);
		}

    }

}


