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
            // 按命令录制顺序维护的布局；不是 GPU 即时状态查询。
            VkImageLayout   currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
            // 由图像格式确定，采样等使用方式不会改变它。
            VkImageAspectFlags aspectMask_ = 0;

            RenderImage() = default;
            ~RenderImage();
            RenderImage(const RenderImage&) = delete;
            RenderImage& operator=(const RenderImage&) = delete;
            RenderImage(RenderImage&& other) noexcept;
            RenderImage& operator=(RenderImage&& other) noexcept;

            void Destroy();
    };

}
