#include "engine/render/render_resource_registry.h"

namespace engine::render
{
    bool RenderResourceRegistry::HasImages(RenderResourceId id) const noexcept
    {
        return this->imageResources_.contains(id);
    }

    bool RenderResourceRegistry::HasBuffers(RenderResourceId id) const noexcept
    {
        return this->bufferResources_.contains(id);
    }

    std::vector<RenderImage>& RenderResourceRegistry::GetOrCreateImages(RenderResourceId id)
    {
        return this->imageResources_[id];
    }

    std::vector<core::Buffer>& RenderResourceRegistry::GetOrCreateBuffers(RenderResourceId id)
    {
        return this->bufferResources_[id];
    }

    const std::vector<RenderImage>& RenderResourceRegistry::GetImages(RenderResourceId id) const
    {
        return this->imageResources_.at(id);
    }

    const std::vector<core::Buffer>& RenderResourceRegistry::GetBuffers(RenderResourceId id) const
    {
        return this->bufferResources_.at(id);
    }

    std::vector<core::Buffer>& RenderResourceRegistry::GetBuffers(RenderResourceId id)
    {
        return this->bufferResources_.at(id);
    }

    void RenderResourceRegistry::ClearImages()
    {
        this->imageResources_.clear();
    }

    void RenderResourceRegistry::ClearBuffers()
    {
        this->bufferResources_.clear();
    }
}
