#include "engine/render/render_context.h"

#include "engine/utils/log.h"

namespace engine::render
{
    bool RenderContext::Init(std::span<const RenderPassDescription> pipelineDescriptions)
    {
        this->pipelineDescriptions_.assign(pipelineDescriptions.begin(), pipelineDescriptions.end());
        this->renderPasses_.clear();
        this->renderPasses_.reserve(this->pipelineDescriptions_.size());

        for (const RenderPassDescription& description : this->pipelineDescriptions_)
        {
            this->renderPasses_.push_back(std::make_unique<RenderPass>(description));
        }

        return this->RebuildFrameGraph();
    }

    bool RenderContext::RebuildFrameGraph()
    {
        this->frameGraph_.Reset();

        std::vector<FrameGraphNodeId> nodeIds;
        nodeIds.reserve(this->renderPasses_.size());
        for (RenderPassIndex index = 0; index < this->renderPasses_.size(); ++index)
        {
            RenderPass* renderPass = this->renderPasses_[index].get();
            const FrameGraphNodeId nodeId = this->frameGraph_.AddPassNode(
                this->pipelineDescriptions_[index].name_,
                renderPass
            );
            nodeIds.push_back(nodeId);

            if (nodeId == kInvalidFrameGraphNodeId)
            {
                this->frameGraph_.Reset();
                return false;
            }
        }

        // Dependency 仍由 Context 显式声明，暂不从 Input/Output 自动推导。
        this->frameGraph_.AddDependency(nodeIds[0], nodeIds[1], {RenderResourceId::SceneColorHdr});
        this->frameGraph_.AddDependency(nodeIds[1], nodeIds[2], {RenderResourceId::SceneColorHdr});

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
        if (renderPassIndex >= this->renderPasses_.size())
        {
            return nullptr;
        }

        return this->renderPasses_[renderPassIndex].get();
    }
}
