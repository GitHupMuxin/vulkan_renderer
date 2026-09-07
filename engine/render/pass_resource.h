#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

#include <vulkan/vulkan.h>

namespace engine::render
{
    // 资源描述：资源身份、自身属性与目录查询，不声明某个 Pass 的使用方式。
    enum class RenderResourceId
    {
        MainColor,
        MainDepth,

        MainCamera,
        SceneParam,

        EnvironmentCube,
        IrradianceMap,
        PrefilteredMap,
        BrdfLut,
        EuLut,
        EavgLut,

        MaterialTextures,
        MaterialBuffer,
        MeshDataBuffer,

        SceneColorHdr,
        BackBuffer,

        MainColorMsaa,
        // 保留旧主 framebuffer 的单采样深度槽；目前并未执行 depth resolve。
        MainDepthSingleSample
    };

    enum class RenderResourceLifetime
    {
        // 由 Swapchain、ResourceManager、Model 或 Pass 提供，Renderer 不重复创建和销毁。
        External,
        Persistent,
        Frame
    };

    // 多份实例与资源存活时间独立；PerSwapchainImage 必须按 imageIndex 选择。
    enum class RenderImageInstancePolicy
    {
        Single,
        PerSwapchainImage
    };

    struct RenderImageDescription
    {
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
        uint32_t mipLevels_ = 1;
        uint32_t arrayLayers_ = 1;
        // 当前只声明二维图像及 cubemap；Cube 要求 6 层和 cube-compatible 创建标记。
        VkImageViewType viewType_ = VK_IMAGE_VIEW_TYPE_2D;
        RenderImageInstancePolicy instancePolicy_ = RenderImageInstancePolicy::Single;
    };

    struct RenderBufferDescription
    {
        // 内部 Buffer 的字节数；外部 Buffer 由原所有者提供。
        VkDeviceSize size_ = 0;
    };

    struct RenderResourceDescription
    {
        RenderResourceId id_;
        std::string_view name_;
        RenderResourceLifetime lifetime_;
        std::variant<RenderImageDescription, RenderBufferDescription> description_;
    };

    // 返回进程期稳定的只读登记表；调用方不拥有底层存储。
    std::span<const RenderResourceDescription> GetRenderResourceDescriptions() noexcept;

    // 返回的指针在进程存续期间有效；未登记的 ID 返回 nullptr。
    const RenderResourceDescription* FindRenderResourceDescription(RenderResourceId id) noexcept;

    enum class ResourceUsage
    {
        UniformBuffer,
        StorageBuffer,
        SampledImage,
        ColorAttachment,
        DepthAttachment
    };

    struct PassResourceUsage
    {
        ResourceUsage type_;
        VkImageLayout requiredLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    // Input 是 Pass 从外部读取的 descriptor 资源。
    struct PassInputResource
    {
        RenderResourceId resource_;
        PassResourceUsage usage_;
    };

    struct PassColorAttachment
    {
        VkAttachmentLoadOp loadOp_ = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp storeOp_ = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkImageLayout initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout finalLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkClearColorValue clearValue_ = {{0.0f, 0.0f, 0.0f, 1.0f}};
    };

    struct PassDepthAttachment
    {
        VkAttachmentLoadOp loadOp_ = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp storeOp_ = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkAttachmentLoadOp stencilLoadOp_ = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp stencilStoreOp_ = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkImageLayout initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout finalLayout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkClearDepthStencilValue clearValue_ = {1.0f, 0};
    };

    // 当前 Output 只有 framebuffer attachment；其他输出类型有需求时再扩展。
    struct PassOutputResource
    {
        RenderResourceId resource_;
        std::variant<PassColorAttachment, PassDepthAttachment> attachment_;
    };

    inline PassResourceUsage GetPassResourceUsage(const PassInputResource& input) noexcept
    {
        return input.usage_;
    }

    inline PassResourceUsage GetPassResourceUsage(const PassOutputResource& output) noexcept
    {
        if (std::holds_alternative<PassColorAttachment>(output.attachment_))
        {
            return {ResourceUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        }

        return {ResourceUsage::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    }
}
