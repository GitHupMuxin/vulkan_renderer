#include "engine/core/loader.h"
#include "engine/render/fullscreen_pass.h"
#include "engine/utils/log.h"

namespace engine::render
{

    resource::Texture2D FullScreenPass::Execute(VkPipelineCache cache, const FullScreenPassConfig& config)
    {
		auto& device = core::Device::Instance();

		const VkFormat format = config.outputFormat;
		const int32_t dim = 512;

        resource::Texture2D texture;        

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
		imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		SUCCESS_OR_LOG(
            vkCreateImage(device.GetLogicalDeviceHandle(), &imageCI, nullptr, &texture.image_) == VK_SUCCESS,
            "FullScreenPass: Failed to create image."
        );

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), texture.image_, &memReqs);

		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		SUCCESS_OR_LOG(
            vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &texture.deviceMemory_) == VK_SUCCESS,
            "FullScreenPass: Failed to allocate memory."
        );

		SUCCESS_OR_LOG(
            vkBindImageMemory(device.GetLogicalDeviceHandle(), texture.image_, texture.deviceMemory_, 0) == VK_SUCCESS,
            "FullScreenPass: Failed to bind image memory."
        );

		// View
		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = format;
		viewCI.subresourceRange = {};
		viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCI.subresourceRange.levelCount = 1;
		viewCI.subresourceRange.layerCount = 1;
		viewCI.image = texture.image_;

		SUCCESS_OR_LOG(
            vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCI, nullptr, &texture.view_) == VK_SUCCESS,
            "FullScreenPass: Failed to create image view."
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
		samplerCI.maxLod = 1.0f;
		samplerCI.maxAnisotropy = 1.0f;
		samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

		SUCCESS_OR_LOG(
            vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerCI, nullptr, &texture.sampler_) == VK_SUCCESS,
            "FullScreenPass: Failed to create sampler."
        );

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
		attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

		// Create the actual renderpass
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
            "FullScreenPass: Failed to create render pass."
        );

		VkFramebufferCreateInfo framebufferCI{};
		framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCI.renderPass = renderpass;
		framebufferCI.attachmentCount = 1;
		framebufferCI.pAttachments = &texture.view_;
		framebufferCI.width = dim;
		framebufferCI.height = dim;
		framebufferCI.layers = 1;

		VkFramebuffer framebuffer;

		SUCCESS_OR_LOG(
            vkCreateFramebuffer(device.GetLogicalDeviceHandle(), &framebufferCI, nullptr, &framebuffer) == VK_SUCCESS,
            "FullScreenPass: Failed to create frame buffer."
        );

		// Descriptor pool
		VkDescriptorPool descriptorPool;
		VkDescriptorPoolCreateInfo descriptorPoolCI{};

		// Descriptors layout
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};

		// Descriptor set
		VkDescriptorSet descriptorSet;

		// Pipeline layout
		VkPipelineLayout pipelineLayout;
		VkPipelineLayoutCreateInfo pipelineLayoutCI{};

		if (config.inputTexture != nullptr)
		{
			//传入 texture Eu
			std::vector<VkDescriptorPoolSize> poolSize = {
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
			};
			
			descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSize.size());
			descriptorPoolCI.pPoolSizes = poolSize.data();
			descriptorPoolCI.maxSets = 1;
			SUCCESS_OR_LOG(
				vkCreateDescriptorPool(device.GetLogicalDeviceHandle(), &descriptorPoolCI, nullptr, &descriptorPool) == VK_SUCCESS,
				"FullScreenPass: Failed to create descriptor pool."
			);

			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
			};

			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			SUCCESS_OR_LOG(
				vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &descriptorSetLayout) == VK_SUCCESS,
				"FullScreenPass: Failed to create descriptor set layout."
			);


			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			SUCCESS_OR_LOG(
				vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &descriptorSet) == VK_SUCCESS,
				"FullScreenPass: Failed to allocate descriptor sets."
			); 

			std::array<VkWriteDescriptorSet, 1> writeDescriptorSets{};
			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = descriptorSet;
			writeDescriptorSets[0].dstBinding = static_cast<uint32_t>(0);
			writeDescriptorSets[0].pImageInfo = &config.inputTexture->descriptor_;

			vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), static_cast<uint32_t>(1), writeDescriptorSets.data(), 0, nullptr);

			pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCI.setLayoutCount = 1;
			pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
		}
		else
		{
			
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

			SUCCESS_OR_LOG(
				vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &descriptorSetLayout) == VK_SUCCESS,
				"FullScreenPass: Failed to create descriptor set layout."
			);

			
			pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutCI.setLayoutCount = 1;
			pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
		}

		SUCCESS_OR_LOG(
			vkCreatePipelineLayout(device.GetLogicalDeviceHandle(), &pipelineLayoutCI, nullptr, &pipelineLayout) == VK_SUCCESS,
			"FullScreenPass: Failed to create pipeline layout."
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
		
		VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
		emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = pipelineLayout;
		pipelineCI.renderPass = renderpass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &emptyInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = 2;
		pipelineCI.pStages = shaderStages.data();

		// Look-up-table (from BRDF) pipeline		
		shaderStages = {
			core::Loader::LoadShader(device.GetLogicalDeviceHandle(), config.vertShader, VK_SHADER_STAGE_VERTEX_BIT),
			core::Loader::LoadShader(device.GetLogicalDeviceHandle(), config.fragShader, VK_SHADER_STAGE_FRAGMENT_BIT)
		};
		VkPipeline pipeline;

		SUCCESS_OR_LOG(
            vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), cache, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
            "FullScreenPass: Failed to create graphics pipelines."
        );

		for (auto shaderStage : shaderStages) {
			vkDestroyShaderModule(device.GetLogicalDeviceHandle(), shaderStage.module, nullptr);
		}

		// Render
		VkClearValue clearValues[1];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderpass;
		renderPassBeginInfo.renderArea.extent.width = dim;
		renderPassBeginInfo.renderArea.extent.height = dim;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = framebuffer;

		VkCommandBuffer cmdBuf = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width = (float)dim;
		viewport.height = (float)dim;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.extent.width = dim;
		scissor.extent.height = dim;

		vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
		vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
		vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// 渲染部分 — 缺了这个
		if (config.inputTexture != nullptr) 
		{
  			vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
    	    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		}

		vkCmdDraw(cmdBuf, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmdBuf);
		device.FlushCommandBuffer(cmdBuf, true);

		vkQueueWaitIdle(core::Device::Instance().GetGraphicsQueue());

		vkDestroyPipeline(device.GetLogicalDeviceHandle(), pipeline, nullptr);
		vkDestroyPipelineLayout(device.GetLogicalDeviceHandle(), pipelineLayout, nullptr);
		vkDestroyRenderPass(device.GetLogicalDeviceHandle(), renderpass, nullptr);
		vkDestroyFramebuffer(device.GetLogicalDeviceHandle(), framebuffer, nullptr);

		// 函数末尾清理（目前你写了 vkDestroyPipeline / vkDestroyPipelineLayout / ...）

		if (config.inputTexture != nullptr) 
		{
  		  	vkDestroyDescriptorPool(device.GetLogicalDeviceHandle(), descriptorPool, nullptr);
    		// descriptorSet 不需要单独销毁——pool 销毁时自动释放从它分配的 set
		}

		vkDestroyDescriptorSetLayout(device.GetLogicalDeviceHandle(), descriptorSetLayout, nullptr);

		texture.descriptor_.imageView = texture.view_;
		texture.descriptor_.sampler = texture.sampler_;
		texture.descriptor_.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		return texture;
    }

}






