#pragma once

#include <vulkan/vulkan.h>

namespace engine::core
{
    class PhysicalDevice
    {
        public:
            VkPhysicalDevice                    devicehandle_;
            VkPhysicalDeviceProperties          deviceProperties_;
            VkPhysicalDeviceFeatures            deviceFeatures_;
            VkPhysicalDeviceMemoryProperties    deviceMemoryProperties_;
            bool requiresStaging_ = true;
            bool Init();
    };
}