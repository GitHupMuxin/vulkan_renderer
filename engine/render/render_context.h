#pragma once

#include <array>
#include <memory>
#include <span>

#include "engine/render/frame_graph.h"
#include "engine/render/pass_resource.h"
#include "engine/render/render_pass.h"

namespace engine::render
{
    struct RenderPasses
    {
        SkyBoxRenderPass skyboxPass;
        PBRRenderPass pbrPass;
        ToneMappingRenderPass toneMappingPass;
    };

    class RenderContext
    {
        private:
            RenderPasses renderPasses_;
            std::array<RenderPass*, 3> renderPassPointers_;
            std::span<const RenderResourceDescription> resourceDescriptions_ = GetRenderResourceDescriptions();
            FrameGraph frameGraph_;

        public:
            RenderContext() = default;
            ~RenderContext() = default;

            RenderContext(const RenderContext&) = delete;
            RenderContext& operator=(const RenderContext&) = delete;
            RenderContext(RenderContext&&) = delete;
            RenderContext& operator=(RenderContext&&) = delete;

            bool Init();

            bool RebuildFrameGraph();

            const FrameGraph& GetFrameGraph() const noexcept;
            std::span<const RenderResourceDescription> GetResourceDescriptions() const noexcept;

            RenderPass* GetRenderPass(RenderPassIndex renderPassIndex) const noexcept;
    };
}
