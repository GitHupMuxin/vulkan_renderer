#include "engine/utils/log.h"
#include "engine/core/logical_device.h"

namespace engine::core
{
    void LogicalDevice::BindingPhysicalDevice(PhysicalDevice* pDevice)
    {
        LOG_INFO("Logical Device: start binded physical device");   
        this->physicalDeviceHandle_ = pDevice;
        assert(this->physicalDeviceHandle_);
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDeviceHandle_->devicehandle_, &queueFamilyCount, nullptr);
        assert(queueFamilyCount > 0);
        this->queueFamilyProperties_.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDeviceHandle_->devicehandle_, &queueFamilyCount, this->queueFamilyProperties_.data());

        // Check if the device has a host accesible device local buffer
        // That either means BAR (max. 256 MByte) or ReBAR (SAM)/Integrated GPU with access to all memory
        // But even 256 MByte is more than enough, and such a memory type saves us from having to stage memory
        for (uint32_t i = 0; i < this->physicalDeviceHandle_->deviceMemoryProperties_.memoryTypeCount; i++) {
            if (((this->physicalDeviceHandle_->deviceMemoryProperties_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) > 0) && ((this->physicalDeviceHandle_->deviceMemoryProperties_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) > 0)) {
                this->requireStaging_ = false;
                break;
            }
        }
    }

    bool LogicalDevice::Init(std::vector<const char* >& extension, VkPhysicalDeviceFeatures& requestedFeatures)
    {
        LOG_INFO("Logical Device: start get graphics indices!");
        // VkPhysicalDeviceFeatures enabledFeatures{};
        this->enabledFeatures_ = requestedFeatures;
		if (!this->physicalDeviceHandle_->deviceFeatures_.samplerAnisotropy) 
        {
			this->enabledFeatures_.samplerAnisotropy = VK_FALSE;
		}
		// std::vector<const char*> enabledExtensions{};
        
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

        // Get queue family indices for the requested queue family types
        // Note that the indices may overlap depending on the implementation

        const float defaultQueuePriority(0.0f);

        VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        // Graphics queue
        if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT) {
            this->queueFamilyIndices_.graphics_ = this->GetQueueFamilyIndices(VK_QUEUE_GRAPHICS_BIT);
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = this->queueFamilyIndices_.graphics_;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
        } else {
            this->queueFamilyIndices_.graphics_ = 0;
        }

        // Dedicated compute queue
        if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT) {
            this->queueFamilyIndices_.compute_ = this->GetQueueFamilyIndices(VK_QUEUE_COMPUTE_BIT);
            if (this->queueFamilyIndices_.compute_ != this->queueFamilyIndices_.graphics_) {
                // If compute family index differs, we need an additional queue create info for the compute queue
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = this->queueFamilyIndices_.compute_;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueInfo);
            }
        } else {
            // Else we use the same queue
            this->queueFamilyIndices_.compute_ = this->queueFamilyIndices_.graphics_;
        }

        // Create the logical device representation
        // std::vector<const char*> deviceExtensions(enabledExtensions);
        // deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.pEnabledFeatures = &this->enabledFeatures_;

        for (auto& it : extension)
            this->enabledExtension_.push_back(it);

        if (this->enabledExtension_.size() > 0) {
            deviceCreateInfo.enabledExtensionCount = (uint32_t)this->enabledExtension_.size();
            deviceCreateInfo.ppEnabledExtensionNames = this->enabledExtension_.data();
        }

        VkResult result = vkCreateDevice(this->physicalDeviceHandle_->devicehandle_, &deviceCreateInfo, nullptr, &this->logicDeviceHandle_);
        SUCCESS_OR_LOG(result == VK_SUCCESS, "Logical Device: can not create a logical device!");

        if (result == VK_SUCCESS) {
            // commandPool = createCommandPool(queueFamilyIndices.graphics);
        }
        else 
            return false;

        return true;
    }

    uint32_t LogicalDevice::GetQueueFamilyIndices(VkQueueFlagBits queueFlags)
    {
        // Dedicated queue for compute
        // Try to find a queue family index that supports compute but not graphics
        if (queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(this->queueFamilyProperties_.size()); i++) {
                if ((this->queueFamilyProperties_[i].queueFlags & queueFlags) && ((this->queueFamilyProperties_[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
                    return i;
                    break;
                }
            }
        }

        // For other queue types or if no separate compute queue is present, return the first one to support the requested flags
        for (uint32_t i = 0; i < static_cast<uint32_t>(this->queueFamilyProperties_.size()); i++) {
            if (this->queueFamilyProperties_[i].queueFlags & queueFlags) {
                return i;
                break;
            }
        }

        SUCCESS_OR_LOG(false, "Logical Device: can not find a queue family index!");
        return 0;
    }

    VkDevice LogicalDevice::GetDeviceHandle()
    {
        return this->logicDeviceHandle_;
    }
    
    bool LogicalDevice::CreateQueue(VkQueue* queue)
    {
        vkGetDeviceQueue(this->logicDeviceHandle_, this->queueFamilyIndices_.graphics_, 0, queue);
        return true;
    }

    void LogicalDevice::Destroy()
    {
        if (this->logicDeviceHandle_ != VK_NULL_HANDLE) {
            vkDestroyDevice(this->logicDeviceHandle_, nullptr);
            this->logicDeviceHandle_ = VK_NULL_HANDLE;
        }
    }

    VkPhysicalDeviceFeatures LogicalDevice::GetEnableFeatures()
    {
        return this->enabledFeatures_;
    }

    VkPhysicalDeviceProperties LogicalDevice::GetDeviceProperties()
    {
        return this->physicalDeviceHandle_->deviceProperties_;
    }

}
