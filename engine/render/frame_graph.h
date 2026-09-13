#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/render/config/pass_resource.h"
#include "engine/render/render_pass.h"

namespace engine::render
{
    class RenderPass;

    using FrameGraphNodeId = uint32_t;
    using RenderPassIndex = uint32_t;

    constexpr FrameGraphNodeId kInvalidFrameGraphNodeId = UINT32_MAX;

    // Node 只保存 Pass 关联和显式依赖；资源使用声明仍由对应 RenderPass 提供。
    struct FrameGraphPassNode
    {
        FrameGraphNodeId nodeId_ = kInvalidFrameGraphNodeId;
        std::string name_;
        RenderPass* renderPass_ = nullptr;
    };

    // Edge 只表示两个 Node 之间的有向关系。
    struct FrameGraphEdgeDependency
    {
        FrameGraphNodeId fromNodeId_ = kInvalidFrameGraphNodeId;
        FrameGraphNodeId toNodeId_ = kInvalidFrameGraphNodeId;
        RenderResourceReference resource_{};
    };

    struct CompiledResourceBarrier
    {
        RenderResourceReference resource_{};
        PassResourceUsage srcUsage_{};
        PassResourceUsage dstUsage_{};
    };

    struct CompiledAttachmentDependency
    {
        RenderResourceReference resource_{};
        PassResourceUsage   srcUsage_{};
        PassResourceUsage   dstUsage_{};
    };

    struct CompiledPass
    {
        FrameGraphNodeId nodeId_ = kInvalidFrameGraphNodeId;
        std::vector<CompiledAttachmentDependency> attachmentDependencies_;
        std::vector<CompiledResourceBarrier> barriersBefore_;
    };

    struct FrameGraphExecutionPlan
    {
        std::vector<CompiledPass> passes_;
        bool valid_ = false;

        void Reset() noexcept
        {
            this->passes_.clear();
            this->valid_ = false;
        }
    };

    class FrameGraph
    {
        private:
            std::unordered_map<FrameGraphNodeId, FrameGraphPassNode> nodes_;
            std::unordered_map<FrameGraphNodeId, std::vector<FrameGraphEdgeDependency>> nodeDependencies_;
            FrameGraphExecutionPlan executionPlan_;

            bool needsRebuild_ = true;

            bool HasNode(FrameGraphNodeId nodeId) const noexcept;
            bool BuildExecutionPlan();
        public:
            // Reset 后旧 Node ID 失效；新一轮建图从 0 连续编号。
            void Reset();

            FrameGraphNodeId AddPassNode(std::string_view name, RenderPass* renderPass);

            // 按照 src -> dst 的方向，将显式依赖保存到源 Node 的出边表。
            void AddDependency(FrameGraphNodeId sourceNodeId, FrameGraphNodeId destinationNodeId, RenderResourceReference resource);

            // 显式重构 Edge 和 ExecutionPlan；没有变化时直接返回已有结果。
            bool Rebuild();
            bool NeedsRebuild() const noexcept;

            const FrameGraphPassNode& GetNode(FrameGraphNodeId nodeId) const;
            const FrameGraphExecutionPlan& GetExecutionPlan() const noexcept;
    };
}
