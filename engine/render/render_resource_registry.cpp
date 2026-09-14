#include "engine/render/render_resource_registry.h"

#include <filesystem>
#include <stdexcept>
#include "engine/core/device.h"
#include "engine/render/config/graphics_pipeline_defaults.h"
#include "engine/resource/resource_manager.h"

namespace engine::render
{
    RenderResourceRegistry::~RenderResourceRegistry()
    {
        this->ClearImages();
    }

    void RenderResourceRegistry::RequireResource(RenderResourceReference reference)
    {
        const auto* resource = FindRenderResourceDescription(reference.id_);
        if (!resource)
            throw std::invalid_argument("Unknown resource in Pass binding");

        if (std::holds_alternative<RenderBufferDescription>(resource->description_))
        {
            if (reference.environmentTexture_ != EnvironmentTexture::Source)
                throw std::invalid_argument("Buffer bindings must select Source");
            if (!this->HasBuffers(reference.id_) || this->GetBuffers(reference.id_).empty())
                throw std::logic_error("Buffer must be provided before preparing Pass bindings");
            return;
        }

        auto& manager = resource::ResourceManager::Instance();
        if (const auto* image = std::get_if<RenderImageDescription>(&resource->description_))
        {
            if (reference.environmentTexture_ != EnvironmentTexture::Source)
                throw std::invalid_argument("Single image bindings must select Source");
            if (!this->HasImages(reference.id_))
            {
                if (!image->path_)
                    throw std::logic_error("Image has no file path and has not been provided");
                if (resource->lifetime_ != RenderResourceLifetime::External ||
                    image->instancePolicy_ != RenderImageInstancePolicy::Single)
                    throw std::invalid_argument("File images must be external, single-instance resources");

                const auto path = std::filesystem::path(manager.assetPath_) / image->path_;
                this->ImportImage(reference.id_, manager.LoadTexture(path.string(), image->format_, image->viewType_));
            }
        }
        else if (const auto* environment = std::get_if<EnvironmentDescription>(&resource->description_))
        {
            if (!this->HasImages(reference.id_))
            {
                if (!environment->path_)
                    throw std::logic_error("Environment has no file path and has not been provided");
                if (resource->lifetime_ != RenderResourceLifetime::External)
                    throw std::invalid_argument("File environments must be external resources");

                const auto path = std::filesystem::path(manager.assetPath_) / environment->path_;
                this->ImportEnvironmentImage(reference.id_, manager.LoadEnvironment(path.string()));
            }
        }

        // 已登记的 Handle 仍需校验有效性，并确认可以解析所选成员。
        (void)this->GetImage(reference);
    }

    bool RenderResourceRegistry::HasImages(RenderResourceId id) const noexcept
    {
        return this->imageResources_.contains(id) || this->externalImages_.contains(id);
    }

    bool RenderResourceRegistry::HasBuffers(RenderResourceId id) const noexcept
    {
        return this->bufferResources_.contains(id);
    }

    std::vector<RenderImage>& RenderResourceRegistry::GetOrCreateImages(RenderResourceId id)
    {
        if (this->externalImages_.contains(id) || this->bufferResources_.contains(id))
            throw std::logic_error("Resource ID is already bound to an external image or buffer");
        return this->imageResources_[id];
    }

    RenderImage& RenderResourceRegistry::GetInternalImage(RenderResourceId id, uint32_t index)
    {
        return this->imageResources_.at(id).at(index);
    }

    std::vector<core::Buffer>& RenderResourceRegistry::GetOrCreateBuffers(RenderResourceId id)
    {
        if (this->HasImages(id))
            throw std::logic_error("Resource ID is already bound to an image");
        return this->bufferResources_[id];
    }

    void RenderResourceRegistry::ImportImage(RenderResourceId id, resource::TextureHandle handle)
    {
        const auto* resource = FindRenderResourceDescription(id);
        if (!resource || !std::holds_alternative<RenderImageDescription>(resource->description_))
            throw std::invalid_argument("Resource description must identify a single image");
        if (this->imageResources_.contains(id) || this->bufferResources_.contains(id))
            throw std::logic_error("Resource ID is already bound to an internal resource");
        if (!resource::ResourceManager::Instance().GetTexture(handle))
            throw std::invalid_argument("Cannot import an invalid or unready texture handle");
        this->externalImages_[id] = handle;
    }

    void RenderResourceRegistry::ImportEnvironmentImage(RenderResourceId id, resource::EnvironmentCubeMapHandle handle)
    {
        const auto* resource = FindRenderResourceDescription(id);
        if (!resource || !std::holds_alternative<EnvironmentDescription>(resource->description_))
            throw std::invalid_argument("Resource description must identify an environment");
        if (this->imageResources_.contains(id) || this->bufferResources_.contains(id))
            throw std::logic_error("Resource ID is already bound to an internal resource");
        if (!resource::ResourceManager::Instance().GetEnvironmentCubeMap(handle))
            throw std::invalid_argument("Cannot import an invalid or unready environment handle");
        this->externalImages_[id] = handle;
    }

