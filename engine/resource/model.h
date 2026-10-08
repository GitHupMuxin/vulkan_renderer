#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include "engine/core/buffer.h"
#include "engine/resource/aabb.h"
#include "engine/resource/mesh_resource.h"
#include "engine/resource/model_data.h"
#include "engine/resource/texture.h"


namespace engine::resource
{
    // 节点在运行时的形态：ModelData::NodeData 加上解析后的模型空间变换与实例槽位。
    // 节点不持有几何，只引用 Model 里的 mesh；多个节点可以引用同一份 mesh。
    struct ModelNode
    {
        std::string             name;
        uint32_t                meshIndex = InvalidIndex;
        uint32_t                parentIndex = InvalidIndex;
        std::vector<uint32_t>   childIndices;

        glm::mat4               localTransform{1.0f};

        // 模型空间（相对资产根）的世界变换；场景实例变换由 Scene 叠加，不在资产内部。
        glm::mat4               worldTransform{1.0f};

        // 逐帧实例矩阵槽位，InvalidIndex 表示该节点没有 mesh。
        // 它与 meshIndex 是两种编号：本字段按"带 mesh 的节点实例"编号（每个节点一个），
        // meshIndex 按共享的几何编号（多个节点可能相同）。
        uint32_t                instanceSlot = InvalidIndex;
    };

    // 一个资产在运行时的形态：几何 + 节点层级 + 材质。
    // 输入是 AssetLoader 产出的 ModelData；本类不解析文件格式，也不决定怎么着色。
    class Model
    {
        public:
            // set 3 的 ShaderMaterial，字段顺序必须与 includes/shadermaterial.glsl 一致。
            struct alignas(16) ShaderMaterial
            {
                glm::vec4   baseColorFactor;
                glm::vec4   emissiveFactor;
                glm::vec4   diffuseFactor;
                glm::vec4   specularFactor;
                float       workflow;
                int32_t     baseColorTextureSet;
                int32_t     physicalDescriptorTextureSet;
                int32_t     normalTextureSet;
                int32_t     occlusionTextureSet;
                int32_t     emissiveTextureSet;
                float       metallicFactor;
                float       roughnessFactor;
                float       alphaMask;
                float       alphaMaskCutoff;
                float       emissiveStrength;
            };

            // set 2 的逐实例数据，字段顺序必须与 pbr.vert 的 MeshData 一致。
            // 蒙皮字段保留：静态几何把 jointCount 置 0，由 shader 跳过蒙皮分支，
            // 这样静态与蒙皮几何能共用同一条管线。
            static constexpr uint32_t kMaxJoints = 128u;
            struct alignas(16) ShaderMeshData
            {
                glm::mat4   matrix;
                glm::mat4   jointMatrix[kMaxJoints]{};
                uint32_t    jointCount = 0;
            };

        private:
            // 几何：一份 MeshData 一对 VBO/IBO；节点用 meshIndex 索引。
            std::vector<std::unique_ptr<MeshResource>>  meshes_;
            std::vector<ModelNode>                      nodes_;

            // 逐帧实例矩阵（set 2）：只为"带 mesh 的节点"建槽位。
            std::vector<ShaderMeshData>                 instanceData_;
            std::vector<core::Buffer>                   instanceBuffers_;
            std::vector<VkDescriptorSet>                instanceSets_;
            uint64_t                                    instanceReadyAt_ = 0;

            // 材质：本期仍由 Model 持有（见 docs/PROJECT_STATE.md 的后续计划）。
            std::vector<std::unique_ptr<Texture>>       textures_;
            std::vector<MaterialData>                   materials_;
            std::vector<ShaderMaterial>                 shaderMaterials_;
            std::vector<VkDescriptorSet>                materialTextureSets_;   // set 1，逐材质
            core::Buffer                                materialBuffer_;        // set 3
            VkDescriptorSet                             materialSet_ = VK_NULL_HANDLE;
            uint64_t                                    materialReadyAt_ = 0;

            AABB                                        bounds_;

            // 建资源分四步：纹理、逐帧实例缓冲、材质缓冲、descriptor。
            // 顺序有依赖：descriptor 写入要求 buffer 与纹理都已存在；实例缓冲按 frameCount 建。
            void                                        CreateTextures(const ModelData& data);
            void                                        CreateInstanceBuffers();
            void                                        CreateMaterialBuffer();
            void                                        CreateDescriptorSets();
            void                                        UpdateDescriptorSets();

            void                                        Destroy();

        public:
            Model() = default;
            ~Model();

            Model(const Model&) = delete;
            Model& operator=(const Model&) = delete;

            // 把一份资产数据转成 GPU 资源。失败时返回 false 并写入首个错误原因，
            // 已建立的部分资源由析构回收。重复调用会先释放已有资源。
            bool                                        Create(const ModelData& data, std::string* error = nullptr);

            // 顶点/索引/材质/纹理的上传回执是否全部达成；未达成时不可用于绘制。
            bool                                        IsReady() const;

            // 几何自带的属性：包围盒、mesh 数、节点数。
            const AABB&                                 GetBounds() const { return this->bounds_; }
            uint32_t                                    GetMeshCount() const { return static_cast<uint32_t>(this->meshes_.size()); }
            const std::vector<ModelNode>&               GetNodes() const { return this->nodes_; }

            // 按 mesh 下标取几何；越界返回 nullptr。
            const MeshResource*                         GetMesh(uint32_t meshIndex) const;

            // 绑定某个 mesh 的顶点/索引缓冲，供绘制前调用；越界时不改变已有绑定。
            void                                        BindGeometry(VkCommandBuffer cb, uint32_t meshIndex) const;

            // 只按几何绘制全部 primitive（不涉及材质与 descriptor），供天空盒等
            // 自备管线与描述符的离线路径使用。
            void                                        Draw(VkCommandBuffer cb) const;

            // 材质数据视图：材质属性与逐材质纹理 Set 由渲染层读取。
            uint32_t                                    GetMaterialCount() const { return static_cast<uint32_t>(this->materials_.size()); }
            const MaterialData*                         GetMaterial(uint32_t materialIndex) const;
            VkDescriptorSet                             GetMaterialTextureSet(uint32_t materialIndex) const;

            // set 2 与 set 3；frameIndex 越界返回 VK_NULL_HANDLE。
            VkDescriptorSet                             GetInstanceSet(uint32_t frameIndex) const;
            VkDescriptorSet                             GetMaterialSet() const { return this->materialSet_; }
    };
}
