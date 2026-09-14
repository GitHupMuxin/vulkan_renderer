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
        this->outputNodeIds_.clear();
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

    void FrameGraph::SetOutputNodes(std::span<const FrameGraphNodeId> nodeIds)
    {
        this->outputNodeIds_.assign(nodeIds.begin(), nodeIds.end());
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

        // 缺少输出起点通常是漏配，不能静默生成一个不执行任何节点的计划。
        if (this->outputNodeIds_.empty())
        {
            LOG_ERROR("FrameGraph: at least one output node must be configured.");
            return false;
        }
        for (const auto nodeId : this->outputNodeIds_)
        {
            // 输出 ID 必须存在于当前图，避免遍历或标记数组越界。
            if (!this->HasNode(nodeId))
            {
                LOG_ERROR("FrameGraph: output references an invalid FrameGraphNodeId.");
                return false;
            }
        }

        std::unordered_map<FrameGraphNodeId, uint32_t> nodeIdIn;
        std::unordered_map<FrameGraphNodeId, std::vector<FrameGraphEdgeDependency>> incomingEdges;
        for (const auto& [sourceNodeId, dependencies] : this->nodeDependencies_)
        {
            for (const auto& edge : dependencies)
            {
                // 每条边的两端都必须存在，才能统计入度和反向追踪依赖。
                if (!this->HasNode(edge.fromNodeId_) || !this->HasNode(edge.toNodeId_))
                {
                    LOG_ERROR("FrameGraph: edge contains an invalid FrameGraphNodeId.");
                    return false;
                }
                nodeIdIn[edge.toNodeId_]++;
                incomingEdges[edge.toNodeId_].push_back(edge);
            }
        }

        std::queue<FrameGraphNodeId> readyNodeIds;
        for (FrameGraphNodeId nodeId = 0; nodeId < this->nodes_.size(); ++nodeId)
        {
            if (nodeIdIn[nodeId] == 0)
            {
                readyNodeIds.push(nodeId);
            }
        }

        // 先检查完整图，未被输出引用的分支也不允许隐藏循环依赖。
        std::vector<FrameGraphNodeId> sortedNodeIds;
        sortedNodeIds.reserve(this->nodes_.size());
        while (!readyNodeIds.empty())
        {
            const FrameGraphNodeId nodeId = readyNodeIds.front();
            readyNodeIds.pop();
            sortedNodeIds.push_back(nodeId);

            const auto outgoing = this->nodeDependencies_.find(nodeId);
            if (outgoing == this->nodeDependencies_.end()) continue;
            for (const auto& edge : outgoing->second)
            {
                nodeIdIn[edge.toNodeId_]--;
                if (nodeIdIn[edge.toNodeId_] == 0)
                {
                    readyNodeIds.push(edge.toNodeId_);
                }
            }
        }

        // 排序未覆盖全部节点说明存在环，不能把部分结果作为有效计划。
        if (sortedNodeIds.size() != this->nodes_.size())
        {
            LOG_ERROR("FrameGraph: dependency cycle detected; no valid execution plan exists.");
            return false;
        }

        // ===== Pass 剔除开始：从最终输出沿入边标记所有需要保留的节点 =====
        std::vector<bool> retainedNodes(this->nodes_.size(), false);
        std::vector<FrameGraphNodeId> pendingNodeIds = this->outputNodeIds_;
        while (!pendingNodeIds.empty())
        {
            const FrameGraphNodeId nodeId = pendingNodeIds.back();
            pendingNodeIds.pop_back();
            // 多个输出可以共享祖先；已经标记的节点无需重复遍历。
            if (retainedNodes[nodeId]) continue;
            retainedNodes[nodeId] = true;

            for (const auto& edge : incomingEdges[nodeId])
            {
                pendingNodeIds.push_back(edge.fromNodeId_);
            }
        }

        // 只过滤执行顺序，保留原始 nodes_ 和 edge，避免改变节点身份。
        std::vector<FrameGraphNodeId> retainedNodeIds;
        retainedNodeIds.reserve(sortedNodeIds.size());
        for (const auto nodeId : sortedNodeIds)
        {
            if (retainedNodes[nodeId]) retainedNodeIds.push_back(nodeId);
        }
        // ===== Pass 剔除结束：后续只为保留节点编译执行与同步信息 =====

        for (const auto nodeId : retainedNodeIds)
        {
            this->executionPlan_.passes_.push_back(CompiledPass{
                .nodeId_ = nodeId
            });

            for (const auto& dependency : incomingEdges[nodeId])
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

        this->executionPlan_.valid_ = true;

        LOG_INFO(
            "FrameGraph: built execution plan for " << this->executionPlan_.passes_.size()
            << " of " << this->nodes_.size() << " nodes; culled "
            << this->nodes_.size() - this->executionPlan_.passes_.size() << " nodes."
        );

        for (const auto nodeId : retainedNodeIds)
        {
            for (const auto& dependency : incomingEdges[nodeId])
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
