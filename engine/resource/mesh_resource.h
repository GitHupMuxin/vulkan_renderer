#pragma once
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include "engine/resource/aabb.h"
#include "engine/resource/model_data.h"


namespace engine::resource
{
    // 一份 MeshData 在 GPU 上的形态，与 MeshData 一对一。
    // 不含节点、实例变换、材质与 descriptor：这些会随使用者变化，由引用它的 Model 负责。
    class MeshResource
    {
        public:
            // 顶点布局必须与 pbr.vert 的 location 0..6 及 RenderPass 的 vertex input 一致。
            // 保留蒙皮字段是"静态几何与蒙皮几何共用同一条管线"的前提：
            // 静态几何把 uv1/joint0/weight0 留默认值，由 jointCount 为 0 跳过蒙皮分支。
            struct Vertex
            {
                glm::vec3   pos;
                glm::vec3   normal;
                glm::vec2   uv0;
                glm::vec2   uv1;
                glm::uvec4  joint0;
                glm::vec4   weight0;
                glm::vec4   color;
            };

        private:
            VkBuffer                    vertexBuffer_ = VK_NULL_HANDLE;
            VkDeviceMemory              vertexMemory_ = VK_NULL_HANDLE;
            VkBuffer                    indexBuffer_ = VK_NULL_HANDLE;
            VkDeviceMemory              indexMemory_ = VK_NULL_HANDLE;

            uint32_t                    vertexCount_ = 0;
            std::vector<PrimitiveData>  primitives_;    // 索引区间 + 材质下标
            AABB                        bounds_;        // Mesh 局部空间

            // 上传回执 timeline 值；0 表示无待上传内容，空 Mesh 立即视为就绪。
            uint64_t                    readyAt_ = 0;

        public:
            MeshResource() = default;
            ~MeshResource();

            MeshResource(const MeshResource&) = delete;
            MeshResource& operator=(const MeshResource&) = delete;

            // 上传一份 Mesh 的几何并接管 GPU 资源；不等待回执，就绪状态由 IsReady() 查询。
            // 重复调用会先释放已有资源。buffer 创建失败时返回 false 并已释放本次资源。
            bool                                Create(const MeshData& mesh);
            void                                Destroy();

            // GPU 上传回执是否已达成；未达成的几何不可用于绘制。
            bool                                IsReady() const;

            VkBuffer                            GetVertexBuffer() const { return this->vertexBuffer_; }
            VkBuffer                            GetIndexBuffer() const { return this->indexBuffer_; }
            uint32_t                            GetVertexCount() const { return this->vertexCount_; }
            const std::vector<PrimitiveData>&   GetPrimitives() const { return this->primitives_; }
            const AABB&                         GetBounds() const { return this->bounds_; }
    };

}
