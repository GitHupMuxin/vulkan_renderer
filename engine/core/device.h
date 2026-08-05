#pragma once
#include <string>
#include <vector>
#include <vulkan\vulkan.h>
#include "engine/core/logical_device.h"
#include "engine/platform/window.h"

namespace engine::core
{
    struct DeviceSetting
    {
        bool validation_ = false;
        bool fullscreen_ = false;
        bool vsync_ = false;

        bool multiSampling_ = true;
        VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_4_BIT;

        uint32_t frameCount_ = 2;

        std::vector<const char* > constInstanceExtensions_{ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
        std::vector<const char* > optionalInstanceExtensions_{ };

        std::vector<const char* > constDeviceExtensions_{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        std::vector<const char* > optionalDeviceExtensions_{ };
        
        std::vector<const char* > constDeviceFeature_{ };
        VkPhysicalDeviceFeatures  requestedFeatures_{ .samplerAnisotropy = VK_TRUE };

        std::vector<const char* > constEnableLayers_{ };
        std::vector<const char* > optionalEnableLayers_{ };

        DeviceSetting() = default;
        DeviceSetting(const DeviceSetting&) = default;
        DeviceSetting& operator=(const DeviceSetting&) = default;
        ~DeviceSetting() = default;
    };

    class Device
    {
        private:
            VkInstance          instance_ = VK_NULL_HANDLE;
            PhysicalDevice      physicalDevice_;
            LogicalDevice       logicalDevice_;
            VkQueue             queue_ = VK_NULL_HANDLE;
            VkCommandPool       commandPool_ = VK_NULL_HANDLE;

            DeviceSetting       settings_;
            std::vector<const char* > enabledExtension_;
            std::vector<const char* > enabledLayerName_;
            std::vector<std::string> supportedExtensions_;
            PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallback = nullptr;
            PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback = nullptr;
            VkDebugReportCallbackEXT debugReportCallback = VK_NULL_HANDLE;

            Device();
            ~Device();

            void                                InitDevice();
            bool                                CreateInstance();
            bool                                EnableValidationLayer();
            bool                                PickUpAGPU();
            bool                                CreateLogicDevice();
            bool                                CreateGraphicsQueue();
            bool                                CreateCommandPool();


            
        public:
           
            static Device&                      Instance();

            void                                Init(DeviceSetting settings = DeviceSetting());
            bool                                CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void *data = nullptr, VkDeviceSize *actualBufferSize = nullptr);
            VkCommandBuffer                     CreateCommandBuffer(VkCommandBufferLevel level, bool begin = false);
            bool                                FlushCommandBuffer(VkCommandBuffer commandBuffer, bool free = true);


            void                                SetSettings(DeviceSetting settings);
    
            VkInstance                          GetInstanceHandle();
            VkPhysicalDevice                    GetPhysicalDeviceHandle();
            VkPhysicalDeviceFeatures            GetPhysicalDeviceFeatures();
            VkDevice                            GetLogicalDeviceHandle();
            VkQueue                             GetGraphicsQueue();
            uint32_t                            GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr);
            VkSampleCountFlagBits               GetMultiSampleCount();
            bool                                GetRequireStaging();
            VkPhysicalDeviceFeatures            GetEnableFeatures();
            VkPhysicalDeviceProperties          GetDeviceProperties();
            DeviceSetting                       GetSetting();

            void                                BeginCommandBuffer(VkCommandBuffer commandBuffer);


            const std::vector<std::string>&     GetSupportedExtension();

    };
    
}


