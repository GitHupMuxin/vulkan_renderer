#pragma once

#include <vulkan/vulkan.h>

namespace engine::render
{
    // 拥有内部图像及其 view / memory；attachment 是使用方式，不是独立资源类型。
    // 不接管 Swapchain 或 Model 等外部图像。
    // move-only：拷贝会导致 double-free；Destroy 后句柄置空，可安全重复调用。
    class RenderImage
    {
        public:
            VkImage         image_ = VK_NULL_HANDLE;
            VkImageView     imageView_ = VK_NULL_HANDLE;
            VkDeviceMemory  memory_ = VK_NULL_HANDLE;

            RenderImage() = default;
            ~RenderImage();
            RenderImage(const RenderImage&) = delete;
            RenderImage& operator=(const RenderImage&) = delete;
            RenderImage(RenderImage&& other) noexcept;
            RenderImage& operator=(RenderImage&& other) noexcept;

            void Destroy();
    };

}
