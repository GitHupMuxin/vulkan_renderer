#include "engine/render/render_context.h"

#include <algorithm>
#include <unordered_map>

#include "engine/utils/log.h"

namespace engine::render
{
    namespace
    {
        bool DeclaresResource(const RenderPassDescription& pass, RenderResourceReference resource)
        {
            const auto matches = [resource](const auto& declaration) {
                return declaration.resource_.id_ == resource.id_ &&
                    declaration.resource_.environmentTexture_ == resource.environmentTexture_;
            };
            return std::any_of(pass.inputs_.begin(), pass.inputs_.end(), matches) ||
                std::any_of(pass.outputs_.begin(), pass.outputs_.end(), matches);
        }
    }

    bool RenderContext::Init(const RenderPipelineDescription& pipelineDescription)
    {
        this->frameGraph_.Reset();
        this->renderPasses_.clear();
        this->pipelineDescription_ = pipelineDescription;
        this->renderPasses_.reserve(this->pipelineDescription_.passes_.size());

        for (const RenderPassDescription& description : this->pipelineDescription_.passes_)
        {
            this->renderPasses_.push_back(std::make_unique<RenderPass>(description));
        }

        return this->RebuildFrameGraph();
    }

    bool RenderContext::RebuildFrameGraph()
    {
        this->frameGraph_.Reset();

        const auto& descriptions = this->pipelineDescription_.passes_;
        std::unordered_map<std::string_view, RenderPassIndex> passIndices;
        for (RenderPassIndex index = 0; index < descriptions.size(); ++index)
        {
            const auto name = descriptions[index].name_;
            // 名称是连接定位 Pass 的依据，拒绝空名称和重名。
            if (name.empty() || !passIndices.emplace(name, index).second)
            {
                LOG_ERROR("RenderContext: pass names must be non-empty and unique: " << name);
                return false;
            }
        }

        for (const auto& dependency : this->pipelineDescription_.dependencies_)
        {
            const auto source = passIndices.find(dependency.sourcePass_);
            const auto destination = passIndices.find(dependency.destinationPass_);
            // 连接中填写的来源和目标必须对应已配置的 Pass。
            if (source == passIndices.end() || destination == passIndices.end())
            {
                LOG_ERROR("RenderContext: dependency references an unknown pass: "
                    << dependency.sourcePass_ << " -> " << dependency.destinationPass_);
                return false;
            }
            // 拒绝节点依赖自身；跨节点的循环依赖由 FrameGraph 拓扑排序检查。
            if (source->second == destination->second)
            {
                LOG_ERROR("RenderContext: pass cannot depend on itself: " << dependency.sourcePass_);
                return false;
            }
            // 节点与连接分别配置，检查两端是否都声明使用了连线指定的资源。
            if (!DeclaresResource(descriptions[source->second], dependency.resource_) ||
                !DeclaresResource(descriptions[destination->second], dependency.resource_))
            {
                LOG_ERROR("RenderContext: dependency resource must be declared by both passes: "
                    << dependency.sourcePass_ << " -> " << dependency.destinationPass_);
                return false;
            }
        }

        for (const auto name : this->pipelineDescription_.outputPasses_)
        {
            // 输出名称必须对应已配置的 Pass，避免漏标起点后误剔除其依赖链。
            if (!passIndices.contains(name))
            {
                LOG_ERROR("RenderContext: output references an unknown pass: " << name);
                return false;
            }
        }

        std::vector<FrameGraphNodeId> nodeIds;
        nodeIds.reserve(this->renderPasses_.size());
        for (RenderPassIndex index = 0; index < this->renderPasses_.size(); ++index)
        {
            const FrameGraphNodeId nodeId = this->frameGraph_.AddPassNode(
                descriptions[index].name_,
                this->renderPasses_[index].get()
            );
            nodeIds.push_back(nodeId);

            // 节点创建失败时清空已添加的节点，避免保留不完整的图。
            if (nodeId == kInvalidFrameGraphNodeId)
            {
                this->frameGraph_.Reset();
                return false;
            }
        }

        for (const auto& dependency : this->pipelineDescription_.dependencies_)
        {
            this->frameGraph_.AddDependency(
                nodeIds[passIndices.at(dependency.sourcePass_)],
                nodeIds[passIndices.at(dependency.destinationPass_)],
                dependency.resource_
            );
        }

        std::vector<FrameGraphNodeId> outputNodeIds;
        outputNodeIds.reserve(this->pipelineDescription_.outputPasses_.size());
        for (const auto name : this->pipelineDescription_.outputPasses_)
        {
            outputNodeIds.push_back(nodeIds[passIndices.at(name)]);
        }
        this->frameGraph_.SetOutputNodes(outputNodeIds);

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
