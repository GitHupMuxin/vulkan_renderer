#pragma once

#include <unordered_map>
#include <vector>

#include "engine/core/buffer.h"
#include "engine/render/pass_resource.h"
#include "engine/render/render_image.h"

namespace engine::render
{
    class RenderResourceRegistry final
    {
        private:
            std::unordered_map<RenderResourceId, std::vector<RenderImage>> imageResources_;
            std::unordered_map<RenderResourceId, std::vector<core::Buffer>> bufferResources_;

        public:
            bool HasImages(RenderResourceId id) const noexcept;
            bool HasBuffers(RenderResourceId id) const noexcept;

            std::vector<RenderImage>& GetOrCreateImages(RenderResourceId id);
            std::vector<core::Buffer>& GetOrCreateBuffers(RenderResourceId id);

            const std::vector<RenderImage>& GetImages(RenderResourceId id) const;
            const std::vector<core::Buffer>& GetBuffers(RenderResourceId id) const;
            std::vector<core::Buffer>& GetBuffers(RenderResourceId id);

            void ClearImages();
            void ClearBuffers();
    };
}
