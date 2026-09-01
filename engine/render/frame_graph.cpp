#include "engine/render/frame_graph.h"

#include <queue>
#include <utility>

#include "engine/utils/log.h"

namespace engine::render
{
    namespace
    {
        const char* GetHazardName(ResourceHazard hazard) noexcept
        {
            switch (hazard)
            {
                case ResourceHazard::ReadAfterWrite: return "RAW";
                case ResourceHazard::WriteAfterRead: return "WAR";
                case ResourceHazard::WriteAfterWrite: return "WAW";
            }

            return "Unknown";
        }
    }

    void FrameGraph::Reset()
    {
        this->nodes_.clear();
        this->edges_.clear();
        this->executionPlan_.Reset();
        this->needsRebuild_ = true;
    }

    FrameGraphNodeId FrameGraph::AddPassNode(RenderPassIndex renderPassIndex, std::string_view name, std::span<const PassResourceUsage> resourceUsages)
    {
        if (renderPassIndex == kInvalidRenderPassIndex)
        {
            LOG_ERROR("FrameGraph: cannot add a node with an invalid RenderPassIndex.");
            return kInvalidFrameGraphNodeId;
        }

        FrameGraphPassNode node;
        node.nodeId_ = static_cast<FrameGraphNodeId>(this->nodes_.size());
        node.renderPassIndex_ = renderPassIndex;
        node.name_ = std::string(name);
        node.resourceUsages_.assign(resourceUsages.begin(), resourceUsages.end());

        this->nodes_.emplace_back(std::move(node));
        this->needsRebuild_ = true;

        return this->nodes_.back().nodeId_;
    }

    void FrameGraph::AddDependency(FrameGraphNodeId sourceNodeId, FrameGraphNodeId destinationNodeId, RenderResourceId resourceId, ResourceHazard hazard)
    {
        if (sourceNodeId >= this->nodes_.size() ||
            destinationNodeId >= this->nodes_.size() ||
            sourceNodeId == destinationNodeId)
        {
            LOG_ERROR("FrameGraph: cannot add an invalid pass dependency.");
            return;
        }

        this->nodes_[destinationNodeId].dependencies_.push_back(FrameGraphPassDependency{
            sourceNodeId,
            resourceId,
            hazard
        });
        this->needsRebuild_ = true;
    }

    void FrameGraph::BuildEdges()
    {
        this->edges_.clear();

        for (const auto& node : this->nodes_)
        {
            for (const auto& dependency : node.dependencies_)
            {
                this->edges_.push_back(FrameGraphEdge{
                    dependency.sourceNodeId_,
                    node.nodeId_
                });
            }
        }
    }

    bool FrameGraph::Rebuild()
    {
        if (!this->needsRebuild_)
        {
            return this->executionPlan_.valid_;
        }

        this->BuildEdges();

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

        std::vector<uint32_t> incomingCounts(this->nodes_.size(), 0);
        std::vector<std::vector<FrameGraphNodeId>> outgoingNodeIds(this->nodes_.size());

        for (const auto& edge : this->edges_)
        {
            if (edge.fromNodeId_ >= this->nodes_.size() || edge.toNodeId_ >= this->nodes_.size())
            {
                LOG_ERROR("FrameGraph: edge contains an invalid FrameGraphNodeId.");
                return false;
            }

            outgoingNodeIds[edge.fromNodeId_].push_back(edge.toNodeId_);
            ++incomingCounts[edge.toNodeId_];
        }

        std::queue<FrameGraphNodeId> readyNodeIds;

        for (FrameGraphNodeId nodeId = 0; nodeId < this->nodes_.size(); ++nodeId)
        {
            if (incomingCounts[nodeId] == 0)
            {
                readyNodeIds.push(nodeId);
            }
        }

        while (!readyNodeIds.empty())
        {
            const FrameGraphNodeId nodeId = readyNodeIds.front();
            readyNodeIds.pop();

            this->executionPlan_.nodeIds_.push_back(nodeId);

            for (FrameGraphNodeId dependentNodeId : outgoingNodeIds[nodeId])
            {
                if (--incomingCounts[dependentNodeId] == 0)
                {
                    readyNodeIds.push(dependentNodeId);
                }
            }
        }

        if (this->executionPlan_.nodeIds_.size() != this->nodes_.size())
        {
            LOG_ERROR("FrameGraph: dependency cycle detected; no valid execution plan exists.");
            this->executionPlan_.Reset();
            return false;
        }

        this->executionPlan_.valid_ = true;

        LOG_INFO(
            "FrameGraph: built execution plan for " << this->nodes_.size()
            << " nodes and " << this->edges_.size() << " edges."
        );

        for (const auto& node : this->nodes_)
        {
            for (const auto& dependency : node.dependencies_)
            {
                const RenderResourceDescription* resource = FindRenderResourceDescription(dependency.resourceId_);
                LOG_DEBUG(
                    "FrameGraph: " << this->nodes_[dependency.sourceNodeId_].name_ << " -> "
                    << node.name_ << " via "
                    << (resource ? resource->name_ : "UnknownResource") << " "
                    << GetHazardName(dependency.hazard_)
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
