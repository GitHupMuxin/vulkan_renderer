#include "engine/utils/log.h"
#include "engine/core/loader.h"
#include "engine/resource/environment_lighting.h"
#include "engine/resource/resource_manager.h"

namespace engine::resource
{
   
    void EnvironmentCubeMap::Init(std::string name)
    {
		this->name_ = name;
    }

    void EnvironmentCubeMap::Destroy()
    {
        this->environmentCube_.Destroy();
        this->irradianceCube_.Destroy();
        this->prefilteredCube_.Destroy();
    }

    void EnvironmentCubeMap::LoadFromFile(std::string fileName, VkFormat format, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
    {
		LOG_INFO("EnvironmentCubeMap: Loading environment from file: " + fileName);

        this->Destroy();

        this->environmentCube_.LoadFromFile(fileName, format);

        this->GenerateCubemaps();
    }


    void EnvironmentCubeMap::GenerateCubemaps()
    {
		auto& device = core::Device::Instance();

        enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };
		for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++) 
		{

		    resource::TextureCubeMap cubemap;

			auto tStart = std::chrono::high_resolution_clock::now();

			VkFormat format;
			int32_t dim;

			switch (target) 
			{
			case IRRADIANCE:
				format = VK_FORMAT_R32G32B32A32_SFLOAT;
				dim = 64;
				break;
			case PREFILTEREDENV:
				format = VK_FORMAT_R16G16B16A16_SFLOAT;
				dim = 512;
				break;
			};

			const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

			// Create target cubemap
			{
				// Image
				VkImageCreateInfo imageCI{};
				imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageCI.imageType = VK_IMAGE_TYPE_2D;
				imageCI.format = format;
				imageCI.extent.width = dim;
				imageCI.extent.height = dim;
				imageCI.extent.depth = 1;
				imageCI.mipLevels = numMips;
				imageCI.arrayLayers = 6;
				imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

                SUCCESS_OR_LOG(
                    vkCreateImage(device.GetLogicalDeviceHandle(), &imageCI, nullptr, &cubemap.image_) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to create image."
                );

				VkMemoryRequirements memReqs;
				vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), cubemap.image_, &memReqs);
				VkMemoryAllocateInfo memAllocInfo{};
				memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				memAllocInfo.allocationSize = memReqs.size;
				memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

