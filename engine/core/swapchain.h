#pragma once

#include <vulkan/vulkan.h>

namespace engine::core
{
    struct SwapChainBuffer {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    class SwapChain
    {
        private:
            VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
            VkInstance instance_ = VK_NULL_HANDLE;
            VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
            VkDevice device_ = VK_NULL_HANDLE;
            
            VkSurfaceKHR surface_ = VK_NULL_HANDLE;

            VkExtent2D extent_{};
            VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
            VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
            VkColorSpaceKHR colorSpace_{};
            uint32_t imageCount_ = 0;
            std::vector<VkImage> images_;
            std::vector<SwapChainBuffer> buffers_;

            uint32_t queueNodeIndex_ = UINT32_MAX;

            // Function pointers
            PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR_ = nullptr;
            PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR_ = nullptr;
            PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR_ = nullptr;
            PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR_ = nullptr;
	    	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR_ = nullptr;
    		PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR_ = nullptr;
    		PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR_ = nullptr;
    		PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR_ = nullptr;
		    PFN_vkQueuePresentKHR fpQueuePresentKHR_ = nullptr;
        public:
            SwapChain();
            ~SwapChain();

            void Connect(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);

            void CreateSurface(void* platformHendle, void* platformWindow);

            void CreateSwapChain(uint32_t *width, uint32_t *height, bool vsync = false);

            void SelectDepthFormat();

            VkResult AcquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t* imageIndex);

            VkResult QueuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore);

            void Destroy();

            uint32_t GetQueueNodeIndex();
            uint32_t GetImageCount();
            SwapChainBuffer& GetSwapChainBuffer(uint32_t index);
            VkFormat GetColorFormat();
            VkFormat GetDepthFormat();
            VkExtent2D GetExtent();
    };
}







