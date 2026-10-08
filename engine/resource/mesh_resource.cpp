#include "engine/resource/mesh_resource.h"

#include <algorithm>

#include "engine/core/device.h"
#include "engine/core/staging_ring_allocator.h"
#include "engine/utils/log.h"


namespace engine::resource
{
    MeshResource::~MeshResource()
    {
        this->Destroy();
    }

    bool MeshResource::Create(const MeshData& mesh)
    {
        this->Destroy();

        // VertexData 补齐为 shader 声明的布局：uv1/joint0/weight0 留默认值，
        // 静态几何靠 MeshShaderData 中 jointCount 为 0 跳过蒙皮分支。
        std::vector<Vertex> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const VertexData& source : mesh.vertices)
        {
            Vertex vertex{};
            vertex.pos = source.position;
            vertex.normal = source.normal;
            vertex.uv0 = source.uv0;
            vertex.color = source.color;
            vertices.push_back(vertex);
        }

        this->vertexCount_ = static_cast<uint32_t>(vertices.size());
        this->primitives_ = mesh.primitives;
        this->bounds_ = mesh.bounds;

        auto& device = core::Device::Instance();
        auto& allocator = core::StagingRingAllocator::Instance();

        // 空 Mesh 允许存在（ValidateModelData 允许），此时不创建 buffer，回执保持 0。
        const VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(vertices.size()) * sizeof(Vertex);
        if (vertexBufferSize > 0)
        {
            // 创建失败必须立即返回：继续调用 SubmitBufferCopy 会向空句柄提交拷贝。
            if (!device.CreateBuffer(
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    vertexBufferSize, &this->vertexBuffer_, &this->vertexMemory_))
            {
                LOG_ERROR("MeshResource: failed to create vertex buffer.");
                this->Destroy();
                return false;
            }

            this->readyAt_ = std::max(this->readyAt_, allocator.SubmitBufferCopy(vertices.data(), vertexBufferSize, this->vertexBuffer_).readyAt);
        }

        const VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(mesh.vertexIndices.size()) * sizeof(uint32_t);
        if (indexBufferSize > 0)
        {
            if (!device.CreateBuffer(
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    indexBufferSize, &this->indexBuffer_, &this->indexMemory_))
            {
                LOG_ERROR("MeshResource: failed to create index buffer.");
                // 顶点可能仍在上传，先等回执再销毁已建 buffer。
                allocator.WaitUntil(this->readyAt_);
                this->Destroy();
                return false;
            }

            this->readyAt_ = std::max(this->readyAt_, allocator.SubmitBufferCopy(mesh.vertexIndices.data(), indexBufferSize, this->indexBuffer_).readyAt);
        }

        return true;
    }

    void MeshResource::Destroy()
    {
        auto& device = core::Device::Instance();

        if (this->vertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device.GetLogicalDeviceHandle(), this->vertexBuffer_, nullptr);
            vkFreeMemory(device.GetLogicalDeviceHandle(), this->vertexMemory_, nullptr);
            this->vertexBuffer_ = VK_NULL_HANDLE;
            this->vertexMemory_ = VK_NULL_HANDLE;
        }
        if (this->indexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device.GetLogicalDeviceHandle(), this->indexBuffer_, nullptr);
            vkFreeMemory(device.GetLogicalDeviceHandle(), this->indexMemory_, nullptr);
            this->indexBuffer_ = VK_NULL_HANDLE;
            this->indexMemory_ = VK_NULL_HANDLE;
        }

        this->vertexCount_ = 0;
        this->primitives_.clear();
        this->bounds_ = AABB{};
        this->readyAt_ = 0;
    }

    bool MeshResource::IsReady() const
    {
        return core::StagingRingAllocator::Instance().IsCompleted(this->readyAt_);
    }
}
