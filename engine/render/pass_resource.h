#pragma once

#include <span>
#include <string_view>

#include <vulkan/vulkan.h>

namespace engine::render
{
    enum class RenderResourceId
    {
        MainColor,
        MainDepth,

        CameraMatrices,
        RenderParams,

        EnvironmentCube,
        IrradianceMap,
        PrefilteredMap,
        BrdfLut,
        EuLut,
        EavgLut,

        MaterialTextures,
        MaterialBuffer,
        MeshDataBuffer
    };

    enum class ResourceAccess
    {
        Read,
        Write,
        ReadWrite
    };

    enum class ResourceUsage
    {
        UniformBuffer,
        StorageBuffer,
        SampledImage,
        ColorAttachment,
        DepthAttachment
    };

    enum class RenderResourceType
    {
        Image,
        Buffer,
        ImageCollection,
        BufferCollection
    };

    enum class RenderResourceLifetime
    {
        External,
        Persistent,
        Frame
    };

    struct PassResourceUsage
    {
        RenderResourceId resource_;
        ResourceAccess access_;
        ResourceUsage usage_;
        VkImageLayout requiredLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct RenderResourceDescription
    {
        RenderResourceId id_;
        std::string_view name_;
        RenderResourceType type_;
        RenderResourceLifetime lifetime_;
    };

    // 返回进程期稳定的只读登记表；调用方不拥有底层存储。
    std::span<const RenderResourceDescription> GetRenderResourceDescriptions() noexcept;

    // 返回的指针在进程存续期间有效；未登记的 ID 返回 nullptr。
    const RenderResourceDescription* FindRenderResourceDescription(RenderResourceId id) noexcept;
}
