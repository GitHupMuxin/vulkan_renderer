#include "engine/render/render_image.h"
#include "engine/core/device.h"

namespace engine::render
{
    RenderImage::~RenderImage()
    {
        this->Destroy();
    }

    RenderImage::RenderImage(RenderImage&& other) noexcept
        : image_(other.image_)
        , imageView_(other.imageView_)
        , memory_(other.memory_)
        , currentLayout_(other.currentLayout_)
        , aspectMask_(other.aspectMask_)
    {
        other.image_ = VK_NULL_HANDLE;
        other.imageView_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        other.aspectMask_ = 0;
    }

    RenderImage& RenderImage::operator=(RenderImage&& other) noexcept
    {
        if (this != &other) {
            this->Destroy();
            this->image_ = other.image_;
            this->imageView_ = other.imageView_;
            this->memory_ = other.memory_;
            this->currentLayout_ = other.currentLayout_;
            this->aspectMask_ = other.aspectMask_;
            other.image_ = VK_NULL_HANDLE;
            other.imageView_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
            other.currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
            other.aspectMask_ = 0;
        }
        return *this;
    }

    void RenderImage::Destroy()
    {
        this->currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        this->aspectMask_ = 0;
        auto& device = core::Device::Instance();
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device.GetLogicalDeviceHandle(), imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device.GetLogicalDeviceHandle(), image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device.GetLogicalDeviceHandle(), memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }
}
