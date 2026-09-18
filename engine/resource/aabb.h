#pragma once

#include <cmath>
#include <limits>

#include <glm/glm.hpp>

namespace engine::resource
{
    struct AABB
    {
        // min > max 表示空包围盒；不另存有效标志，避免与端点状态不一致。
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{std::numeric_limits<float>::lowest()};

        bool IsValid() const
        {
            return std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(min.z) &&
                   std::isfinite(max.x) && std::isfinite(max.y) && std::isfinite(max.z) &&
                   min.x <= max.x && min.y <= max.y && min.z <= max.z;
        }

        // 输入点必须是有限数；模型数据校验负责拒绝 NaN/无穷值。
        void Expand(const glm::vec3& point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        void Expand(const AABB& other)
        {
            if (!other.IsValid())
            {
                return;
            }

            min = glm::min(min, other.min);
            max = glm::max(max, other.max);
        }
    };
}
