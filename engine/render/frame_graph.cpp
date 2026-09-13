#include "engine/render/frame_graph.h"

#include <queue>
#include <utility>
#include <unordered_map>

#include "engine/render/render_pass.h"
#include "engine/utils/log.h"

namespace engine::render
{
    namespace
    {
        bool IsAttachmentUsage(ResourceUsage usage) noexcept
        {
            return usage == ResourceUsage::ColorAttachment || usage == ResourceUsage::DepthAttachment;
        }
    }

    void FrameGraph::Reset()
    {
        this->nodes_.clear();
        this->nodeDependencies_.clear();
        this->executionPlan_.Reset();
        this->needsRebuild_ = true;
    }

    FrameGraphNodeId FrameGraph::AddPassNode(std::string_view name, RenderPass* renderPass)
    {
        // Node ID 仅在本次建图内有效；排序使用从 0 开始的连续编号。
        FrameGraphPassNode node;
        node.nodeId_ = static_cast<FrameGraphNodeId>(this->nodes_.size());
        node.name_ = std::string(name);
        node.renderPass_ = renderPass;
        this->nodes_.insert({node.nodeId_, std::move(node)});
        this->needsRebuild_ = true;

        return this->nodes_.at(node.nodeId_).nodeId_;
    }

    bool FrameGraph::HasNode(FrameGraphNodeId nodeId) const noexcept
    {
        return this->nodes_.find(nodeId) != this->nodes_.end();
    }

    void FrameGraph::AddDependency(FrameGraphNodeId sourceNodeId, FrameGraphNodeId destinationNodeId, RenderResourceReference resource)
    {
        if (!this->HasNode(sourceNodeId) || !this->HasNode(destinationNodeId) || sourceNodeId == destinationNodeId)
        {
            LOG_ERROR("FrameGraph: cannot add an invalid pass dependency.");
            return;
        }

        this->nodeDependencies_[sourceNodeId].push_back(FrameGraphEdgeDependency{
            .fromNodeId_ = sourceNodeId,
            .toNodeId_ = destinationNodeId,
            .resource_ = resource
        });

        this->needsRebuild_ = true;
    }

    bool FrameGraph::Rebuild()
    {
        if (!this->needsRebuild_)
        {
            return this->executionPlan_.valid_;
        }

        if (!this->BuildExecutionPlan())
        {
            return false;
        }

        this->needsRebuild_ = false;
        return true;
    }

    bool FrameGraph::NeedsRebuild() const noexcept
    {
        return this->needsRebuild_;
    }

    bool FrameGraph::BuildExecutionPlan()
    {
        this->executionPlan_.Reset();

        std::unordered_map<FrameGraphNodeId, uint32_t> nodeIdIn;
        // std::unordered_map<FrameGraphNodeId, uint32_t> nodeIdOut;
        std::unordered_map<FrameGraphNodeId, std::vector<FrameGraphEdgeDependency>> incomingEdges;

        for (const auto& [destinationNodeId, dependencies] : this->nodeDependencies_)
        {
            for (const auto& edge : dependencies)
            {
                if (edge.fromNodeId_ >= this->nodes_.size() || edge.toNodeId_ >= this->nodes_.size())
                {
                    LOG_ERROR("FrameGraph: edge contains an invalid FrameGraphNodeId.");
                    return false;
                }

                // nodeIdOut[edge.fromNodeId_]++;
                nodeIdIn[edge.toNodeId_]++;
            }
        }

        std::queue<FrameGraphNodeId> readyNodeIds;

        for (FrameGraphNodeId nodeId = 0; nodeId < this->nodes_.size(); nodeId++)
        {
            if (nodeIdIn[nodeId] == 0)
            {
                readyNodeIds.push(nodeId);
            }
        }

        if (readyNodeIds.empty())
        {
            LOG_ERROR("FrameGraph: no nodes are ready to execute; dependency cycle detected.");
            return false;
        }

        while (!readyNodeIds.empty())
        {
            const FrameGraphNodeId nodeId = readyNodeIds.front();
            readyNodeIds.pop();

            this->executionPlan_.passes_.push_back(CompiledPass{
                .nodeId_ = nodeId
            });

            for (const auto& edge : this->nodeDependencies_[nodeId])
            {
                incomingEdges[edge.toNodeId_].push_back(edge);

                nodeIdIn[edge.toNodeId_]--;
                if (nodeIdIn[edge.toNodeId_] == 0)
                {
                    readyNodeIds.push(edge.toNodeId_);
                }
            }

            auto& incoming = incomingEdges[nodeId];

            for (auto& dependency : incoming)
            {
                PassResourceUsage srcUsage = this->nodes_[dependency.fromNodeId_].renderPass_->GetResourceUsage(dependency.resource_);
                PassResourceUsage dstUsage = this->nodes_[dependency.toNodeId_].renderPass_->GetResourceUsage(dependency.resource_);
                if (IsAttachmentUsage(srcUsage.type_) && IsAttachmentUsage(dstUsage.type_))
                {
                    this->executionPlan_.passes_.back().attachmentDependencies_.push_back(CompiledAttachmentDependency{
                        .resource_ = dependency.resource_,
                        .srcUsage_ = srcUsage,
                        .dstUsage_ = dstUsage
                    });
                }
                else
                {
                    this->executionPlan_.passes_.back().barriersBefore_.push_back(CompiledResourceBarrier{
                        .resource_ = dependency.resource_,
                        .srcUsage_ = srcUsage,
                        .dstUsage_ = dstUsage
                    });
                }

            }
        }

        if (this->executionPlan_.passes_.size() != this->nodes_.size())
        {
            LOG_ERROR("FrameGraph: dependency cycle detected; no valid execution plan exists.");
            this->executionPlan_.Reset();
            return false;
        }

        this->executionPlan_.valid_ = true;

        LOG_INFO(
            "FrameGraph: built execution plan for " << this->nodes_.size()
            << " nodes and " << this->nodeDependencies_.size() << " nodeDependencies."
        );

        for (const auto& [nodeId, dependencies] : this->nodeDependencies_)
        {
            for (const auto& dependency : dependencies)
            {
                const RenderResourceDescription* resource = FindRenderResourceDescription(dependency.resource_.id_);
                LOG_DEBUG(
                    "FrameGraph: " << this->nodes_[dependency.fromNodeId_].name_ << " -> "
                    << this->nodes_[dependency.toNodeId_].name_ << " via "
                    << (resource ? resource->name_ : "UnknownResource")
                );
            }
        }

        return true;
    }

    const FrameGraphPassNode& FrameGraph::GetNode(FrameGraphNodeId nodeId) const
    {
        return this->nodes_.at(nodeId);
    }

    const FrameGraphExecutionPlan& FrameGraph::GetExecutionPlan() const noexcept
    {
        return this->executionPlan_;
    }
}
