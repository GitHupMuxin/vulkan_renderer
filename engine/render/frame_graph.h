#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/render/pass_resource.h"

namespace engine::render
{
    using FrameGraphNodeId = uint32_t;
    using RenderPassIndex = uint32_t;

    constexpr FrameGraphNodeId kInvalidFrameGraphNodeId = UINT32_MAX;
    constexpr RenderPassIndex kInvalidRenderPassIndex = UINT32_MAX;

    enum class ResourceHazard
    {
        ReadAfterWrite,
        WriteAfterRead,
        WriteAfterWrite
    };

    // Node 对另一个 Node 的显式依赖，并记录这项依赖对应的资源访问冲突。
    struct FrameGraphPassDependency
    {
        FrameGraphNodeId sourceNodeId_ = kInvalidFrameGraphNodeId;
        RenderResourceId resourceId_{};
        ResourceHazard hazard_{};
    };

    // Node 只保存算法所需声明和实际 Pass 下标，不拥有 RenderPass。
    struct FrameGraphPassNode
    {
        FrameGraphNodeId nodeId_ = kInvalidFrameGraphNodeId;
        RenderPassIndex renderPassIndex_ = kInvalidRenderPassIndex;

        std::string name_;
        std::vector<PassResourceUsage> resourceUsages_;
        std::vector<FrameGraphPassDependency> dependencies_;
    };

    // Edge 只表示两个 Node 之间的有向关系。
    struct FrameGraphEdge
    {
        FrameGraphNodeId fromNodeId_ = kInvalidFrameGraphNodeId;
        FrameGraphNodeId toNodeId_ = kInvalidFrameGraphNodeId;
    };

    struct FrameGraphExecutionPlan
    {
        std::vector<FrameGraphNodeId> nodeIds_;
        bool valid_ = false;

        void Reset() noexcept
        {
            this->nodeIds_.clear();
            this->valid_ = false;
        }
    };

    class FrameGraph
    {
        private:
            std::vector<FrameGraphPassNode> nodes_;
            std::vector<FrameGraphEdge> edges_;
            FrameGraphExecutionPlan executionPlan_;

            bool needsRebuild_ = true;

            void BuildEdges();
            bool BuildExecutionPlan();
        public:
            void Reset();

            FrameGraphNodeId AddPassNode(RenderPassIndex renderPassIndex, std::string_view name, std::span<const PassResourceUsage> resourceUsages);

            // 按照 src -> dst 的方向，将显式依赖保存到目标 Node。
            void AddDependency(FrameGraphNodeId sourceNodeId, FrameGraphNodeId destinationNodeId, RenderResourceId resourceId, ResourceHazard hazard);

            // 显式重构 Edge 和 ExecutionPlan；没有变化时直接返回已有结果。
            bool Rebuild();
            bool NeedsRebuild() const noexcept;

            const FrameGraphPassNode& GetNode(FrameGraphNodeId nodeId) const;
            const FrameGraphExecutionPlan& GetExecutionPlan() const noexcept;
    };
}
