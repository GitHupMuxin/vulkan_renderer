#include <assert.h>
#include "engine/utils/log.h"
#include "engine/core/device.h"

namespace engine::core
{
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, 
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* userData 
    )
	{
        (void)messageType;
        (void)userData;

        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            LOG_ERROR(pCallbackData->pMessage);
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            LOG_WARN(pCallbackData->pMessage);
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            LOG_INFO(pCallbackData->pMessage);
        }

        return VK_FALSE;
	}

    Device::Device() { }

    Device::~Device() 
    {
        auto vk = this->logicalDevice_.GetDeviceHandle();
        vkDeviceWaitIdle(vk);
        if (this->commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vk, this->commandPool_, nullptr);
        }
        this->logicalDevice_.Destroy();
        if (this->debugUtilsMessenger_ != VK_NULL_HANDLE && this->destroyDebugUtilsMessenger_) {
            this->destroyDebugUtilsMessenger_(this->instance_, this->debugUtilsMessenger_, nullptr);
        }
        if (this->instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(this->instance_, nullptr);
        }
    }

    Device& Device::Instance()
    {
        static Device instance;
        return instance;
    }

    PFN_vkCmdBeginDebugUtilsLabelEXT Device::GetCmdBeginDebugUtilsLabel()
    {
        return this->cmdBeginDebugUtilsLabel_;
    }

    PFN_vkCmdEndDebugUtilsLabelEXT Device::GetCmdEndDebugUtilsLabel()
    {
        return this->cmdEndDebugUtilsLabel_;
    }

    void Device::Init(DeviceSetting settings)
    {
        this->settings_ = settings;
        this->InitDevice();
    }

    void Device::InitDevice()
    {
        SUCCESS_OR_LOG(this->CreateInstance(), "Device: Fail to create instance!");
        
        if (this->settings_.validation_)
        {
            SUCCESS_OR_LOG(this->CreateDebugMessenger(), "Device: Fail to enable validation layer!");
        }

        SUCCESS_OR_LOG(this->PickUpAGPU(), "Device: Fail to pick up a GPU!");

        SUCCESS_OR_LOG(this->CreateLogicDevice(), "Device: Fail to create a logical device");

        SUCCESS_OR_LOG(this->CreateGraphicsQueue(), "Device: Fail to get a graphics queue!");

        SUCCESS_OR_LOG(this->CreateCommandPool(), "Device: Fail to create a command pool!");

    }

    bool Device::CreateInstance()
    {
        LOG_INFO("Device: start create instance!");
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "";
        appInfo.apiVersion = VK_API_VERSION_1_0;

        // std::vector<const char* > instanceExtension = { VK_KHR_SURFACE_EXTENSION_NAME };
        for (auto& it : this->settings_.constInstanceExtensions_)
            this->enabledExtension_.emplace_back(it);
        for (auto& it : this->settings_.optionalInstanceExtensions_)
            this->enabledExtension_.emplace_back(it);

        VkInstanceCreateInfo instanceCI{};
        instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCI.pNext = nullptr;
        instanceCI.pApplicationInfo = &appInfo;

        if (this->enabledExtension_.size() > 0)
        {
            if (this->settings_.validation_)
                this->enabledExtension_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            instanceCI.enabledExtensionCount = (uint32_t)this->enabledExtension_.size();
            instanceCI.ppEnabledExtensionNames = this->enabledExtension_.data();
        }

        for (auto& it : this->settings_.constEnableLayers_)
            this->enabledLayerName_.emplace_back(it);
        for (auto& it : this->settings_.optionalEnableLayers_)
            this->enabledLayerName_.emplace_back(it);   

        if (this->settings_.validation_)
        {
            this->enabledLayerName_.push_back("VK_LAYER_KHRONOS_validation");
            instanceCI.enabledLayerCount = (uint32_t)this->enabledLayerName_.size();
            instanceCI.ppEnabledLayerNames = this->enabledLayerName_.data();
        }

        return vkCreateInstance(&instanceCI, nullptr, &this->instance_) == VK_SUCCESS;
    }   

    bool Device::CreateDebugMessenger()
    {
        LOG_INFO("Device: start to create debug utils messenger!");

        this->createDebugUtilsMessenger_ = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(this->instance_, "vkCreateDebugUtilsMessengerEXT")
        );

        this->destroyDebugUtilsMessenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(this->instance_, "vkDestroyDebugUtilsMessengerEXT")
        );

        if (this->createDebugUtilsMessenger_ == nullptr || this->destroyDebugUtilsMessenger_ == nullptr)
        {
            LOG_WARN("Device: VK_EXT_debug_utils functions are unavailable.");
            return false;
        }

        VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo{};
        messengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

        messengerCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

        messengerCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

        messengerCreateInfo.pfnUserCallback = DebugMessageCallback;

        return this->createDebugUtilsMessenger_(this->instance_, &messengerCreateInfo, nullptr, &this->debugUtilsMessenger_) == VK_SUCCESS;
    }

    bool Device::PickUpAGPU()
    {
        LOG_INFO("Device: start to pick up a GPU!");
        bool success = true;
		uint32_t gpuCount = 0;
		vkEnumeratePhysicalDevices(this->instance_, &gpuCount, nullptr);
		assert(gpuCount > 0);
		std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
		vkEnumeratePhysicalDevices(this->instance_, &gpuCount, physicalDevices.data());
        
        for (auto& dev : physicalDevices) 
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) 
            {
                this->physicalDevice_.devicehandle_ = dev;
                LOG_INFO("Selected discrete GPU: " << props.deviceName);
                return this->physicalDevice_.Init();
            }
        }
        // 没有独显就选第一个
        this->physicalDevice_.devicehandle_ = physicalDevices[0];
        success = this->physicalDevice_.Init();
        LOG_INFO("No discrete GPU found, using: " << this->physicalDevice_.deviceProperties_.deviceName);
        return success;
    }

    bool Device::CreateLogicDevice()
    {
        LOG_INFO("Device: start to create logical device!");
        this->logicalDevice_.BindingPhysicalDevice(&this->physicalDevice_);

        std::vector<const char* > ext(this->settings_.constDeviceExtensions_);
        for (auto& it : this->settings_.optionalDeviceExtensions_)
            ext.emplace_back(it);

        bool success = this->logicalDevice_.Init(ext, this->settings_.requestedFeatures_);

        this->setDebugUtilsObjectName_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(this->logicalDevice_.GetDeviceHandle(), "vkSetDebugUtilsObjectNameEXT")
        );

        this->cmdBeginDebugUtilsLabel_ = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(this->logicalDevice_.GetDeviceHandle(), "vkCmdBeginDebugUtilsLabelEXT")
        );

        this->cmdEndDebugUtilsLabel_ = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(this->logicalDevice_.GetDeviceHandle(), "vkCmdEndDebugUtilsLabelEXT")
        );

        if(!success)
            SUCCESS_OR_LOG(success, "Device: can not create logical device!");
        return success;
    }

    bool Device::CreateGraphicsQueue()
    {
        LOG_INFO("Device: start to get graphics queue!");
        return this->logicalDevice_.CreateQueue(&this->queue_);
    }
            
    bool Device::CreateCommandPool()
    {
        LOG_INFO("Device: start to create command pool!");
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = this->logicalDevice_.queueFamilyIndices_.graphics_;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        return vkCreateCommandPool(this->logicalDevice_.GetDeviceHandle(), &cmdPoolInfo, nullptr, &this->commandPool_) == VK_SUCCESS;
    }

    bool Device::CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void *data, VkDeviceSize *actualBufferSize)
    {
        assert(size > 0);
        // Create the buffer handle
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.usage = usageFlags;
        bufferCreateInfo.size = size;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        SUCCESS_OR_LOG(
            vkCreateBuffer(this->logicalDevice_.GetDeviceHandle(), &bufferCreateInfo, nullptr, buffer) == VK_SUCCESS,
            "Device: Failed to create buffer."
        );

        // Create the memory backing up the buffer handle
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc{};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vkGetBufferMemoryRequirements(this->logicalDevice_.GetDeviceHandle(), *buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        // Find a memory type index that fits the properties of the buffer
        memAlloc.memoryTypeIndex = this->GetMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);

        SUCCESS_OR_LOG(
            vkAllocateMemory(this->logicalDevice_.GetDeviceHandle(), &memAlloc, nullptr, memory) == VK_SUCCESS,
            "Device: Failed to allocate memory.");

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data != nullptr)
        {
            void *mapped;
            SUCCESS_OR_LOG(
                vkMapMemory(this->logicalDevice_.GetDeviceHandle(), *memory, 0, size, 0, &mapped) == VK_SUCCESS,
                "Device: Failed to map memory."
            );

            memcpy(mapped, data, size);
            // If host coherency hasn't been requested, do a manual flush to make writes visible
            if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
            {
                VkMappedMemoryRange mappedRange{};
                mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                mappedRange.memory = *memory;
                mappedRange.offset = 0;
                mappedRange.size = memReqs.size;
                vkFlushMappedMemoryRanges(this->logicalDevice_.GetDeviceHandle(), 1, &mappedRange);
            }
            vkUnmapMemory(this->logicalDevice_.GetDeviceHandle(), *memory);
        }

        // Attach the memory to the buffer object
        SUCCESS_OR_LOG(
            vkBindBufferMemory(this->logicalDevice_.GetDeviceHandle(), *buffer, *memory, 0) == VK_SUCCESS,
            "Device: Failed to bind buffer memory."
        );

        if (actualBufferSize) 
        {
            *actualBufferSize = memReqs.size;
        }

        return true;
    }
            
    VkCommandBuffer Device::CreateCommandBuffer(VkCommandBufferLevel level, bool begin)
    {
        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = this->commandPool_;
        cmdBufAllocateInfo.level = level;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        SUCCESS_OR_LOG(
            vkAllocateCommandBuffers(this->logicalDevice_.GetDeviceHandle(), &cmdBufAllocateInfo, &cmdBuffer) == VK_SUCCESS,
            "Device: Failed to create command buffer."
        );

        // If requested, also start recording for the new command buffer
        if (begin) 
        {
            VkCommandBufferBeginInfo commandBufferBI{};
            commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            SUCCESS_OR_LOG(
                vkBeginCommandBuffer(cmdBuffer, &commandBufferBI) == VK_SUCCESS,
                "Device: Failed to begin command buffer."
            );
        }

        return cmdBuffer;
    }
    
    bool Device::FlushCommandBuffer(VkCommandBuffer commandBuffer, bool free)
    {
        SUCCESS_OR_LOG(
            vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
            "Device: Failed to end command buffer.");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;

        SUCCESS_OR_LOG(
            vkCreateFence(this->logicalDevice_.GetDeviceHandle(), &fenceInfo, nullptr, &fence) == VK_SUCCESS,
            "Device: Failed to create fence.");

        // Submit to the queue
        SUCCESS_OR_LOG(
            vkQueueSubmit(this->GetGraphicsQueue(), 1, &submitInfo, fence) == VK_SUCCESS,
            "Device: Failed to submit queue.");
        // Wait for the fence to signal that command buffer has finished executing
        SUCCESS_OR_LOG(
            vkWaitForFences(this->logicalDevice_.GetDeviceHandle(), 1, &fence, VK_TRUE, 100000000000) == VK_SUCCESS,
            "Device: Failed to wait for fences.");

        vkDestroyFence(this->logicalDevice_.GetDeviceHandle(), fence, nullptr);

        if (free) {
            vkFreeCommandBuffers(this->logicalDevice_.GetDeviceHandle(), this->commandPool_, 1, &commandBuffer);
        }

        return true;
    }



       
    void Device::SetSettings(DeviceSetting setting)
    {
        this->settings_ = setting;
    }

    VkInstance Device::GetInstanceHandle()
    {
        return this->instance_;
    }

    VkPhysicalDevice Device::GetPhysicalDeviceHandle()
    {
        return this->physicalDevice_.devicehandle_;
    }

    VkPhysicalDeviceFeatures Device::GetPhysicalDeviceFeatures()
    {
        return this->physicalDevice_.deviceFeatures_;
    }

    VkDevice Device::GetLogicalDeviceHandle()
    {
        return this->logicalDevice_.GetDeviceHandle();
    }

    VkQueue Device::GetGraphicsQueue()
    {
        return this->queue_;
    }

    uint32_t  Device::GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound)
    {
        for (uint32_t i = 0; i < this->physicalDevice_.deviceMemoryProperties_.memoryTypeCount; i++) 
        {
            if ((typeBits & 1) == 1) 
            {
                if ((this->physicalDevice_.deviceMemoryProperties_.memoryTypes[i].propertyFlags & properties) == properties) 
                {
                    if (memTypeFound) 
                    {
                        *memTypeFound = true;
                    }
                    return i;
                }
            }
            typeBits >>= 1;
        }

        if (memTypeFound) 
        {
            *memTypeFound = false;
            return 0;
        } 
        else 
        {
            throw std::runtime_error("Could not find a matching memory type");
        }
    }

    VkSampleCountFlagBits Device::GetMultiSampleCount()
    {
        return this->settings_.sampleCount_;
    }

    bool Device::GetRequireStaging()
    {
        return this->physicalDevice_.requiresStaging_;
    }


    VkPhysicalDeviceFeatures Device::GetEnableFeatures()
    {
        return this->logicalDevice_.GetEnableFeatures();
    }


    VkPhysicalDeviceProperties Device::GetDeviceProperties()
    {
        return this->logicalDevice_.GetDeviceProperties();
    }

    DeviceSetting Device::GetSetting()
    {
        return this->settings_;
    }

    void Device::BeginCommandBuffer(VkCommandBuffer commandBuffer)
    {
        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        SUCCESS_OR_LOG(
            vkBeginCommandBuffer(commandBuffer, &commandBufferBI) == VK_SUCCESS,
            "Device: Failed to begin command buffer."
        );
    }

    void Device::SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name)
    {
        if (this->setDebugUtilsObjectName_ && name) 
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = objectType;
            nameInfo.objectHandle = objectHandle;
            nameInfo.pObjectName = name;

            this->setDebugUtilsObjectName_(this->logicalDevice_.GetDeviceHandle(), &nameInfo);
        }
    }

    const std::vector<std::string>& Device::GetSupportedExtension()
    {
        this->supportedExtensions_.clear();

        if (this->physicalDevice_.devicehandle_ == VK_NULL_HANDLE) 
        {
            LOG_ERROR("Device: Cannot enumerate extensions before selecting a physical device.");
            return this->supportedExtensions_;
        }

        uint32_t extensionCount = 0;
        VkResult result = vkEnumerateDeviceExtensionProperties(
            this->physicalDevice_.devicehandle_, nullptr, &extensionCount, nullptr);
        if (result != VK_SUCCESS) 
        {
            LOG_ERROR("Device: Failed to get supported extension count, VkResult: " << result);
            return this->supportedExtensions_;
        }

        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        result = vkEnumerateDeviceExtensionProperties(
            this->physicalDevice_.devicehandle_, nullptr, &extensionCount, extensionProperties.data());
        if (result != VK_SUCCESS) 
        {
            LOG_ERROR("Device: Failed to enumerate supported extensions, VkResult: " << result);
            return this->supportedExtensions_;
        }

        this->supportedExtensions_.reserve(extensionCount);
        for (const auto& extension : extensionProperties) 
        {
            this->supportedExtensions_.emplace_back(extension.extensionName);
        }

        return this->supportedExtensions_;
    }

}
