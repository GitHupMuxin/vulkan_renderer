#include "engine/render/render_context.h"

#include "engine/utils/log.h"

namespace engine::render
{
    bool RenderContext::Init()
    {
        this->renderPassPointers_[0] = &this->renderPasses_.skyboxPass;
        this->renderPassPointers_[1] = &this->renderPasses_.pbrPass;
        this->renderPassPointers_[2] = &this->renderPasses_.toneMappingPass;

        return this->RebuildFrameGraph();
    }

    bool RenderContext::RebuildFrameGraph()
    {
        this->frameGraph_.Reset();

        std::array<FrameGraphNodeId, 3> nodeIds{};
        for (RenderPassIndex index = 0; index < this->renderPassPointers_.size(); index++)
        {
            RenderPass* renderPass = this->renderPassPointers_[index];
            nodeIds[index] = this->frameGraph_.AddPassNode(
                renderPass->GetName(),
                renderPass
            );

            if (nodeIds[index] == kInvalidFrameGraphNodeId)
            {
                this->frameGraph_.Reset();
                return false;
            }
        }

        // 顺序由 Context 显式决定，不根据 Request 自动推导依赖。
        this->frameGraph_.AddDependency(nodeIds[0], nodeIds[1], RenderResourceId::SceneColorHdr);
        this->frameGraph_.AddDependency(nodeIds[1], nodeIds[2], RenderResourceId::SceneColorHdr);

        return this->frameGraph_.Rebuild();
    }

    const FrameGraph& RenderContext::GetFrameGraph() const noexcept
    {
        return this->frameGraph_;
    }

    std::span<const RenderResourceDescription> RenderContext::GetResourceDescriptions() const noexcept
    {
        return this->resourceDescriptions_;
    }

    RenderPass* RenderContext::GetRenderPass(RenderPassIndex renderPassIndex) const noexcept
    {
        if (renderPassIndex >= this->renderPassPointers_.size())
        {
            return nullptr;
        }

        return this->renderPassPointers_[renderPassIndex];
    }
}
