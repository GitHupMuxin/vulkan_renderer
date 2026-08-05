#include "engine/render/renderer.h"
#include "engine/utils/log.h"
#include "engine/resource/resource_manager.h"
#include "engine/core/loader.h"

namespace engine::render
{

	Attachment::~Attachment()
    {
        this->Destroy();
    }

    Attachment::Attachment(Attachment&& other) noexcept
        : image_(other.image_)
        , imageView_(other.imageView_)
        , memory_(other.memory_)
    {
        other.image_ = VK_NULL_HANDLE;
        other.imageView_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }

    Attachment& Attachment::operator=(Attachment&& other) noexcept
    {
        if (this != &other) {
            this->Destroy();
            this->image_ = other.image_;
            this->imageView_ = other.imageView_;
            this->memory_ = other.memory_;
            other.image_ = VK_NULL_HANDLE;
            other.imageView_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Attachment::Destroy()
    {
        auto& device = core::Device::Instance();
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device.GetLogicalDeviceHandle(), imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device.GetLogicalDeviceHandle(), image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device.GetLogicalDeviceHandle(), memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }
   
    
    Renderer::Renderer()
    {
		this->frameCount_ = core::Device::Instance().GetSetting().frameCount_;
		this->frameBuffers_.resize(this->frameCount_);
    }

    Renderer::Renderer(const RendererDescription& description) : rendererDescription_(description)
    {
		this->frameCount_ = core::Device::Instance().GetSetting().frameCount_;
		this->frameBuffers_.resize(this->frameCount_);
    }

    void Renderer::PrepareFrame()
    {
		LOG_INFO("Renderer: start to prepare frame...");
        this->renderPassInitInfo_.swapChain_ = &this->swapChain_;
        this->renderPassInitInfo_.multiSamplingEnabled_ = this->rendererDescription_.multiSampling_;
		this->renderPassInitInfo_.pipelineCache_ = &this->pipelineCache_;
		this->renderPassInitInfo_.scene_ = this->scene_;
		this->renderPassInitInfo_.descriptorPool_ = &this->descriptorPool_;
		this->renderPassInitInfo_.mainRenderPass_ = &this->mainRenderPass_;
    
        for (auto& renderPass : this->renderPasses_)
        {
            renderPass->Init(this->renderPassInitInfo_);
        }

		this->CreateDescriptorPool();

		for (auto& renderPass : this->renderPasses_)
		{
			renderPass->ExecutePreProcess();
		}
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
		this->InitSwapChain(window);
        this->InitCommandPool();
        this->CreatePipelineCache();
        this->CreateSyncObjects();
        this->CreateCommandBuffers();
		this->CreateMainRenderPass();
		this->CreatMainFrameBuffer();
    }


    void Renderer::AddRenderPass(std::unique_ptr<RenderPass> renderPass)
	{
		this->renderPasses_.emplace_back(std::move(renderPass));	
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
    }

    void Renderer::CreatePipelineCache()
    {
		LOG_INFO("Renderer: start to create pipeline cache...");
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        SUCCESS_OR_LOG(
            (vkCreatePipelineCache(core::Device::Instance().GetLogicalDeviceHandle(), &pipelineCacheCreateInfo, nullptr, &this->pipelineCache_) == VK_SUCCESS),
            "Failed to create pipeline cache");
    }    

    void Renderer::CreateSyncObjects()
    {
		LOG_INFO("Renderer: start to create sync objects...");
        this->waitFences_.resize(this->frameCount_);
		this->presentCompleteSemaphores_.resize(this->swapChain_.GetImageCount());
		this->renderCompleteSemaphores_.resize(this->swapChain_.GetImageCount());
		// this->uniformBuffers_.resize(this->renderAhead_);
		// this->descriptorSets_.resize(this->renderAhead_);
		// this->shaderMeshDataBuffers_.resize(this->renderAhead_);
		// this->descriptorSetsMeshData_.resize(this->renderAhead_);
		// Command buffer execution fences
		for (auto &waitFence : this->waitFences_) {
			VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
            SUCCESS_OR_LOG(
                (vkCreateFence(core::Device::Instance().GetLogicalDeviceHandle(), &fenceCI, nullptr, &waitFence) == VK_SUCCESS),
                "Failed to create fence");
		}
		// Queue ordering semaphores
		for (auto &semaphore : this->presentCompleteSemaphores_) {
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            SUCCESS_OR_LOG(
                (vkCreateSemaphore(core::Device::Instance().GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &semaphore) == VK_SUCCESS),
                "Failed to create present complete semaphore");
		}
		for (auto &semaphore : this->renderCompleteSemaphores_) {
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            SUCCESS_OR_LOG(
                (vkCreateSemaphore(core::Device::Instance().GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &semaphore) == VK_SUCCESS),
                "Failed to create render complete semaphore");
		}
    }

    void Renderer::CreateCommandBuffers()
    {
		LOG_INFO("Renderer: start to create command buffers...");
		this->commandBuffers_.resize(this->frameCount_);
		VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
		cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdBufAllocateInfo.commandPool = this->commandPool_;
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdBufAllocateInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
        SUCCESS_OR_LOG(
            (vkAllocateCommandBuffers(core::Device::Instance().GetLogicalDeviceHandle(), &cmdBufAllocateInfo, this->commandBuffers_.data()) == VK_SUCCESS),
            "Failed to allocate command buffers");
    }


    void Renderer::PrepareUniformBUffers()
    {
        
    }


    void Renderer::CreateDescriptorPool()
	{
		LOG_INFO("Renderer: start to create descriptor pool...");
		auto& device = core::Device::Instance();
		DescriptorSetCount setCount;
		
		for (auto& renderPass : this->renderPasses_)
		{
			setCount = setCount + renderPass->GetDescriptorSetCount();
		}

		std::vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount.uniformBufferCount },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount.imageSamplerCount },
			// One SSBO for the shader material buffer and one SSBO for the mesh data buffer
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount.storageBufferCount }
		};
		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		descriptorPoolCI.pPoolSizes = poolSizes.data();
		descriptorPoolCI.maxSets = setCount.maxSets;

		SUCCESS_OR_LOG(
            vkCreateDescriptorPool(device.GetLogicalDeviceHandle(), &descriptorPoolCI, nullptr, &this->descriptorPool_) == VK_SUCCESS,
            "Renderer: Failed to set up descriptors."
        );
	}


    // void Renderer::AddRenderPass(std::unique_ptr<RenderPass> renderPass)
    // {
    //     this->renderPasses_.push_back(std::move(renderPass));
    // }

    void Renderer::BindingScene(scene::Scene* scene)
    {
        this->scene_ = scene;
    }

	void Renderer::CreateMainRenderPass()
	{
		LOG_INFO("Renderer: start to create main render pass...");
		auto& device = core::Device::Instance();
        // Initialization logic for PBRRenderPass
        if (this->rendererDescription_.multiSampling_) {
			std::array<VkAttachmentDescription, 4> attachments = {};

			// Multisampled attachment that we render to
			attachments[0].format = this->swapChain_.GetColorFormat();
			attachments[0].samples = device.GetMultiSampleCount();
			attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			// This is the frame buffer attachment to where the multisampled image
			// will be resolved to and which will be presented to the swapchain
			attachments[1].format = this->swapChain_.GetColorFormat();
			attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			// Multisampled depth attachment we render to
			attachments[2].format = this->swapChain_.GetDepthFormat();
			attachments[2].samples = device.GetMultiSampleCount();
			attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			// Depth resolve attachment
			attachments[3].format = this->swapChain_.GetDepthFormat();
			attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorReference = {};
			colorReference.attachment = 0;
			colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 2;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			// Resolve attachment reference for the color attachment
			VkAttachmentReference resolveReference = {};
			resolveReference.attachment = 1;
			resolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorReference;
			// Pass our resolve attachments to the sub pass
			subpass.pResolveAttachments = &resolveReference;
			subpass.pDepthStencilAttachment = &depthReference;

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

			VkRenderPassCreateInfo renderPassCI = {};
			renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassCI.pAttachments = attachments.data();
			renderPassCI.subpassCount = 1;
			renderPassCI.pSubpasses = &subpass;
			renderPassCI.dependencyCount = 2;
			renderPassCI.pDependencies = dependencies.data();
            SUCCESS_OR_LOG(
                (vkCreateRenderPass(device.GetLogicalDeviceHandle(), &renderPassCI, nullptr, &this->mainRenderPass_) == VK_SUCCESS),
                "Failed to create render pass");
		}
		else {
			std::array<VkAttachmentDescription, 2> attachments = {};
			// Color attachment
			attachments[0].format = this->swapChain_.GetColorFormat();
			attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			// Depth attachment
			attachments[1].format = this->swapChain_.GetDepthFormat();
			attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorReference = {};
			colorReference.attachment = 0;
			colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 1;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpassDescription = {};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.colorAttachmentCount = 1;
			subpassDescription.pColorAttachments = &colorReference;
			subpassDescription.pDepthStencilAttachment = &depthReference;
			subpassDescription.inputAttachmentCount = 0;
			subpassDescription.pInputAttachments = nullptr;
			subpassDescription.preserveAttachmentCount = 0;
			subpassDescription.pPreserveAttachments = nullptr;
			subpassDescription.pResolveAttachments = nullptr;

			// Subpass dependencies for layout transitions
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

			VkRenderPassCreateInfo renderPassCI{};
			renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
			renderPassCI.pAttachments = attachments.data();
			renderPassCI.subpassCount = 1;
			renderPassCI.pSubpasses = &subpassDescription;
			renderPassCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
			renderPassCI.pDependencies = dependencies.data();
            SUCCESS_OR_LOG(
                vkCreateRenderPass(device.GetLogicalDeviceHandle(), &renderPassCI, nullptr, &mainRenderPass_) == VK_SUCCESS,
                "Failed to create render pass"
			);
		}
	}

	void Renderer::CreatMainFrameBuffer()
	{
		LOG_INFO("Renderer: start to create main frame buffer...");
		auto& device = core::Device::Instance();
		/*
		MSAA
		*/
		if (this->rendererDescription_.multiSampling_) {
			// Check if device supports requested sample count for color and depth frame buffer
			//assert((deviceProperties.limits.framebufferColorSampleCounts >= sampleCount) && (deviceProperties.limits.framebufferDepthSampleCounts >= sampleCount));

			VkImageCreateInfo imageCI{};
			imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = this->swapChain_.GetColorFormat();
			imageCI.extent.width = this->swapChain_.GetExtent().width;
			imageCI.extent.height = this->swapChain_.GetExtent().height;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = 1;
			imageCI.arrayLayers = 1;
			imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.samples = device.GetMultiSampleCount();
			imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            SUCCESS_OR_LOG(
                (vkCreateImage(device.GetLogicalDeviceHandle(), &imageCI, nullptr, &this->mainAttachmentList_.multisampleColorAttachment_.image_) == VK_SUCCESS),
                "Failed to create multisample color image");

			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->mainAttachmentList_.multisampleColorAttachment_.image_, &memReqs);
			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAllocInfo.allocationSize = memReqs.size;
			VkBool32 lazyMemTypePresent;
			memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
			if (!lazyMemTypePresent) {
				memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			}
            SUCCESS_OR_LOG(
                vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->mainAttachmentList_.multisampleColorAttachment_.memory_) == VK_SUCCESS,
                "Failed to allocate multisample color image memory");
			vkBindImageMemory(device.GetLogicalDeviceHandle(), this->mainAttachmentList_.multisampleColorAttachment_.image_, this->mainAttachmentList_.multisampleColorAttachment_.memory_, 0);

			// Create image view for the MSAA target
			VkImageViewCreateInfo imageViewCI{};
			imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCI.image = this->mainAttachmentList_.multisampleColorAttachment_.image_;
			imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCI.format = this->swapChain_.GetColorFormat();
			imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
			imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
			imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
			imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
			imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageViewCI.subresourceRange.levelCount = 1;
			imageViewCI.subresourceRange.layerCount = 1;
            SUCCESS_OR_LOG(
                (vkCreateImageView(device.GetLogicalDeviceHandle(), &imageViewCI, nullptr, &this->mainAttachmentList_.multisampleColorAttachment_.imageView_) == VK_SUCCESS),
                "Failed to create multisample color image view");

			// Depth target
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = this->swapChain_.GetDepthFormat();
			imageCI.extent.width = this->swapChain_.GetExtent().width;
			imageCI.extent.height = this->swapChain_.GetExtent().height;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = 1;
			imageCI.arrayLayers = 1;
			imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.samples = device.GetMultiSampleCount();
			imageCI.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            SUCCESS_OR_LOG(
                vkCreateImage(device.GetLogicalDeviceHandle(), &imageCI, nullptr, &this->mainAttachmentList_.multisampleDepthAttachment_.image_) == VK_SUCCESS,
                "Failed to create multisample depth image");

			vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->mainAttachmentList_.multisampleDepthAttachment_.image_, &memReqs);
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
			if (!lazyMemTypePresent) {
				memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			}
            SUCCESS_OR_LOG(
                vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->mainAttachmentList_.multisampleDepthAttachment_.memory_) == VK_SUCCESS,
                "Failed to allocate multisample depth image memory");
			vkBindImageMemory(device.GetLogicalDeviceHandle(), this->mainAttachmentList_.multisampleDepthAttachment_.image_, this->mainAttachmentList_.multisampleDepthAttachment_.memory_, 0);

			// Create image view for the MSAA target
			imageViewCI.image = this->mainAttachmentList_.multisampleDepthAttachment_.image_;
			imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCI.format = this->swapChain_.GetDepthFormat();
			imageViewCI.components.r = VK_COMPONENT_SWIZZLE_R;
			imageViewCI.components.g = VK_COMPONENT_SWIZZLE_G;
			imageViewCI.components.b = VK_COMPONENT_SWIZZLE_B;
			imageViewCI.components.a = VK_COMPONENT_SWIZZLE_A;
			imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			imageViewCI.subresourceRange.levelCount = 1;
			imageViewCI.subresourceRange.layerCount = 1;
            SUCCESS_OR_LOG(
                (vkCreateImageView(device.GetLogicalDeviceHandle(), &imageViewCI, nullptr, &this->mainAttachmentList_.multisampleDepthAttachment_.imageView_) == VK_SUCCESS),
                "Failed to create multisample depth image view");
		}


		// Depth/Stencil attachment is the same for all frame buffers

		VkImageCreateInfo image = {};
		image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image.pNext = NULL;
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = this->swapChain_.GetDepthFormat();
		image.extent = { this->swapChain_.GetExtent().width, this->swapChain_.GetExtent().height, 1 };
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image.flags = 0;

		VkMemoryAllocateInfo mem_alloc = {};
		mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_alloc.pNext = NULL;
		mem_alloc.allocationSize = 0;
		mem_alloc.memoryTypeIndex = 0;

		VkImageViewCreateInfo depthStencilView = {};
		depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthStencilView.pNext = NULL;
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = this->swapChain_.GetDepthFormat();
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;

		VkMemoryRequirements memReqs;

        SUCCESS_OR_LOG(
            vkCreateImage(device.GetLogicalDeviceHandle(), &image, nullptr, &this->mainAttachmentList_.depthAttachment_.image_) == VK_SUCCESS,
            "Failed to create depth stencil image"
		);

		vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->mainAttachmentList_.depthAttachment_.image_, &memReqs);
		mem_alloc.allocationSize = memReqs.size;
		mem_alloc.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        SUCCESS_OR_LOG(
            vkAllocateMemory(device.GetLogicalDeviceHandle(), &mem_alloc, nullptr, &this->mainAttachmentList_.depthAttachment_.memory_) == VK_SUCCESS,
            "Failed to allocate depth stencil image memory"
		);

        SUCCESS_OR_LOG(
            vkBindImageMemory(device.GetLogicalDeviceHandle(), this->mainAttachmentList_.depthAttachment_.image_, this->mainAttachmentList_.depthAttachment_.memory_, 0) == VK_SUCCESS,
            "Failed to bind memory to depth stencil image"
		);

		depthStencilView.image = this->mainAttachmentList_.depthAttachment_.image_;

        SUCCESS_OR_LOG(
            vkCreateImageView(device.GetLogicalDeviceHandle(), &depthStencilView, nullptr, &this->mainAttachmentList_.depthAttachment_.imageView_) == VK_SUCCESS,
            "Failed to create depth stencil image view"
		);

		//

		VkImageView attachments[4];

		if (this->rendererDescription_.multiSampling_) {
			attachments[0] = this->mainAttachmentList_.multisampleColorAttachment_.imageView_;
			attachments[2] = this->mainAttachmentList_.multisampleDepthAttachment_.imageView_;
			attachments[3] = this->mainAttachmentList_.depthAttachment_.imageView_;
		}
		else {
			attachments[1] = this->mainAttachmentList_.depthAttachment_.imageView_;
		}

		VkFramebufferCreateInfo frameBufferCI{};
		frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCI.pNext = NULL;
		frameBufferCI.renderPass = this->mainRenderPass_;
		frameBufferCI.attachmentCount = this->rendererDescription_.multiSampling_ ? 4 : 2;
		frameBufferCI.pAttachments = attachments;
		frameBufferCI.width = this->swapChain_.GetExtent().width;
		frameBufferCI.height = this->swapChain_.GetExtent().height;
		frameBufferCI.layers = 1;

		// Create frame buffers for every swap chain image
		this->frameBuffers_.resize(this->swapChain_.GetImageCount());
		for (uint32_t i = 0; i < frameBuffers_.size(); i++) {
			if (this->rendererDescription_.multiSampling_) {
				attachments[1] = this->swapChain_.GetSwapChainBuffer(i).view;
			}
			else {
				attachments[0] = this->swapChain_.GetSwapChainBuffer(i).view;
			}
            SUCCESS_OR_LOG(
                (vkCreateFramebuffer(device.GetLogicalDeviceHandle(), &frameBufferCI, nullptr, &frameBuffers_[i]) == VK_SUCCESS),
                "Failed to create framebuffer");
		}
	}

    void Renderer::DestroyMainFrameBuffer()
    {
        auto& device = core::Device::Instance();
        for (auto& fb : this->frameBuffers_) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device.GetLogicalDeviceHandle(), fb, nullptr);
                fb = VK_NULL_HANDLE;
            }
        }
        this->frameBuffers_.clear();

        this->mainAttachmentList_.multisampleColorAttachment_.Destroy();
        this->mainAttachmentList_.multisampleDepthAttachment_.Destroy();
        this->mainAttachmentList_.depthAttachment_.Destroy();
        this->mainAttachmentList_.colorAttachment_.Destroy();
    }

    void Renderer::RecreateSyncObjects()
    {
        auto& device = core::Device::Instance();
        for (auto& sem : this->renderCompleteSemaphores_) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device.GetLogicalDeviceHandle(), sem, nullptr);
        }
        for (auto& sem : this->presentCompleteSemaphores_) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device.GetLogicalDeviceHandle(), sem, nullptr);
        }

        this->presentCompleteSemaphores_.resize(this->swapChain_.GetImageCount());
        this->renderCompleteSemaphores_.resize(this->swapChain_.GetImageCount());
        for (auto& sem : this->presentCompleteSemaphores_) {
            VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            SUCCESS_OR_LOG(
                vkCreateSemaphore(device.GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &sem) == VK_SUCCESS,
                "Failed to create present complete semaphore"
            );
        }
        for (auto& sem : this->renderCompleteSemaphores_) {
            VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            SUCCESS_OR_LOG(
                vkCreateSemaphore(device.GetLogicalDeviceHandle(), &semaphoreCI, nullptr, &sem) == VK_SUCCESS,
                "Failed to create render complete semaphore"
            );
        }
    }

    void Renderer::WindowResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        auto& device = core::Device::Instance();
        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());

        this->DestroyMainFrameBuffer();

        // Recreate swapchain (internally destroys old swapchain + image views)
        this->swapChain_.CreateSwapChain(&width, &height, this->rendererDescription_.vsync_);

        // Image count may have changed -> recreate semaphores sized by image count
        this->RecreateSyncObjects();

        // Rebuild MSAA/depth attachments + framebuffers with the new extent
        this->CreatMainFrameBuffer();
    }

	void Renderer::BeginFrame()
	{
		auto& device = core::Device::Instance();
		bool multiSampling = core::Device::Instance().GetSetting().multiSampling_;
		VkExtent2D extent = this->swapChain_.GetExtent();
		
		SUCCESS_OR_LOG(
			vkWaitForFences(device.GetLogicalDeviceHandle(), 1, &this->waitFences_[this->frameIndex_], VK_TRUE, UINT64_MAX) == VK_SUCCESS,
			"Renderer: Failed to wait for fences."
		);

		VkResult acquire = this->swapChain_.AcquireNextImage(this->presentCompleteSemaphores_[this->frameIndex_], &this->imageIndex_);
		if ((acquire == VK_ERROR_OUT_OF_DATE_KHR) || (acquire == VK_SUBOPTIMAL_KHR)) {
			// Swapchain is stale (window was resized) -> recreate and retry once
			VkExtent2D newExtent = this->swapChain_.GetExtent();
			this->WindowResize(newExtent.width, newExtent.height);
			acquire = this->swapChain_.AcquireNextImage(this->presentCompleteSemaphores_[this->frameIndex_], &this->imageIndex_);
			if (acquire != VK_SUCCESS) {
				return;
			}
		}

		SUCCESS_OR_LOG(
			vkResetFences(device.GetLogicalDeviceHandle(), 1, &this->waitFences_[this->frameIndex_]) == VK_SUCCESS,
			"Renderer: Failed to reset fences."
		);


		vkResetCommandBuffer(this->commandBuffers_[this->frameIndex_], 0);

		VkCommandBufferBeginInfo cmdBufferBeginInfo{};
		cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		VkClearValue clearValues[3];
		if (multiSampling) {
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
			clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
			clearValues[2].depthStencil = { 1.0f, 0 };
		}
		else {
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };
		}

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = this->mainRenderPass_;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = extent.width;
		renderPassBeginInfo.renderArea.extent.height = extent.height;
		renderPassBeginInfo.clearValueCount = multiSampling ? 3 : 2;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = this->frameBuffers_[this->imageIndex_];

		this->currentCB_ = this->commandBuffers_[this->frameIndex_];

		SUCCESS_OR_LOG(
			vkBeginCommandBuffer(this->currentCB_, &cmdBufferBeginInfo) == VK_SUCCESS,
			"Renderer: Failed to begin command buffer."
		);

		vkCmdBeginRenderPass(this->currentCB_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width = (float)extent.width;
		viewport.height = (float)extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(this->currentCB_, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.extent = { extent.width, extent.height };
		vkCmdSetScissor(this->currentCB_, 0, 1, &scissor);

	}

	void Renderer::Render()
	{
		for (auto& renderPass : this->renderPasses_) 
		{
			renderPass->Execute(currentCB_, this->frameIndex_);
		}
	}

    void Renderer::UpdateParams()
	{	
		this->scene_->UpdateParams();
	}

    void Renderer::UpdateUniformData()
	{
		this->scene_->UpdateUniformData(this->frameIndex_);
		this->scene_->UpdateParams();
	}

	void Renderer::EndFrame()
	{
		auto queue = core::Device::Instance().GetGraphicsQueue();

		vkCmdEndRenderPass(this->currentCB_);

		SUCCESS_OR_LOG(
			vkEndCommandBuffer(this->currentCB_) == VK_SUCCESS,
			"Renderer: Failed to end command buffer."
		);

		// Update UBOs
		this->UpdateUniformData();
		for (auto& renderPass : this->renderPasses_) 
		{
			renderPass->UpdateUniformData(this->frameIndex_);
		}

		const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pWaitDstStageMask = &waitDstStageMask;
		submitInfo.pWaitSemaphores = &this->presentCompleteSemaphores_[this->frameIndex_];
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &this->renderCompleteSemaphores_[this->imageIndex_];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pCommandBuffers = &this->commandBuffers_[this->frameIndex_];
		submitInfo.commandBufferCount = 1;

		SUCCESS_OR_LOG(
			vkQueueSubmit(queue, 1, &submitInfo, this->waitFences_[this->frameIndex_]) == VK_SUCCESS,
			"Renderer: Failed to queue submit."
		);

		VkResult present = this->swapChain_.QueuePresent(queue, this->imageIndex_, this->renderCompleteSemaphores_[this->imageIndex_]);
		if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) {
			if (present == VK_ERROR_OUT_OF_DATE_KHR) {
				// BeginFrame handles resize on next acquire; skip this frame
				return;
			}
			else if (present != VK_SUCCESS) 
				LOG_ERROR("Renderer: Failed to present swap chain image.");
		}

		if (!this->paused_) {
			if ((*(this->controller.animate)) && (this->scene_->sceneObjects_[0].model->GetAnimations().size() > 0)) {
				*(this->controller.animationTimer) += *(this->controller.frameTimer);
				if (*(this->controller.animationTimer) > this->scene_->sceneObjects_[0].model->GetAnimations()[*(this->controller.animationIndex)].end) {
					*(this->controller.animationTimer) -= this->scene_->sceneObjects_[0].model->GetAnimations()[*(this->controller.animationIndex)].end;
				}
				this->scene_->sceneObjects_[0].model->UpdateAnimation(*(this->controller.animationIndex), *(this->controller.animationTimer));
				this->scene_->sceneObjects_[0].model->UpdateMeshDataBuffer(this->frameIndex_);
			}
		}

		this->frameIndex_ = (this->frameIndex_ + 1) % this->frameCount_;
	}
	
	VkRenderPass Renderer::GetRenderPass()
	{
		return this->mainRenderPass_;
	}

	VkPipelineCache Renderer::GetPipelineCache()
	{
		return this->pipelineCache_;
	}

	VkCommandBuffer Renderer::GetCurrentCommandBuffer()
	{
		return this->currentCB_;
	}

    Renderer::~Renderer()
    {
        this->Destroy();
    }

    void Renderer::Destroy()
    {
        auto& device = core::Device::Instance();

        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());

        if (this->descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device.GetLogicalDeviceHandle(), this->descriptorPool_, nullptr);
            this->descriptorPool_ = VK_NULL_HANDLE;
        }

        if (this->pipelineCache_ != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device.GetLogicalDeviceHandle(), this->pipelineCache_, nullptr);
            this->pipelineCache_ = VK_NULL_HANDLE;
        }

        for (auto& sem : this->renderCompleteSemaphores_) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(device.GetLogicalDeviceHandle(), sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }
        for (auto& sem : this->presentCompleteSemaphores_) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(device.GetLogicalDeviceHandle(), sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }
        for (auto& fence : this->waitFences_) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device.GetLogicalDeviceHandle(), fence, nullptr);
                fence = VK_NULL_HANDLE;
            }
        }

        if (this->commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device.GetLogicalDeviceHandle(), this->commandPool_, nullptr);
            this->commandPool_ = VK_NULL_HANDLE;
        }

		for (auto& frameBuffer : this->frameBuffers_)
		{
			if (frameBuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device.GetLogicalDeviceHandle(), frameBuffer, nullptr);
		}
		this->frameBuffers_.clear();

		if (this->mainRenderPass_ != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(device.GetLogicalDeviceHandle(), this->mainRenderPass_, nullptr);
			this->mainRenderPass_ = VK_NULL_HANDLE;
		}

		this->mainAttachmentList_.multisampleColorAttachment_.Destroy();
		this->mainAttachmentList_.multisampleDepthAttachment_.Destroy();
		this->mainAttachmentList_.depthAttachment_.Destroy();
		this->mainAttachmentList_.colorAttachment_.Destroy();
		this->renderCompleteSemaphores_.clear();
		this->presentCompleteSemaphores_.clear();
		this->waitFences_.clear();
		this->commandBuffers_.clear();
		this->currentCB_ = VK_NULL_HANDLE;

    }
}



