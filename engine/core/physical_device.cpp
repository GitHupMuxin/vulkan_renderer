#include "engine/utils/log.h"
#include "engine/core/physical_device.h"

namespace engine::core 
{
    bool PhysicalDevice::Init()
    {
        LOG_INFO("PhysicalDevice: start to init");
        if (this->devicehandle_ ==  VK_NULL_HANDLE)
        {
            LOG_ERROR("Physical Device init fail!");
            return false;
        }
        vkGetPhysicalDeviceProperties(this->devicehandle_, &this->deviceProperties_);
        vkGetPhysicalDeviceFeatures(this->devicehandle_, &this->deviceFeatures_);
        vkGetPhysicalDeviceMemoryProperties(this->devicehandle_, &this->deviceMemoryProperties_);
       
        for (uint32_t i = 0; i < this->deviceMemoryProperties_.memoryTypeCount; i++) {
            if (((this->deviceMemoryProperties_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) > 0) && ((this->deviceMemoryProperties_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) > 0)) {
                this->requiresStaging_ = false;
                break;
            }
        }

        return true;
    }
}
