#pragma once

#include <memory>
#include <span>
#include <vector>

#include "engine/render/frame_graph.h"
#include "engine/render/config/pass_resource.h"
#include "engine/render/render_pass.h"
#include "engine/render/render_pipeline_description.h"

namespace engine::render
{
    class RenderContext
    {
        private:
            RenderPipelineDescription                pipelineDescription_;
            std::vector<std::unique_ptr<RenderPass>>   renderPasses_;
            std::span<const RenderResourceDescription> resourceDescriptions_ = GetRenderResourceDescriptions();
            FrameGraph frameGraph_;

        public:
            RenderContext() = default;
            ~RenderContext() = default;

            RenderContext(const RenderContext&) = delete;
            RenderContext& operator=(const RenderContext&) = delete;
            RenderContext(RenderContext&&) = delete;
            RenderContext& operator=(RenderContext&&) = delete;

            bool Init(const RenderPipelineDescription& pipelineDescription);

            bool RebuildFrameGraph();

            const FrameGraph& GetFrameGraph() const noexcept;
            std::span<const RenderResourceDescription> GetResourceDescriptions() const noexcept;

            RenderPass* GetRenderPass(RenderPassIndex renderPassIndex) const noexcept;
    };
}
