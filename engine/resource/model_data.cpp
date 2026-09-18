#include "engine/resource/model_data.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace engine::resource
{
    namespace
    {
        bool Fail(std::string* error, const std::string& message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return false;
        }

        bool IsFinite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const glm::mat4& matrix)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                for (uint32_t row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(matrix[column][row]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    void MeshData::RecalculateBounds()
    {
        bounds = AABB{};
        for (const VertexData& vertex : vertices)
        {
            bounds.Expand(vertex.position);
        }
    }

    bool ValidateModelData(const ModelData& model, std::string* error)
    {
        if (error != nullptr)
        {
            error->clear();
        }

        // 检查每个 Mesh 的几何数据与局部包围盒。
        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
        {
            const MeshData& mesh = model.meshes[meshIndex];
            const std::string meshName = "Mesh " + std::to_string(meshIndex);
            AABB expectedBounds;

            // 顶点位置必须有限，同时计算用于核对缓存的包围盒。
            for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex)
            {
                const glm::vec3& position = mesh.vertices[vertexIndex].position;
                if (!IsFinite(position))
                {
                    return Fail(error, meshName + " vertex " + std::to_string(vertexIndex) + " has a non-finite position");
                }
                expectedBounds.Expand(position);
            }

            // 空 Mesh 必须保留空包围盒；非空 Mesh 的缓存必须与全部顶点一致。
            if (mesh.vertices.empty())
            {
                if (mesh.bounds.IsValid())
                {
                    return Fail(error, meshName + " has bounds but no vertices");
                }
            }
            else if (!mesh.bounds.IsValid() ||
                     mesh.bounds.min != expectedBounds.min || mesh.bounds.max != expectedBounds.max)
            {
                return Fail(error, meshName + " bounds do not match its vertices");
            }

            // 每个顶点索引都必须指向当前 Mesh 的顶点数组。
            for (size_t index = 0; index < mesh.vertexIndices.size(); ++index)
            {
                if (mesh.vertexIndices[index] >= mesh.vertices.size())
                {
                    return Fail(error, meshName + " vertex index " + std::to_string(index) + " is out of range");
                }
            }

            // Primitive 的索引区间和材质引用不能越界。
            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
            {
                const PrimitiveData& primitive = mesh.primitives[primitiveIndex];
                const std::string primitiveName = meshName + " primitive " + std::to_string(primitiveIndex);
                if (primitive.firstIndex > mesh.vertexIndices.size() ||
                    primitive.indexCount > mesh.vertexIndices.size() - primitive.firstIndex)
                {
                    return Fail(error, primitiveName + " index range is out of bounds");
                }
                if (primitive.materialIndex != InvalidIndex &&
                    primitive.materialIndex >= model.materials.size())
                {
                    return Fail(error, primitiveName + " material index is out of range");
                }
            }
        }

        const size_t nodeCount = model.nodes.size();
        std::vector<uint8_t> childReferences(nodeCount, 0);
        std::vector<uint8_t> rootReferences(nodeCount, 0);

        // 检查节点的 Mesh、父节点、局部矩阵和子节点引用。
        for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
        {
            const NodeData& node = model.nodes[nodeIndex];
            const std::string nodeName = "Node " + std::to_string(nodeIndex);
            if (node.meshIndex != InvalidIndex && node.meshIndex >= model.meshes.size())
            {
                return Fail(error, nodeName + " mesh index is out of range");
            }
            if (node.parentIndex != InvalidIndex && node.parentIndex >= nodeCount)
            {
                return Fail(error, nodeName + " parent index is out of range");
            }
            if (!IsFinite(node.localTransform))
            {
                return Fail(error, nodeName + " has a non-finite local transform");
            }

            // 子节点必须反向指向当前父节点，且不能被重复列出。
            for (uint32_t childIndex : node.childIndices)
            {
                if (childIndex >= nodeCount)
                {
                    return Fail(error, nodeName + " child index is out of range");
                }
                if (model.nodes[childIndex].parentIndex != nodeIndex)
                {
                    return Fail(error, nodeName + " child has a different parent");
                }
                if (++childReferences[childIndex] != 1)
                {
                    return Fail(error, "Node " + std::to_string(childIndex) + " is referenced more than once as a child");
                }
            }
        }

        // 反向核对：非根节点必须恰好出现在父节点的子列表中。
        for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
        {
            const NodeData& node = model.nodes[nodeIndex];
            if (node.parentIndex == InvalidIndex)
            {
                if (childReferences[nodeIndex] != 0)
                {
                    return Fail(error, "Root node " + std::to_string(nodeIndex) + " is also referenced as a child");
                }
            }
            else if (childReferences[nodeIndex] != 1)
            {
                return Fail(error, "Node " + std::to_string(nodeIndex) + " is missing from its parent's children");
            }
        }

        std::vector<uint8_t> visitState(nodeCount, 0);
        // 沿每个节点的父链查环；没有根节点的环也必须明确报告。
        for (size_t start = 0; start < nodeCount; ++start)
        {
            if (visitState[start] != 0)
            {
                continue;
            }

            std::vector<uint32_t> path;
            uint32_t current = static_cast<uint32_t>(start);
            // 标记本次父链，遇到正在访问的节点就是环。
            while (current != InvalidIndex && visitState[current] == 0)
            {
                visitState[current] = 1;
                path.push_back(current);
                current = model.nodes[current].parentIndex;
            }
            if (current != InvalidIndex && visitState[current] == 1)
            {
                return Fail(error, "Node hierarchy contains a cycle at node " + std::to_string(current));
            }
            // 本次父链无环后，标记为已完成以避免重复检查。
            for (uint32_t pathNode : path)
            {
                visitState[pathNode] = 2;
            }
        }

        // 根节点列表只能包含无父节点的有效索引，且不能重复。
        for (uint32_t rootIndex : model.rootNodeIndices)
        {
            if (rootIndex >= nodeCount)
            {
                return Fail(error, "Root node index is out of range");
            }
            if (model.nodes[rootIndex].parentIndex != InvalidIndex)
            {
                return Fail(error, "Node " + std::to_string(rootIndex) + " is listed as a root but has a parent");
            }
            if (++rootReferences[rootIndex] != 1)
            {
                return Fail(error, "Node " + std::to_string(rootIndex) + " is listed as a root more than once");
            }
        }

        // 所有实际根节点都必须出现在根节点列表中。
        for (size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
        {
            if (model.nodes[nodeIndex].parentIndex == InvalidIndex && rootReferences[nodeIndex] != 1)
            {
                return Fail(error, "Root node " + std::to_string(nodeIndex) + " is missing from rootNodeIndices");
            }
        }

        return true;
    }
}
