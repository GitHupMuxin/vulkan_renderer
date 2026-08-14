#pragma once

#include <vulkan/vulkan.h>

namespace engine::render
{
    // Framebuffer 附件：包装一组关联的 Vulkan 图片资源（image + view + memory）。
    // move-only：拷贝会导致 double-free；Destroy 后句柄置空，可安全重复调用。
    class Attachment
    {
        public:
            VkImage         image_ = VK_NULL_HANDLE;
            VkImageView     imageView_ = VK_NULL_HANDLE;
            VkDeviceMemory  memory_ = VK_NULL_HANDLE;

            Attachment() = default;
            ~Attachment();
            Attachment(const Attachment&) = delete;
            Attachment& operator=(const Attachment&) = delete;
            Attachment(Attachment&& other) noexcept;
            Attachment& operator=(Attachment&& other) noexcept;

            void Destroy();
    };

    // 主渲染 Pass 的附件集合（MSAA color/depth + resolve depth）
    class MainRenderPassAttachmentList
    {
        public:
            Attachment colorAttachment_;
            Attachment depthAttachment_;
            Attachment multisampleColorAttachment_;
            Attachment multisampleDepthAttachment_;
    };
}