				SUCCESS_OR_LOG(
                    vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &cubemap.deviceMemory_) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to allocate memory."
                );

			    SUCCESS_OR_LOG(
                    vkBindImageMemory(device.GetLogicalDeviceHandle(), cubemap.image_, cubemap.deviceMemory_, 0) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to bind image memory."
                );

				// View
				VkImageViewCreateInfo viewCI{};
				viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
				viewCI.format = format;
				viewCI.subresourceRange = {};
				viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewCI.subresourceRange.levelCount = numMips;
				viewCI.subresourceRange.layerCount = 6;
				viewCI.image = cubemap.image_;

				SUCCESS_OR_LOG(
                    vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCI, nullptr, &cubemap.view_) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to create image view."
                );

				// Sampler
				VkSamplerCreateInfo samplerCI{};
				samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				samplerCI.magFilter = VK_FILTER_LINEAR;
				samplerCI.minFilter = VK_FILTER_LINEAR;
				samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerCI.minLod = 0.0f;
				samplerCI.maxLod = static_cast<float>(numMips);
				samplerCI.maxAnisotropy = 1.0f;
				samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

				SUCCESS_OR_LOG(
                    vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerCI, nullptr, &cubemap.sampler_) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to create sampler."
                );
			}

			// FB, Att, RP, Pipe, etc.
			VkAttachmentDescription attDesc{};
			// Color attachment
			attDesc.format = format;
			attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpassDescription{};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;

			// Use subpass dependencies for layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Renderpass
			VkRenderPassCreateInfo renderPassCI{};
			renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCI.attachmentCount = 1;
			renderPassCI.pAttachments = &attDesc;
			renderPassCI.subpassCount = 1;
			renderPassCI.pSubpasses = &subpassDescription;
			renderPassCI.dependencyCount = 2;
			renderPassCI.pDependencies = dependencies.data();
			VkRenderPass renderpass;

			SUCCESS_OR_LOG(
                vkCreateRenderPass(device.GetLogicalDeviceHandle(), &renderPassCI, nullptr, &renderpass) == VK_SUCCESS,
                "EnvironmentCubeMap: Failed to create render pass."
            );

			struct Offscreen {
				VkImage image;
				VkImageView view;
				VkDeviceMemory memory;
				VkFramebuffer framebuffer;
			} offscreen;

			// Create offscreen framebuffer
			{
				// Image
				VkImageCreateInfo imageCI{};
				imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageCI.imageType = VK_IMAGE_TYPE_2D;
				imageCI.format = format;
				imageCI.extent.width = dim;
				imageCI.extent.height = dim;
				imageCI.extent.depth = 1;
				imageCI.mipLevels = 1;
				imageCI.arrayLayers = 1;
				imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
				imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				SUCCESS_OR_LOG(
                    vkCreateImage(device.GetLogicalDeviceHandle(), &imageCI, nullptr, &offscreen.image) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to create image."
                );

				VkMemoryRequirements memReqs;
				vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), offscreen.image, &memReqs);
				VkMemoryAllocateInfo memAllocInfo{};
				memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				memAllocInfo.allocationSize = memReqs.size;
				memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

				SUCCESS_OR_LOG(
                    vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &offscreen.memory) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to allocate memory."
                );

				SUCCESS_OR_LOG(
                    vkBindImageMemory(device.GetLogicalDeviceHandle(), offscreen.image, offscreen.memory, 0) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to bind image memory."
                );

				// View
				VkImageViewCreateInfo viewCI{};
				viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewCI.format = format;
				viewCI.flags = 0;
				viewCI.subresourceRange = {};
				viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewCI.subresourceRange.baseMipLevel = 0;
				viewCI.subresourceRange.levelCount = 1;
				viewCI.subresourceRange.baseArrayLayer = 0;
				viewCI.subresourceRange.layerCount = 1;
				viewCI.image = offscreen.image;

				SUCCESS_OR_LOG(
                    vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCI, nullptr, &offscreen.view) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to create image view."
                );

				// Framebuffer
				VkFramebufferCreateInfo framebufferCI{};
				framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferCI.renderPass = renderpass;
				framebufferCI.attachmentCount = 1;
				framebufferCI.pAttachments = &offscreen.view;
				framebufferCI.width = dim;
				framebufferCI.height = dim;
				framebufferCI.layers = 1;
				SUCCESS_OR_LOG(
                    vkCreateFramebuffer(device.GetLogicalDeviceHandle(), &framebufferCI, nullptr, &offscreen.framebuffer) == VK_SUCCESS,
                    "EnvironmentCubeMap: Failed to create framebuffer."
                );

				VkCommandBuffer layoutCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = offscreen.image;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdPipelineBarrier(layoutCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				device.FlushCommandBuffer(layoutCmd, true);
			}

			// Descriptors
			VkDescriptorSetLayout descriptorsetlayout;
			VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = &setLayoutBinding;
			descriptorSetLayoutCI.bindingCount = 1;
            SUCCESS_OR_LOG(
                vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &descriptorsetlayout) == VK_SUCCESS,
                "EnvironmentCubeMap: Failed to create descriptor set layout."
            );

			// Descriptor Pool
			VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
			VkDescriptorPoolCreateInfo descriptorPoolCI{};
			descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCI.poolSizeCount = 1;
			descriptorPoolCI.pPoolSizes = &poolSize;
			descriptorPoolCI.maxSets = 2;
			VkDescriptorPool descriptorpool;
            SUCCESS_OR_LOG(
                vkCreateDescriptorPool(device.GetLogicalDeviceHandle(), &descriptorPoolCI, nullptr, &descriptorpool) == VK_SUCCESS,
                "EnvironmentCubeMap: Failed to create descriptor pool."
            );

			// Descriptor sets
			VkDescriptorSet descriptorset;
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorpool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
			descriptorSetAllocInfo.descriptorSetCount = 1;
            SUCCESS_OR_LOG(
                vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &descriptorset) == VK_SUCCESS,
                "EnvironmentCubeMap: Failed to allocate descriptor sets."
            );
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = descriptorset;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pImageInfo = &this->environmentCube_.descriptor_;
			vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), 1, &writeDescriptorSet, 0, nullptr);

			struct PushBlockIrradiance {
				glm::mat4 mvp;
				float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
				float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
			} pushBlockIrradiance;

			struct PushBlockPrefilterEnv {
				glm::mat4 mvp;
				float roughness;
				uint32_t numSamples = 32u;
			} pushBlockPrefilterEnv;

			// Pipeline layout
			VkPipelineLayout pipelinelayout;
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

			switch (target) {
				case IRRADIANCE:
					pushConstantRange.size = sizeof(PushBlockIrradiance);
					break;
				case PREFILTEREDENV:
					pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
					break;
			};

			VkPipelineLayoutCreateInfo pipelineLayoutCI{};
			pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCI.setLayoutCount = 1;
			pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
			pipelineLayoutCI.pushConstantRangeCount = 1;
			pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
            SUCCESS_OR_LOG(
                vkCreatePipelineLayout(device.GetLogicalDeviceHandle(), &pipelineLayoutCI, nullptr, &pipelinelayout) == VK_SUCCESS,
                "EnvironmentCubeMap: Failed to create pipeline layout."
            );

			// Pipeline
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
			multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			
			std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dynamicStateCI{};
			dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
			dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

			// Vertex input state
			VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(resource::GLTFModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
			VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

			VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
			vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputStateCI.vertexBindingDescriptionCount = 1;
			vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
			vertexInputStateCI.vertexAttributeDescriptionCount = 1;
			vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;

			std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

			VkGraphicsPipelineCreateInfo pipelineCI{};
			pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineCI.layout = pipelinelayout;
			pipelineCI.renderPass = renderpass;
			pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
			pipelineCI.pVertexInputState = &vertexInputStateCI;
			pipelineCI.pRasterizationState = &rasterizationStateCI;
			pipelineCI.pColorBlendState = &colorBlendStateCI;
			pipelineCI.pMultisampleState = &multisampleStateCI;
			pipelineCI.pViewportState = &viewportStateCI;
			pipelineCI.pDepthStencilState = &depthStencilStateCI;
			pipelineCI.pDynamicState = &dynamicStateCI;
			pipelineCI.stageCount = 2;
			pipelineCI.pStages = shaderStages.data();
			pipelineCI.renderPass = renderpass;

			std::string assetPath = resource::ResourceManager::Instance().assetPath_;

			shaderStages[0] = core::Loader::LoadShader(device.GetLogicalDeviceHandle(), assetPath + "shaders/filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			switch (target) {
				case IRRADIANCE:
					shaderStages[1] = core::Loader::LoadShader(device.GetLogicalDeviceHandle(), assetPath + "shaders/irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
					break;
				case PREFILTEREDENV:
					shaderStages[1] = core::Loader::LoadShader(device.GetLogicalDeviceHandle(), assetPath + "shaders/prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
					break;
			};
			VkPipeline pipeline;
            SUCCESS_OR_LOG(
                vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
                "EnvironmentCubeMap: Failed to create graphics pipeline."
            );
			for (auto shaderStage : shaderStages) {
				vkDestroyShaderModule(device.GetLogicalDeviceHandle(), shaderStage.module, nullptr);
			}

			// Render cubemap
			VkClearValue clearValues[1];
			clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

			VkRenderPassBeginInfo renderPassBeginInfo{};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderpass;
			renderPassBeginInfo.framebuffer = offscreen.framebuffer;
			renderPassBeginInfo.renderArea.extent.width = dim;
			renderPassBeginInfo.renderArea.extent.height = dim;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = clearValues;

			std::vector<glm::mat4> matrices = {
				glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			};

			VkCommandBuffer cmdBuf = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);

			VkViewport viewport{};
			viewport.width = (float)dim;
			viewport.height = (float)dim;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor{};
			scissor.extent.width = dim;
			scissor.extent.height = dim;

			VkImageSubresourceRange subresourceRange{};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = numMips;
			subresourceRange.layerCount = 6;

			// Change image layout for all cubemap faces to transfer destination
			{
				device.BeginCommandBuffer(cmdBuf);
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = cubemap.image_;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				device.FlushCommandBuffer(cmdBuf, false);
			}

			for (uint32_t m = 0; m < numMips; m++) {
				for (uint32_t f = 0; f < 6; f++) {

					device.BeginCommandBuffer(cmdBuf);

					viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
					viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
					vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
					vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

					// Render scene from cube face's point of view
					vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					// Pass parameters for current pass using a push constant block
					switch (target) {
						case IRRADIANCE:
							pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
							vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
							break;
						case PREFILTEREDENV:
							pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];
							pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
							vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
							break;
					};

					vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

					VkDeviceSize offsets[1] = { 0 };

					Model* skybox = resource::ResourceManager::Instance().skybox_.get();  // change ResourceManager to singleton design pattern
					skybox->Draw(cmdBuf);

					vkCmdEndRenderPass(cmdBuf);

					VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					subresourceRange.baseMipLevel = 0;
					subresourceRange.levelCount = numMips;
					subresourceRange.layerCount = 6;

					{
						VkImageMemoryBarrier imageMemoryBarrier{};
						imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						imageMemoryBarrier.image = offscreen.image;
						imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
					}

					// Copy region for transfer from framebuffer to cube face
					VkImageCopy copyRegion{};

					copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.srcSubresource.baseArrayLayer = 0;
					copyRegion.srcSubresource.mipLevel = 0;
					copyRegion.srcSubresource.layerCount = 1;
					copyRegion.srcOffset = { 0, 0, 0 };

					copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					copyRegion.dstSubresource.baseArrayLayer = f;
					copyRegion.dstSubresource.mipLevel = m;
					copyRegion.dstSubresource.layerCount = 1;
					copyRegion.dstOffset = { 0, 0, 0 };

					copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
					copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
					copyRegion.extent.depth = 1;

					vkCmdCopyImage(
						cmdBuf,
						offscreen.image,
						VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						cubemap.image_,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1,
						&copyRegion);

					{
						VkImageMemoryBarrier imageMemoryBarrier{};
						imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
						imageMemoryBarrier.image = offscreen.image;
						imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
						imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
						vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
					}

					device.FlushCommandBuffer(cmdBuf, false);
				}
			}

			{
				device.BeginCommandBuffer(cmdBuf);
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.image = cubemap.image_;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				device.FlushCommandBuffer(cmdBuf, true);
			}


			vkDestroyRenderPass(device.GetLogicalDeviceHandle(), renderpass, nullptr);
			vkDestroyFramebuffer(device.GetLogicalDeviceHandle(), offscreen.framebuffer, nullptr);
			vkFreeMemory(device.GetLogicalDeviceHandle(), offscreen.memory, nullptr);
			vkDestroyImageView(device.GetLogicalDeviceHandle(), offscreen.view, nullptr);
			vkDestroyImage(device.GetLogicalDeviceHandle(), offscreen.image, nullptr);
			vkDestroyDescriptorPool(device.GetLogicalDeviceHandle(), descriptorpool, nullptr);
			vkDestroyDescriptorSetLayout(device.GetLogicalDeviceHandle(), descriptorsetlayout, nullptr);
			vkDestroyPipeline(device.GetLogicalDeviceHandle(), pipeline, nullptr);
			vkDestroyPipelineLayout(device.GetLogicalDeviceHandle(), pipelinelayout, nullptr);

			cubemap.descriptor_.imageView = cubemap.view_;
			cubemap.descriptor_.sampler = cubemap.sampler_;
			cubemap.descriptor_.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			switch (target) {
				case IRRADIANCE:
					this->irradianceCube_ = std::move(cubemap);
					break;
				case PREFILTEREDENV:
					this->prefilteredCube_ = std::move(cubemap);
					this->prefilteredCubeMipLevels_ = static_cast<float>(numMips);
					break;
			};

			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
		}
    }

    uint32_t EnvironmentCubeMap::GetPrefilteredCubeMipLevels()
	{
		return this->prefilteredCubeMipLevels_;
	}


}