    void RenderResourceRegistry::CreateDefaultSampler()
    {
        if (this->sampler_ != VK_NULL_HANDLE) return;

        auto& device = core::Device::Instance();
        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(device.GetLogicalDeviceHandle(), &graphics_pipeline_defaults::kSampler, nullptr, &sampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create the render resource default sampler");
        this->sampler_ = sampler;
        device.SetObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler),
            "RenderResourceRegistry Default Sampler");
    }

    void RenderResourceRegistry::SetCurrentImageIndex(uint32_t index) noexcept
    {
        this->currentImageIndex_ = index;
    }

    RenderImageView RenderResourceRegistry::GetCurrentImage(RenderResourceReference reference) const
    {
        return this->GetImage(reference, this->GetImageCount(reference.id_) == 1 ? 0 : this->currentImageIndex_);
    }

    RenderImageView RenderResourceRegistry::GetCurrentImage(RenderResourceId id) const
    {
        return this->GetCurrentImage(RenderResourceReference{id});
    }

    uint32_t RenderResourceRegistry::GetPrefilteredCubeMipLevels(RenderResourceId id) const
    {
        const auto handle = std::get<resource::EnvironmentCubeMapHandle>(this->externalImages_.at(id));
        auto* environment = resource::ResourceManager::Instance().GetEnvironmentCubeMap(handle);
        if (!environment)
        {
            throw std::runtime_error("Stale environment handle in RenderResourceRegistry");
        }
        return environment->GetPrefilteredCubeMipLevels();
    }

    uint32_t RenderResourceRegistry::GetImageCount(RenderResourceId id) const
    {
        if (this->externalImages_.contains(id)) return 1;
        return static_cast<uint32_t>(this->imageResources_.at(id).size());
    }

    RenderImageView RenderResourceRegistry::GetImage(RenderResourceId id, uint32_t index) const
    {
        return this->GetImage(RenderResourceReference{id}, index);
    }

    RenderImageView RenderResourceRegistry::GetImage(RenderResourceReference reference, uint32_t index) const
    {
        const auto id = reference.id_;
        if (const auto found = this->externalImages_.find(id); found != this->externalImages_.end())
        {
            if (index != 0) throw std::out_of_range("Imported texture has only one instance");
            auto& manager = resource::ResourceManager::Instance();
            const resource::Texture* texture = nullptr;
            if (const auto* handle = std::get_if<resource::TextureHandle>(&found->second))
            {
                if (reference.environmentTexture_ != EnvironmentTexture::Source)
                    throw std::invalid_argument("Single image bindings must select Source");
                texture = manager.GetTexture(*handle);
            }
            else
            {
                const auto* environment = manager.GetEnvironmentCubeMap(
                    std::get<resource::EnvironmentCubeMapHandle>(found->second));
                if (!environment) throw std::runtime_error("Stale environment handle in RenderResourceRegistry");
                switch (reference.environmentTexture_)
                {
                    case EnvironmentTexture::Source: texture = &environment->environmentCube_; break;
                    case EnvironmentTexture::Irradiance: texture = &environment->irradianceCube_; break;
                    case EnvironmentTexture::Prefiltered: texture = &environment->prefilteredCube_; break;
                    default: throw std::invalid_argument("Invalid environment texture member");
                }
            }
            if (!texture) throw std::runtime_error("Stale external texture handle in RenderResourceRegistry");
            return {texture->image_, texture->view_, texture->sampler_};
        }
        if (reference.environmentTexture_ != EnvironmentTexture::Source)
            throw std::invalid_argument("Internal image bindings must select Source");
        const auto& image = this->imageResources_.at(id).at(index);
        return {image.image_, image.imageView_, this->sampler_};
    }

    const std::vector<core::Buffer>& RenderResourceRegistry::GetBuffers(RenderResourceId id) const
    {
        return this->bufferResources_.at(id);
    }

    std::vector<core::Buffer>& RenderResourceRegistry::GetBuffers(RenderResourceId id)
    {
        return this->bufferResources_.at(id);
    }

    void RenderResourceRegistry::ClearInternalImages()
    {
        this->imageResources_.clear();
    }

    void RenderResourceRegistry::ClearImages()
    {
        this->ClearInternalImages();
        this->externalImages_.clear();
        if (this->sampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(core::Device::Instance().GetLogicalDeviceHandle(), this->sampler_, nullptr);
            this->sampler_ = VK_NULL_HANDLE;
        }
    }

    void RenderResourceRegistry::ClearBuffers()
    {
        this->bufferResources_.clear();
    }
}
