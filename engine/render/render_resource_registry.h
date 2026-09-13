#pragma once

#include <unordered_map>
#include <variant>
#include <vector>

#include "engine/core/buffer.h"
#include "engine/render/config/pass_resource.h"
#include "engine/render/render_image.h"
#include "engine/resource/resource_manager.h"

namespace engine::render
{
    // Borrowed Vulkan handles only; this view never owns or destroys a resource.
    struct RenderImageView
    {
        VkImage image_ = VK_NULL_HANDLE;
        VkImageView imageView_ = VK_NULL_HANDLE;
        VkSampler sampler_ = VK_NULL_HANDLE;
    };

    class RenderResourceRegistry final
    {
        private:
            std::unordered_map<RenderResourceId, std::vector<RenderImage>> imageResources_;
            std::unordered_map<RenderResourceId, std::vector<core::Buffer>> bufferResources_;
            using ExternalImageHandle = std::variant<resource::TextureHandle, resource::EnvironmentCubeMapHandle>;
            std::unordered_map<RenderResourceId, ExternalImageHandle> externalImages_;
            uint32_t currentImageIndex_ = 0;
            VkSampler sampler_ = VK_NULL_HANDLE;

        public:
            RenderResourceRegistry() = default;
            ~RenderResourceRegistry();
            RenderResourceRegistry(const RenderResourceRegistry&) = delete;
            RenderResourceRegistry& operator=(const RenderResourceRegistry&) = delete;

            // 绑定准备：复用已有资源；缺少时按配置加载外部文件并登记 Handle。
            // 无路径的内部资源必须先由所有者创建。
            void RequireResource(RenderResourceReference reference);

            bool HasImages(RenderResourceId id) const noexcept;
            bool HasBuffers(RenderResourceId id) const noexcept;

            // Internal resources are owned here. An ID cannot have two owners/types.
            std::vector<RenderImage>& GetOrCreateImages(RenderResourceId id);
            std::vector<core::Buffer>& GetOrCreateBuffers(RenderResourceId id);
            // Imports a Manager-owned texture without taking ownership.
            void ImportImage(RenderResourceId id, resource::TextureHandle handle);
            void ImportEnvironmentImage(RenderResourceId id, resource::EnvironmentCubeMapHandle handle);

            void CreateDefaultSampler();
            void SetCurrentImageIndex(uint32_t index) noexcept;
            RenderImageView GetCurrentImage(RenderResourceReference reference) const;
            RenderImageView GetCurrentImage(RenderResourceId id) const;

            // 从该 ID 已登记的环境资源读取生成时保存的层数；不触发加载。
            uint32_t GetPrefilteredCubeMipLevels(RenderResourceId id) const;
            uint32_t GetImageCount(RenderResourceId id) const;
            RenderImageView GetImage(RenderResourceReference reference, uint32_t index = 0) const;
            RenderImageView GetImage(RenderResourceId id, uint32_t index = 0) const;
            const std::vector<core::Buffer>& GetBuffers(RenderResourceId id) const;
            std::vector<core::Buffer>& GetBuffers(RenderResourceId id);

            // Resize destroys only internally owned images; the default sampler survives.
            void ClearInternalImages();
            // Full cleanup destroys the default sampler and drops imports, not the imported assets.
            void ClearImages();
            void ClearBuffers();
    };
}
