#include "engine/render/renderer.h"
#include "engine/render/config/shader_protocol.h"
#include "engine/render/render_context.h"
#include "engine/utils/log.h"
#include "engine/resource/resource_manager.h"
#include "engine/core/loader.h"
#include "engine/core/buffer.h"
#include "engine/core/staging_ring_allocator.h"
#include "engine/render/pipeline_cache_file.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace engine::render
{

	// 每帧 timestamp 查询槽位数：[0]=FrameStart + 每个 Pass 2 个（开始/结束），预留 8 个 Pass 的容量
	constexpr uint32_t kGpuQueryCount = 1 + 8 * 2;

	namespace
	{
		std::string_view GetResourceUsageName(ResourceUsage usage) noexcept
		{
			switch (usage)
			{
				case ResourceUsage::UniformBuffer:   return "UniformBuffer";
				case ResourceUsage::StorageBuffer:   return "StorageBuffer";
				case ResourceUsage::SampledImage:    return "SampledImage";
				case ResourceUsage::ColorAttachment: return "ColorAttachment";
				case ResourceUsage::DepthAttachment: return "DepthAttachment";
			}

			return "Unknown";
		}

		bool IsUsageCompatible(const RenderResourceDescription& resource, ResourceUsage usage) noexcept
		{
			switch (usage)
			{
				case ResourceUsage::UniformBuffer:
				case ResourceUsage::StorageBuffer:
					return std::holds_alternative<RenderBufferDescription>(resource.description_);

				case ResourceUsage::SampledImage:
					return std::holds_alternative<RenderImageDescription>(resource.description_) ||
						std::holds_alternative<EnvironmentDescription>(resource.description_);

				case ResourceUsage::ColorAttachment:
				case ResourceUsage::DepthAttachment:
					return std::holds_alternative<RenderImageDescription>(resource.description_);
			}

			return false;
		}

		bool IsLayoutCompatible(VkImageLayout layout, ResourceUsage usage) noexcept
		{
			switch (usage)
			{
				case ResourceUsage::UniformBuffer:
				case ResourceUsage::StorageBuffer:
					return layout == VK_IMAGE_LAYOUT_UNDEFINED;

				case ResourceUsage::SampledImage:
					return layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || layout == VK_IMAGE_LAYOUT_GENERAL;

				case ResourceUsage::ColorAttachment:
					return layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || layout == VK_IMAGE_LAYOUT_GENERAL;

				case ResourceUsage::DepthAttachment:
					return layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || layout == VK_IMAGE_LAYOUT_GENERAL;
			}

			return false;
		}

		VkPipelineStageFlags GetPipelineStage(const PassResourceUsage& usage) noexcept
		{
			switch (usage.type_)
			{
				case ResourceUsage::ColorAttachment:
					return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				case ResourceUsage::DepthAttachment:
					return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				case ResourceUsage::UniformBuffer:
				case ResourceUsage::StorageBuffer:
				case ResourceUsage::SampledImage:
					return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}

			return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		}

		VkAccessFlags GetAccessMask(const PassResourceUsage& usage, bool output) noexcept
		{
			switch (usage.type_)
			{
				case ResourceUsage::ColorAttachment:
					return output ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				case ResourceUsage::DepthAttachment:
					return output ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
				case ResourceUsage::UniformBuffer:
					return VK_ACCESS_UNIFORM_READ_BIT;
				case ResourceUsage::SampledImage:
					return VK_ACCESS_SHADER_READ_BIT;
				case ResourceUsage::StorageBuffer:
					return output ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
			}

			return 0;
		}

	}

    Renderer::Renderer()
    {
		this->frameCount_ = core::Device::Instance().GetSetting().frameCount_;
    }

    Renderer::Renderer(const RendererDescription& description) : rendererDescription_(description)
    {
		this->frameCount_ = core::Device::Instance().GetSetting().frameCount_;
    }

    void Renderer::PrepareFrame(const RenderContext& renderContext)
    {
        this->renderContext_ = &renderContext;
        if (!this->ValidatePassResourceDeclarations())
        {
            LOG_FATAL("Renderer: pass resource declaration validation failed.");
        }

        this->CreateResources();
        const FrameGraph& frameGraph = this->renderContext_->GetFrameGraph();
        for (const CompiledPass& compiledPass : frameGraph.GetExecutionPlan().passes_)
        {
            const FrameGraphPassNode& node = frameGraph.GetNode(compiledPass.nodeId_);
            RenderPass* renderPass = node.renderPass_;
            renderPass->Prepare(compiledPass, this->renderResources_, this->swapChain_);
        }
        this->PrepareUI();
        LOG_INFO("Renderer: resources and render passes prepared.");
    }

    void Renderer::InitSwapChain(engine::platform::Window& window)
    {
		LOG_INFO("Renderer: start to init swap chain...");
        this->swapChain_.Connect(core::Device::Instance().GetInstanceHandle(), core::Device::Instance().GetPhysicalDeviceHandle(), core::Device::Instance().GetLogicalDeviceHandle());
        this->swapChain_.CreateSurface(window.NativeInstance(), window.NativeHandle());
        this->swapChain_.CreateSwapChain(&window.windowDescription_.width_, &window.windowDescription_.height_, this->rendererDescription_.vsync_);
    }

    void Renderer::Init(engine::platform::Window& window)
    {
		LOG_INFO("Renderer: start to init renderer...");
		// Device 是 MSAA 配置的唯一权威源，Renderer 在初始化时同步一次，避免两处配置不一致
		this->rendererDescription_.multiSampling_ = core::Device::Instance().GetSetting().multiSampling_;
		this->InitSwapChain(window);

		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(core::Device::Instance().GetPhysicalDeviceHandle(), &props);
		this->timestampPeriod_ = props.limits.timestampPeriod;  // 每个 tick 的纳秒数
		this->timestampQuerySupported_ = (props.limits.timestampComputeAndGraphics != 0);
		if (!this->timestampQuerySupported_) 
		{
    		LOG_WARN("Renderer: GPU does not support timestamp queries");
		}

        this->InitCommandPool();
        PipelineCache::Instance().Init();
        this->CreateSyncObjects();
		this->CreateFrameContexts();
		this->renderScenes_.resize(this->frameCount_);
    }


	void Renderer::SetRenderScene(const RenderScene& renderScene)
	{
		// 存入当前 frame-in-flight 的槽（与 UBO 双缓冲同步）
		this->renderScenes_[this->frameIndex_] = renderScene;
		this->UpdateFrameUniformData();

        const FrameGraph& graph = this->renderContext_->GetFrameGraph();
        for (const CompiledPass& compiledPass : graph.GetExecutionPlan().passes_)
        {
            RenderPass* pass = graph.GetNode(compiledPass.nodeId_).renderPass_;
            const auto inputs = pass->GetInputResources();
            // 这类图像随 acquire 结果选择实例，需要更新当前帧 Set 的资源指向。
            const bool usesPerImageResource = std::any_of(inputs.begin(), inputs.end(), [](const auto& input) {
                const auto* resource = FindRenderResourceDescription(input.resource_.id_);
                const auto* image = resource ? std::get_if<RenderImageDescription>(&resource->description_) : nullptr;
                return image && image->instancePolicy_ == RenderImageInstancePolicy::PerSwapchainImage;
            });
            if (usesPerImageResource)
                pass->UpdateDescriptorSets(this->frameIndex_);
        }
	}

	uint32_t Renderer::GetFrameIndex() const
	{
		return this->frameIndex_;
	}

    void Renderer::CreateResources()
    {
        const FrameGraph& graph = this->renderContext_->GetFrameGraph();
        // 逐个检查资源注册表并收集所有 Pass 的输入输出，累积 usage，创建内部 Buffer / Image
        for (const auto& resource : this->renderContext_->GetResourceDescriptions())
        {
            if (resource.lifetime_ == RenderResourceLifetime::External)
            {
                continue;
            }

            VkBufferUsageFlags bufferUsage = 0;
            VkImageUsageFlags imageUsage = 0;
            for (const CompiledPass& compiledPass : graph.GetExecutionPlan().passes_)
            {
                RenderPass* pass = graph.GetNode(compiledPass.nodeId_).renderPass_;
                const auto accumulateUsage = [&](const auto resources)
                {
                    for (const auto& passResource : resources)
                    {
                        if (passResource.resource_.id_ != resource.id_)
                        {
                            continue;
                        }

                        switch (passResource.usage_.type_)
                        {
                            case ResourceUsage::UniformBuffer: bufferUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
                            case ResourceUsage::StorageBuffer: bufferUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
                            case ResourceUsage::SampledImage: imageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT; break;
                            case ResourceUsage::ColorAttachment: imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
                            case ResourceUsage::DepthAttachment: imageUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;
                        }
                    }
                };
                accumulateUsage(pass->GetInputResources());
                accumulateUsage(pass->GetOutputResources());
            }

            if (std::holds_alternative<RenderBufferDescription>(resource.description_) && bufferUsage != 0)
            {
                // 窗口重建只替换图像；保留已被 Pass descriptor 引用的 Buffer。
                if (!this->renderResources_.HasBuffers(resource.id_))
                {
                    this->CreateBuffer(resource, bufferUsage);
                }
            }
            else if (std::holds_alternative<RenderImageDescription>(resource.description_) && imageUsage != 0)
            {
                auto imageResource = resource;
                auto& description = std::get<RenderImageDescription>(imageResource.description_);
                const VkSampleCountFlagBits samples = this->rendererDescription_.multiSampling_
                    ? core::Device::Instance().GetMultiSampleCount() : VK_SAMPLE_COUNT_1_BIT;

                // 当前输出所需的设备参数在准备时填入，CreateImage 只消费具体值。
                switch (resource.id_)
                {
                    case RenderResourceId::MainDepth:
                        description.format_ = this->swapChain_.GetDepthFormat();
                        description.samples_ = samples;
                        break;
                    case RenderResourceId::MainColorMsaa:
                        description.format_ = this->swapChain_.GetColorFormat();
                        description.samples_ = samples;
                        break;
                    case RenderResourceId::MainDepthSingleSample:
                        description.format_ = this->swapChain_.GetDepthFormat();
                        break;
                    default:
                        break;
                }

                if ((imageUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
                    this->renderResources_.CreateDefaultSampler();
                this->CreateImage(imageResource, imageUsage);
            }
        }
    }

    void Renderer::CreateBuffer(const RenderResourceDescription& resource, VkBufferUsageFlags usage)
    {
        auto& device = core::Device::Instance();
        const auto& description = std::get<RenderBufferDescription>(resource.description_);
        auto& buffers = this->renderResources_.GetOrCreateBuffers(resource.id_);
        const uint32_t instanceCount = resource.lifetime_ == RenderResourceLifetime::Frame ? this->frameCount_ : 1;
        buffers.resize(instanceCount);

        // 当前内部 Buffer 接收 CPU 上传；Model 的设备侧 Buffer 由外部提供。
        for (uint32_t i = 0; i < instanceCount; ++i)
        {
            buffers[i].Create(
                usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                description.size_
            );
            const std::string name = std::string(resource.name_) + "[" + std::to_string(i) + "]";
            device.SetObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffers[i].buffer), name.c_str());
        }

        LOG_INFO("Renderer: created " << resource.name_ << " buffers, instances=" << instanceCount
            << ", bytes=" << description.size_);
    }

    void Renderer::InitCommandPool()
    {
		LOG_INFO("Renderer: start to init command pool...");
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = this->swapChain_.GetQueueNodeIndex();
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        SUCCESS_OR_LOG(
            (vkCreateCommandPool(core::Device::Instance().GetLogicalDeviceHandle(), &cmdPoolInfo, nullptr, &this->commandPool_) == VK_SUCCESS),
            "Failed to create command pool");

		core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(this->commandPool_), "Renderer Command Pool");

    }

	bool Renderer::ValidatePassResourceDeclarations() const
	{
		const FrameGraph& frameGraph = this->renderContext_->GetFrameGraph();
		const FrameGraphExecutionPlan& executionPlan = frameGraph.GetExecutionPlan();
		if (frameGraph.NeedsRebuild() || !executionPlan.valid_)
		{
			LOG_ERROR("Renderer: Context must provide a rebuilt, valid FrameGraph.");
			return false;
		}

		bool valid = true;
		uint32_t requestCount = 0;
		const auto descriptions = this->renderContext_->GetResourceDescriptions();

		for (const CompiledPass& compiledPass : executionPlan.passes_)
		{
			const RenderPass* renderPass = frameGraph.GetNode(compiledPass.nodeId_).renderPass_;
			if (renderPass == nullptr)
			{
				LOG_ERROR("Renderer: FrameGraph node does not resolve to a Context RenderPass.");
				valid = false;
				continue;
			}

			const auto validateResources = [&](const auto resources)
			{
				requestCount += static_cast<uint32_t>(resources.size());
				for (size_t i = 0; i < resources.size(); ++i)
				{
					const auto& passResource = resources[i];
					const PassResourceUsage usage = passResource.usage_;
					const auto& reference = passResource.resource_;
					const auto descriptionIt = std::find_if(descriptions.begin(), descriptions.end(), [&reference](const auto& entry){
							return entry.id_ == reference.id_;
						}
					);
					const RenderResourceDescription* description = descriptionIt != descriptions.end() ? &*descriptionIt : nullptr;

					for (size_t j = 0; j < i; ++j)
					{
						const auto& previous = resources[j].resource_;
						if (previous.id_ == reference.id_ &&
							previous.environmentTexture_ == reference.environmentTexture_)
						{
							LOG_ERROR(
								"Pass resource declaration: " << renderPass->GetName()
								<< " declares " << (description ? description->name_ : "an unknown resource")
								<< " more than once in the same input/output list."
							);
							valid = false;
							break;
						}
					}

					if (description == nullptr)
					{
						LOG_ERROR(
							"Pass resource declaration: " << renderPass->GetName()
							<< " uses unregistered resource ID " << static_cast<uint32_t>(reference.id_) << "."
						);
						valid = false;
						continue;
					}

					const auto member = reference.environmentTexture_;
					const bool validMember = member == EnvironmentTexture::Source ||
						(std::holds_alternative<EnvironmentDescription>(description->description_) &&
							(member == EnvironmentTexture::Irradiance || member == EnvironmentTexture::Prefiltered));
					if (!validMember)
					{
						LOG_ERROR("Pass resource declaration: " << renderPass->GetName()
							<< " selects an invalid texture member for " << description->name_ << ".");
						valid = false;
					}

					if (!IsUsageCompatible(*description, usage.type_))
					{
						LOG_ERROR(
							"Pass resource declaration: " << renderPass->GetName()
							<< " uses " << description->name_ << " as " << GetResourceUsageName(usage.type_)
							<< ", but the registered resource type is incompatible."
						);
						valid = false;
					}

					if (!IsLayoutCompatible(usage.requiredLayout_, usage.type_))
					{
						LOG_ERROR(
							"Pass resource declaration: " << renderPass->GetName()
							<< " has incompatible image layout " << static_cast<int32_t>(usage.requiredLayout_)
							<< " for " << description->name_ << " used as " << GetResourceUsageName(usage.type_) << "."
						);
						valid = false;
					}
				}
			};
			validateResources(renderPass->GetInputResources());
			validateResources(renderPass->GetOutputResources());
		}

		if (valid)
		{
			LOG_INFO(
				"Renderer: validated " << requestCount << " resource declarations across "
				<< executionPlan.passes_.size() << " passes."
			);
		}

		return valid;
	}

    void Renderer::CreateFrameContexts()
    {
		LOG_INFO("Renderer: start to create frame contexts...");
        this->frameContexts_.resize(this->frameCount_);

		for (uint32_t i = 0; i < this->frameCount_; ++i)
		{
			VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
			cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cmdBufAllocateInfo.commandPool = this->commandPool_;
			cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cmdBufAllocateInfo.commandBufferCount = 1;
			SUCCESS_OR_LOG(
				vkAllocateCommandBuffers(core::Device::Instance().GetLogicalDeviceHandle(), &cmdBufAllocateInfo, &frameContexts_[i].commandBuffer_) == VK_SUCCESS,
				"Failed to allocate command buffer"
			);
			core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(frameContexts_[i].commandBuffer_), ("FrameContext[" + std::to_string(i) + "]: CommandBuffer").c_str());

			VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
			SUCCESS_OR_LOG(
				vkCreateFence(core::Device::Instance().GetLogicalDeviceHandle(), &fenceCI, nullptr, &frameContexts_[i].inFlightFence_) == VK_SUCCESS,
				"Failed to create fence"
			);
			core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(frameContexts_[i].inFlightFence_), ("FrameContext[" + std::to_string(i) + "]: InFlightFence").c_str());

			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
			SUCCESS_OR_LOG(
				vkCreateSemaphore(core::Device::Instance().GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &frameContexts_[i].imageAvailableSemaphore_) == VK_SUCCESS,
				"Failed to create image available semaphore"
			);
			core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(frameContexts_[i].imageAvailableSemaphore_), ("FrameContext[" + std::to_string(i) + "]: ImageAvailableSemaphore").c_str());

			if (this->timestampQuerySupported_)
			{
				VkQueryPoolCreateInfo queryPoolCI{};
				queryPoolCI.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
				queryPoolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
				queryPoolCI.queryCount = kGpuQueryCount;
				SUCCESS_OR_LOG(
					vkCreateQueryPool(core::Device::Instance().GetLogicalDeviceHandle(), &queryPoolCI, nullptr, &frameContexts_[i].queryPool_) == VK_SUCCESS,
					"Failed to create query pool"
				);
				core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<uint64_t>(frameContexts_[i].queryPool_), ("FrameContext[" + std::to_string(i) + "]: QueryPool").c_str());
			}
		}
    }

    void Renderer::CreateSyncObjects()
    {
		LOG_INFO("Renderer: start to create sync objects...");
		this->renderFinishedSemaphores_.resize(this->swapChain_.GetImageCount());

		for (uint32_t i = 0; i < this->swapChain_.GetImageCount(); ++i)
		{
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            SUCCESS_OR_LOG(
                vkCreateSemaphore(core::Device::Instance().GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &this->renderFinishedSemaphores_[i]) == VK_SUCCESS,
                "Failed to create render complete semaphore"
			);
			core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(this->renderFinishedSemaphores_[i]), ("RenderFinishedSemaphore[" + std::to_string(i) + "]").c_str());
		}
    }
    
    void Renderer::CreateImage(const RenderResourceDescription& resource, VkImageUsageFlags usage)
    {
        auto& device = core::Device::Instance();
        const auto& description = std::get<RenderImageDescription>(resource.description_);
        const VkExtent2D extent = this->swapChain_.GetExtent();

        VkImageCreateInfo imageCI{};
        imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.flags = description.viewType_ == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        imageCI.format = description.format_;
        imageCI.extent = {extent.width, extent.height, 1};
        imageCI.mipLevels = description.mipLevels_;
        imageCI.arrayLayers = description.arrayLayers_;
        imageCI.samples = description.samples_;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCI.usage = usage;
        imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // aspect 属于图像格式；深度图即使只用于采样，也不能创建 color view。
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        switch (imageCI.format)
        {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D32_SFLOAT:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                break;
            case VK_FORMAT_D16_UNORM_S8_UINT:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                break;
            case VK_FORMAT_S8_UINT:
                aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
                break;
            default:
                break;
        }

        auto& images = this->renderResources_.GetOrCreateImages(resource.id_);
        const uint32_t instanceCount = description.instancePolicy_ == RenderImageInstancePolicy::PerSwapchainImage
            ? this->swapChain_.GetImageCount() : 1;
        images.resize(instanceCount);
        for (uint32_t i = 0; i < images.size(); ++i)
        {
            auto& image = images[i];
            SUCCESS_OR_LOG(
                vkCreateImage(device.GetLogicalDeviceHandle(), &imageCI, nullptr, &image.image_) == VK_SUCCESS,
                "Failed to create render image"
            );

            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), image.image_, &memoryRequirements);
            VkMemoryAllocateInfo memoryInfo{};
            memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memoryInfo.allocationSize = memoryRequirements.size;
            memoryInfo.memoryTypeIndex = device.GetMemoryType(
                memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
            SUCCESS_OR_LOG(
                vkAllocateMemory(device.GetLogicalDeviceHandle(), &memoryInfo, nullptr, &image.memory_) == VK_SUCCESS,
                "Failed to allocate render image memory"
            );

            SUCCESS_OR_LOG(
                vkBindImageMemory(device.GetLogicalDeviceHandle(), image.image_, image.memory_, 0) == VK_SUCCESS,
                "Failed to bind render image memory"
            );

            VkImageViewCreateInfo viewCI{};
            viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCI.image = image.image_;
            viewCI.viewType = description.viewType_;
            viewCI.format = imageCI.format;
            viewCI.subresourceRange = {aspect, 0, imageCI.mipLevels, 0, imageCI.arrayLayers};
            SUCCESS_OR_LOG(
                vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCI, nullptr, &image.imageView_) == VK_SUCCESS,
                "Failed to create render image view"
            );

            const std::string name = std::string(resource.name_) + "[" + std::to_string(i) + "]";
            device.SetObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image.image_), name.c_str());
            device.SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(image.imageView_), (name + " View").c_str());
        }
        LOG_INFO("Renderer: created " << resource.name_ << " images, instances=" << images.size()
            << ", samples=" << imageCI.samples);
    }

    void Renderer::PrepareUI()
    {
        auto& device = core::Device::Instance();

        VkAttachmentDescription attachment{};
        attachment.format = this->swapChain_.GetColorFormat();
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        const VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = 1;
        renderPassCI.pAttachments = &attachment;
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpass;
        renderPassCI.dependencyCount = 1;
        renderPassCI.pDependencies = &dependency;

        SUCCESS_OR_LOG(
            vkCreateRenderPass(device.GetLogicalDeviceHandle(), &renderPassCI, nullptr, &this->uiRenderPass_) == VK_SUCCESS,
            "Renderer: failed to create UI render pass."
        );
        device.SetObjectName(
            VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<uint64_t>(this->uiRenderPass_), "UI RenderPass"
        );

        this->CreateUIFramebuffers();
    }

    void Renderer::CreateUIFramebuffers()
    {
        auto& device = core::Device::Instance();
        const VkExtent2D extent = this->swapChain_.GetExtent();
        this->uiFramebuffers_.resize(this->swapChain_.GetImageCount(), VK_NULL_HANDLE);

        for (uint32_t i = 0; i < this->uiFramebuffers_.size(); ++i)
        {
            VkImageView view = this->swapChain_.GetSwapChainBuffer(i).view;

            VkFramebufferCreateInfo framebufferCI{};
            framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCI.renderPass = this->uiRenderPass_;
            framebufferCI.attachmentCount = 1;
            framebufferCI.pAttachments = &view;
            framebufferCI.width = extent.width;
            framebufferCI.height = extent.height;
            framebufferCI.layers = 1;

            SUCCESS_OR_LOG(
                vkCreateFramebuffer(device.GetLogicalDeviceHandle(), &framebufferCI, nullptr, &this->uiFramebuffers_[i]) == VK_SUCCESS,
                "Renderer: failed to create UI framebuffer."
            );
            const std::string name = "UI Framebuffer[" + std::to_string(i) + "]";
            device.SetObjectName(
                VK_OBJECT_TYPE_FRAMEBUFFER, reinterpret_cast<uint64_t>(this->uiFramebuffers_[i]), name.c_str()
            );
        }
    }

    void Renderer::DestroyUIFramebuffers()
    {
        for (auto& framebuffer : this->uiFramebuffers_)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(core::Device::Instance().GetLogicalDeviceHandle(), framebuffer, nullptr);
                framebuffer = VK_NULL_HANDLE;
            }
        }
        this->uiFramebuffers_.clear();
    }

    void Renderer::RecreateSyncObjects()
    {
        auto& device = core::Device::Instance();
        for (auto& sem : this->renderFinishedSemaphores_)
        {
            if (sem != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device.GetLogicalDeviceHandle(), sem, nullptr);
            }
        }

        this->renderFinishedSemaphores_.resize(this->swapChain_.GetImageCount());

		for (uint32_t i = 0; i < this->renderFinishedSemaphores_.size(); ++i)
		{
            VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            SUCCESS_OR_LOG(
                vkCreateSemaphore(device.GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &this->renderFinishedSemaphores_[i]) == VK_SUCCESS,
                "Failed to create render complete semaphore"
            );
            core::Device::Instance().SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(this->renderFinishedSemaphores_[i]), ("RenderFinishedSemaphore[" + std::to_string(i) + "]").c_str());
        }
    }

    void Renderer::RequestResize(uint32_t width, uint32_t height, bool force)
    {
        this->resizePending_ = true;
        this->forceResize_ = this->forceResize_ || force;
        this->pendingWidth_ = width;
        this->pendingHeight_ = height;
    }

    bool Renderer::RecreateSwapChain(uint32_t width, uint32_t height)
    {
		LOG_INFO("Renderer: start to recreate swap chain...");
        if (width == 0 || height == 0)
        {
            return false;
        }

        auto& device = core::Device::Instance();
        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());

        this->DestroyUIFramebuffers();
        const FrameGraphExecutionPlan& executionPlan = this->renderContext_->GetFrameGraph().GetExecutionPlan();
        for (const CompiledPass& compiledPass : executionPlan.passes_)
        {
            RenderPass* renderPass = this->renderContext_->GetFrameGraph().GetNode(compiledPass.nodeId_).renderPass_;
            renderPass->DestroyFramebuffers();
        }
        this->renderResources_.ClearInternalImages();

        // Recreate swapchain (internally destroys old swapchain + image views)
        this->swapChain_.CreateSwapChain(&width, &height, this->rendererDescription_.vsync_);

        // Image count may have changed -> recreate semaphores sized by image count
        this->RecreateSyncObjects();

        this->CreateResources();
        for (const CompiledPass& compiledPass : executionPlan.passes_)
        {
            RenderPass* renderPass = this->renderContext_->GetFrameGraph().GetNode(compiledPass.nodeId_).renderPass_;
            renderPass->RecreateFramebuffers(this->renderResources_, this->swapChain_);
        }
        this->CreateUIFramebuffers();

        this->imageIndex_ = 0;
        this->currentCB_ = VK_NULL_HANDLE;
        return true;
    }

	bool Renderer::BeginFrame(uint32_t windowWidth, uint32_t windowHeight)
	{
		auto& device = core::Device::Instance();

		if (windowWidth == 0 || windowHeight == 0)
		{
			return false;
		}

		if (this->forceResize_)
		{
			this->pendingWidth_ = windowWidth;
			this->pendingHeight_ = windowHeight;
			this->resizePending_ = true;
		}

		if (this->resizePending_)
		{
			const VkExtent2D oldExtent = this->swapChain_.GetExtent();
			const bool sizeChanged = oldExtent.width != this->pendingWidth_ ||
				oldExtent.height != this->pendingHeight_;
			const bool shouldRecreate = this->forceResize_ || sizeChanged;
			const uint32_t requestedWidth = this->pendingWidth_;
			const uint32_t requestedHeight = this->pendingHeight_;

			this->resizePending_ = false;
			this->forceResize_ = false;

			if (shouldRecreate)
			{
				this->RecreateSwapChain(requestedWidth, requestedHeight);
				return false;
			}
		}

		auto& frameContext = this->frameContexts_[this->frameIndex_];

		VkExtent2D extent = this->swapChain_.GetExtent();
		
		SUCCESS_OR_LOG(
			vkWaitForFences(device.GetLogicalDeviceHandle(), 1, &frameContext.inFlightFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS,
			"Renderer: Failed to wait for fences."
		);

		// Fence 已确认该 FrameContext 上一轮提交的 GPU 工作全部完成：
		// 1) 此时可安全读取其 timestamp 结果
		// 2) 通知资源管理器推进帧号，清理到期的延迟删除资源（GPU 已不再使用它们）
		resource::ResourceManager::Instance().OnFrameCompleted();

		// 每帧回收 staging ring 已完成的上传（空闲期也还账，回绕等待窗口最小化）
		core::StagingRingAllocator::Instance().Tick();

		// 第一帧（或该 FrameContext 尚未提交过）时 query 从未被 reset，必须跳过读取
		if (this->timestampQuerySupported_ && frameContext.hasSubmittedFrame_)
		{
			// 只读取实际写入的 query（FrameStart 1 个 + 每个 Pass 2 个），不要读取预留但未写入的槽位
			const auto passCount = this->renderContext_->GetFrameGraph().GetExecutionPlan().passes_.size();
			const uint32_t queryCountToRead = std::min<uint32_t>(kGpuQueryCount, 1 + static_cast<uint32_t>(passCount) * 2);
			uint64_t timestamps[kGpuQueryCount] = {};
			VkResult queryResult = vkGetQueryPoolResults(
				device.GetLogicalDeviceHandle(),
				frameContext.queryPool_,
				0,
				queryCountToRead,
				sizeof(timestamps),
				timestamps,
				sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT
			);

			// 布局：[0]=FrameStart [1]=Pass0开始 [2]=Pass0结束 [3]=Pass1开始 [4]=Pass1结束 ...
			// 帧总耗时 = 最后一个 Pass 结束 - 帧开始；单个 Pass 耗时 = 结束 - 开始
			if (queryResult == VK_SUCCESS && timestamps[queryCountToRead - 1] > timestamps[0])
			{
				const float periodNs = this->timestampPeriod_;
				const uint64_t frameEnd = timestamps[queryCountToRead - 1];
				this->lastGpuTimings_.frameTotalMs = static_cast<float>(frameEnd - timestamps[0]) * periodNs / 1000000.0f;
				this->lastGpuTimings_.skyboxMs    = static_cast<float>(timestamps[2] - timestamps[1]) * periodNs / 1000000.0f;
				this->lastGpuTimings_.pbrMs       = static_cast<float>(timestamps[4] - timestamps[3]) * periodNs / 1000000.0f;
				this->lastGpuTimings_.valid       = true;
			}
		}

		VkResult acquire = this->swapChain_.AcquireNextImage(frameContext.imageAvailableSemaphore_, &this->imageIndex_);
		if ((acquire == VK_ERROR_OUT_OF_DATE_KHR))
		{
			this->RequestResize(windowWidth, windowHeight, true);
			return false;
		}
		else if (acquire == VK_SUBOPTIMAL_KHR)
		{
			this->RequestResize(windowWidth, windowHeight, true);
			LOG_WARN("Renderer: Swap chain is suboptimal. Recreating swap chain.");
		}
		else if (acquire != VK_SUCCESS)
		{
			LOG_ERROR("Renderer: Failed to acquire swap chain image, VkResult: " << acquire);
			return false;
		}

		SUCCESS_OR_LOG(
			vkResetFences(device.GetLogicalDeviceHandle(), 1, &frameContext.inFlightFence_) == VK_SUCCESS,
			"Renderer: Failed to reset fences."
		);

        this->renderResources_.SetCurrentImageIndex(this->imageIndex_);


		vkResetCommandBuffer(frameContext.commandBuffer_, 0);

		VkCommandBufferBeginInfo cmdBufferBeginInfo{};
		cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		this->currentCB_ = frameContext.commandBuffer_;

		vkBeginCommandBuffer(this->currentCB_, &cmdBufferBeginInfo);

		// vkCmdResetQueryPool 禁止在 render pass 内部调用（VUID-vkCmdResetQueryPool-renderpass），必须放在 BeginRenderPass 之前
		if (this->timestampQuerySupported_)
		{
			vkCmdResetQueryPool(this->currentCB_, frameContext.queryPool_, 0, kGpuQueryCount);
		}

		static auto beginLabel = core::Device::Instance().GetCmdBeginDebugUtilsLabel();

		if (beginLabel) 
		{
			VkDebugUtilsLabelEXT labelInfo{};
			labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
			labelInfo.pLabelName = "Renderer Begin Frame";
			labelInfo.color[0] = 1.0f;
			labelInfo.color[1] = 1.0f;
			labelInfo.color[2] = 1.0f;
			labelInfo.color[3] = 1.0f;
			beginLabel(this->currentCB_, &labelInfo);
		}

		// 记录帧起点（vkCmdWriteTimestamp 允许在 render pass 内部使用）
		if (this->timestampQuerySupported_)
		{
			vkCmdWriteTimestamp(this->currentCB_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.queryPool_, 0);
		}

		VkViewport viewport{};
		viewport.width = (float)extent.width;
		viewport.height = (float)extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(this->currentCB_, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.extent = { extent.width, extent.height };
		vkCmdSetScissor(this->currentCB_, 0, 1, &scissor);

		return true;
	}

	void Renderer::Render()
	{
		if (this->renderContext_ == nullptr)
		{
			LOG_ERROR("Renderer: PrepareFrame must provide a RenderContext before rendering.");
			return;
		}

		auto& frameContext = this->frameContexts_[this->frameIndex_];
		uint32_t queryIndex = 1;

		// 每帧使用当前 frame slot 的 RenderScene（由 SetRenderScene 从组合层填充）
		const RenderScene& renderScene = this->renderScenes_[this->frameIndex_];

		const FrameGraph& frameGraph = this->renderContext_->GetFrameGraph();
		if (frameGraph.NeedsRebuild())
		{
			LOG_ERROR("Renderer: FrameGraph must be rebuilt before rendering.");
			return;
		}

		const FrameGraphExecutionPlan& executionPlan = frameGraph.GetExecutionPlan();
		if (!executionPlan.valid_)
		{
			LOG_ERROR("Renderer: FrameGraph execution plan is invalid.");
			return;
		}

		for (const CompiledPass& compiledPass : executionPlan.passes_)
		{
			const FrameGraphPassNode& node = frameGraph.GetNode(compiledPass.nodeId_);
			RenderPass* renderPass = node.renderPass_;
			if (renderPass == nullptr)
			{
				LOG_ERROR("Renderer: FrameGraph node contains an invalid RenderPassIndex.");
				return;
			}

			for (const CompiledResourceBarrier& compiledBarrier : compiledPass.barriersBefore_)
			{
				const uint32_t resourceImageIndex = this->renderResources_.GetImageCount(compiledBarrier.resource_.id_) == 1 ? 0 : this->imageIndex_;

				VkImageMemoryBarrier imageBarrier{};
				imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageBarrier.srcAccessMask = GetAccessMask(compiledBarrier.srcUsage_, true);
				imageBarrier.dstAccessMask = GetAccessMask(compiledBarrier.dstUsage_, false);
				imageBarrier.oldLayout = compiledBarrier.srcUsage_.requiredLayout_;
				imageBarrier.newLayout = compiledBarrier.dstUsage_.requiredLayout_;
				imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageBarrier.image = this->renderResources_.GetImage(compiledBarrier.resource_, resourceImageIndex).image_;
				imageBarrier.subresourceRange.aspectMask =
					compiledBarrier.dstUsage_.type_ == ResourceUsage::DepthAttachment
					? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
				imageBarrier.subresourceRange.baseMipLevel = 0;
				imageBarrier.subresourceRange.levelCount = 1;
				imageBarrier.subresourceRange.baseArrayLayer = 0;
				imageBarrier.subresourceRange.layerCount = 1;

				vkCmdPipelineBarrier(
					this->currentCB_,
					GetPipelineStage(compiledBarrier.srcUsage_),
					GetPipelineStage(compiledBarrier.dstUsage_),
					0,
					0, nullptr,
					0, nullptr,
					1, &imageBarrier
				);
			}

			if (this->timestampQuerySupported_)
			{
				vkCmdWriteTimestamp(this->currentCB_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.queryPool_, queryIndex);
			}

			const std::span<const VkClearValue> clearValues = renderPass->GetClearValues();
			VkRenderPassBeginInfo renderPassBeginInfo{};
			renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassBeginInfo.renderPass = renderPass->GetHandle();
			renderPassBeginInfo.framebuffer = renderPass->GetFramebuffer(this->imageIndex_);
			renderPassBeginInfo.renderArea.extent = this->swapChain_.GetExtent();
			renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassBeginInfo.pClearValues = clearValues.data();

			vkCmdBeginRenderPass(this->currentCB_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			renderPass->Execute(currentCB_, this->frameIndex_, renderScene);
			vkCmdEndRenderPass(this->currentCB_);

			if (this->timestampQuerySupported_)
			{
				// 每个 Pass 消耗 2 个 query：[n]=开始 [n+1]=结束
				vkCmdWriteTimestamp(this->currentCB_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.queryPool_, queryIndex + 1);
				queryIndex += 2;
			}
		}
	}

    void Renderer::UpdateFrameUniformData()
	{
		const RenderScene& renderScene = this->renderScenes_[this->frameIndex_];
		this->UpdateCameraUniformData(renderScene);
		this->UpdateSceneParamUniformData(renderScene);
	}

    void Renderer::UpdateCameraUniformData(const RenderScene& renderScene)
	{
		shader_protocol::CameraUniformData cameraData;
		cameraData.projection = renderScene.camera.projection;
		cameraData.model = renderScene.modelMatrix;
		cameraData.view = renderScene.camera.view;
		cameraData.camPos = renderScene.camera.position;

		this->WriteFrameUniformBuffer(RenderResourceId::MainCamera, &cameraData, sizeof(cameraData));
	}

    void Renderer::UpdateSceneParamUniformData(const RenderScene& renderScene)
	{
		shader_protocol::SceneParamUniformData params;
		params.lightDir = glm::vec4(renderScene.light.direction, 0.0f);
		params.exposure = renderScene.environment.exposure;
		params.gamma = renderScene.environment.gamma;
		params.prefilteredCubeMipLevels = static_cast<float>(
			this->renderResources_.GetPrefilteredCubeMipLevels(RenderResourceId::Environment)
		);
		params.scaleIBLAmbient = renderScene.environment.scaleIBLAmbient;
		params.debugViewInputs = renderScene.settings.debugViewInputs;
		params.debugViewEquation = renderScene.settings.debugViewEquation;
		params.debugBSDFType = renderScene.settings.debugBsdfType;

		this->WriteFrameUniformBuffer(RenderResourceId::SceneParam, &params, sizeof(params));
	}

    void Renderer::WriteFrameUniformBuffer(RenderResourceId resourceId, const void* data, VkDeviceSize size)
	{
		auto& buffer = this->renderResources_.GetBuffers(resourceId)[this->frameIndex_];
		std::memcpy(buffer.mapped, data, static_cast<size_t>(size));
	}

	void Renderer::EndFrame()
	{
		auto queue = core::Device::Instance().GetGraphicsQueue();
		auto& frameContext = this->frameContexts_[this->frameIndex_];

		static auto endLabel = core::Device::Instance().GetCmdEndDebugUtilsLabel();
		if (endLabel)
		{
			endLabel(this->currentCB_);
		}

		SUCCESS_OR_LOG(
			vkEndCommandBuffer(this->currentCB_) == VK_SUCCESS,
			"Renderer: Failed to end command buffer."
		);

		const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pWaitDstStageMask = &waitDstStageMask;
		submitInfo.pWaitSemaphores = &frameContext.imageAvailableSemaphore_;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &this->renderFinishedSemaphores_[this->imageIndex_];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pCommandBuffers = &frameContext.commandBuffer_;
		submitInfo.commandBufferCount = 1;

		SUCCESS_OR_LOG(
			vkQueueSubmit(queue, 1, &submitInfo, frameContext.inFlightFence_) == VK_SUCCESS,
			"Renderer: Failed to queue submit."
		);

		VkResult present = this->swapChain_.QueuePresent(queue, this->imageIndex_, this->renderFinishedSemaphores_[this->imageIndex_]);
		if ((present == VK_ERROR_OUT_OF_DATE_KHR) || (present == VK_SUBOPTIMAL_KHR))
		{
			this->resizePending_ = true;
			this->forceResize_ = true;
			return;
		}
		if (present != VK_SUCCESS)
		{
			LOG_ERROR("Renderer: Failed to present swap chain image, VkResult: " << present);
			return;
		}

		// 该 FrameContext 已成功提交并展示一帧，下一轮 BeginFrame 才能读取其 timestamp 结果
		frameContext.hasSubmittedFrame_ = true;

		// 动画更新属于"场景逻辑"，由组合层（Application）每帧驱动，Renderer 不持有 Scene

		this->frameIndex_ = (this->frameIndex_ + 1) % this->frameCount_;
	}
	
	VkPipelineCache Renderer::GetPipelineCache()
	{
		return PipelineCache::Instance().GetHandle();
	}

	VkCommandBuffer Renderer::GetCurrentCommandBuffer()
	{
		return this->currentCB_;
	}

	VkRenderPass Renderer::GetUIRenderPass() const
	{
		return this->uiRenderPass_;
	}

	void Renderer::BeginUIRenderPass()
	{
		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = this->uiRenderPass_;
		renderPassBeginInfo.framebuffer = this->uiFramebuffers_[this->imageIndex_];
		renderPassBeginInfo.renderArea.extent = this->swapChain_.GetExtent();

		vkCmdBeginRenderPass(this->currentCB_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	void Renderer::EndUIRenderPass()
	{
		vkCmdEndRenderPass(this->currentCB_);
	}

    GpuTimings Renderer::GetGpuTimings()
    {
        return this->lastGpuTimings_;
    }

    Renderer::~Renderer()
    {
        this->Destroy();
    }

    void Renderer::Destroy()
    {
        auto& device = core::Device::Instance();

        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());

        // framebuffer 引用图像 view，必须在 RenderResourceRegistry 和 Swapchain view 之前销毁。
        this->DestroyUIFramebuffers();
        if (this->renderContext_ != nullptr)
        {
            const FrameGraph& frameGraph = this->renderContext_->GetFrameGraph();
            for (const CompiledPass& compiledPass : frameGraph.GetExecutionPlan().passes_)
            {
                RenderPass* renderPass = frameGraph.GetNode(compiledPass.nodeId_).renderPass_;
                renderPass->DestroyRenderTarget();
            }
        }
        if (this->uiRenderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device.GetLogicalDeviceHandle(), this->uiRenderPass_, nullptr);
            this->uiRenderPass_ = VK_NULL_HANDLE;
        }
		PipelineCache::Instance().Destroy();

        for (auto& sem : this->renderFinishedSemaphores_) 
		{
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(device.GetLogicalDeviceHandle(), sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }

		this->DestroyFrameContexts();

		this->renderResources_.ClearBuffers();
		this->renderScenes_.clear();

        if (this->commandPool_ != VK_NULL_HANDLE) 
		{
            vkDestroyCommandPool(device.GetLogicalDeviceHandle(), this->commandPool_, nullptr);
            this->commandPool_ = VK_NULL_HANDLE;
        }

		this->renderResources_.ClearImages();
		this->renderFinishedSemaphores_.clear();
		this->currentCB_ = VK_NULL_HANDLE;

    }

    void Renderer::DestroyFrameContexts()
    {
		for (auto& frameContext : this->frameContexts_)
		{
			if (frameContext.commandBuffer_ != VK_NULL_HANDLE)
			{
				vkFreeCommandBuffers(core::Device::Instance().GetLogicalDeviceHandle(), this->commandPool_, 1, &frameContext.commandBuffer_);
				frameContext.commandBuffer_ = VK_NULL_HANDLE;
			}

			if (frameContext.inFlightFence_ != VK_NULL_HANDLE)
			{
				vkDestroyFence(core::Device::Instance().GetLogicalDeviceHandle(), frameContext.inFlightFence_, nullptr);
				frameContext.inFlightFence_ = VK_NULL_HANDLE;
			}

			if (frameContext.imageAvailableSemaphore_ != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(core::Device::Instance().GetLogicalDeviceHandle(), frameContext.imageAvailableSemaphore_, nullptr);
				frameContext.imageAvailableSemaphore_ = VK_NULL_HANDLE;
			}

			if (frameContext.queryPool_ != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(core::Device::Instance().GetLogicalDeviceHandle(), frameContext.queryPool_, nullptr);
				frameContext.queryPool_ = VK_NULL_HANDLE;
			}
		}
		this->frameContexts_.clear();
	}
}
