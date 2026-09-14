#pragma once

#include <string_view>
#include <vector>

#include "engine/render/render_pass_description.h"

namespace engine::render
{
    struct PassDependencyDescription
    {
        // 名称引用同一份管线描述中的 Pass；运行时 Node ID 由 Context 建图时解析。
        std::string_view sourcePass_;
        std::string_view destinationPass_;
        RenderResourceReference resource_;
    };

    struct RenderPipelineDescription
    {
        // Pass 的 name_ 在管线内必须非空且唯一。
        std::vector<RenderPassDescription> passes_;
        std::vector<PassDependencyDescription> dependencies_;
        // 最终输出节点的 name_，作为反向追踪依赖时的保留起点。
        std::vector<std::string_view> outputPasses_;
    };
}
