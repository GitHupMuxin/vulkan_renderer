#pragma once
#include <vector> 
#include "engine/core/physical_device.h"

namespace engine::core
{
    class LogicalDevice
    {
        private:
            PhysicalDevice*                         physicalDeviceHandle_;
            VkDevice                                logicDeviceHandle_;
            std::vector<VkQueueFamilyProperties>    queueFamilyProperties_;
            bool                                    requireStaging_ = true;
        
            std::vector<const char* >               enabledExtension_;
            VkPhysicalDeviceFeatures                enabledFeatures_;


        public:
            struct 
            {
                uint32_t graphics_;
                uint32_t compute_;
            } queueFamilyIndices_;

            void BindingPhysicalDevice(PhysicalDevice* pDevice);
            bool Init(std::vector<const char* >& extension, VkPhysicalDeviceFeatures& requestedFeatures);
            bool CreateQueue(VkQueue* queue);
            void Destroy();
            uint32_t GetQueueFamilyIndices(VkQueueFlagBits queueFlags);
            VkDevice GetDeviceHandle();
            VkPhysicalDeviceFeatures GetEnableFeatures();
            VkPhysicalDeviceProperties GetDeviceProperties();
    };

}



