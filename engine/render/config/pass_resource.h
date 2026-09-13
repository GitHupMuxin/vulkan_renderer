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

        // 整组环境资源；绑定时再选择其中的纹理。
        Environment,
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

    enum class EnvironmentTexture
    {
        Source,
        Irradiance,
        Prefiltered
    };

    struct RenderResourceReference
    {
        RenderResourceId id_;
        // Source：普通资源自身，或环境资源的源 Cubemap。
        EnvironmentTexture environmentTexture_ = EnvironmentTexture::Source;
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
        // nullptr 表示不从文件加载；非空路径相对 data 目录，由 Manager 加载并持有。
        const char* path_ = nullptr;
    };

    // 描述一组环境资源：源 Cubemap，以及由它生成的 Irradiance、Prefiltered。
    struct EnvironmentDescription
    {
        // 源 Cubemap 文件路径，相对 data 目录；nullptr 表示没有外部文件。
        const char* path_ = nullptr;
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
        std::variant<RenderImageDescription, RenderBufferDescription, EnvironmentDescription> description_;
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
        RenderResourceReference resource_;
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
        RenderResourceReference resource_;
        PassResourceUsage usage_;
        std::variant<PassColorAttachment, PassDepthAttachment> attachment_;
    };
}
