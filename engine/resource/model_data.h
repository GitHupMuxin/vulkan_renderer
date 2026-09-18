#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/resource/aabb.h"

namespace engine::resource
{
    constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

    struct VertexData
    {
        glm::vec3 position{};
        glm::vec3 normal{};
        glm::vec2 uv0{};
        glm::vec4 color{1.0f};
    };

    struct PrimitiveData
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t materialIndex = InvalidIndex;
    };

    struct MeshData
    {
        std::vector<VertexData> vertices;
        std::vector<uint32_t> vertexIndices;
        std::vector<PrimitiveData> primitives;

        // 包围盒处于 Mesh 局部空间，由全部顶点位置计算；修改顶点后需重新计算。
        AABB bounds;
        void RecalculateBounds();
    };

    struct NodeData
    {
        uint32_t meshIndex = InvalidIndex;

        std::string name;

        glm::mat4 localTransform{1.0f};

        uint32_t parentIndex = InvalidIndex;
        std::vector<uint32_t> childIndices;
    };


    struct MaterialData
    {
    };

    struct ModelData
    {
        std::string name;

        std::vector<MeshData> meshes;
        std::vector<NodeData> nodes;
        std::vector<uint32_t> rootNodeIndices;
        std::vector<MaterialData> materials;
    };

    // 只检查资产数据契约，不加载文件或创建 GPU 资源；失败时返回首个错误。
    bool ValidateModelData(const ModelData& model, std::string* error = nullptr);
}
