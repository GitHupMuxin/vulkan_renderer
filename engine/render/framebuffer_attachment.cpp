#include "engine/render/framebuffer_attachment.h"
#include "engine/core/device.h"

namespace engine::render
{
    Attachment::~Attachment()
    {
        this->Destroy();
    }

    Attachment::Attachment(Attachment&& other) noexcept
        : image_(other.image_)
        , imageView_(other.imageView_)
        , memory_(other.memory_)
    {
        other.image_ = VK_NULL_HANDLE;
        other.imageView_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }

    Attachment& Attachment::operator=(Attachment&& other) noexcept
    {
        if (this != &other) {
            this->Destroy();
            this->image_ = other.image_;
            this->imageView_ = other.imageView_;
            this->memory_ = other.memory_;
            other.image_ = VK_NULL_HANDLE;
            other.imageView_ = VK_NULL_HANDLE;
            other.memory_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Attachment::Destroy()
    {
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
