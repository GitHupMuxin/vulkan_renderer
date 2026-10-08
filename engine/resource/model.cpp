#include "engine/resource/model.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "engine/core/config/schema.h"
#include "engine/core/descriptor_allocator.h"
#include "engine/core/descriptor_layout_registry.h"
#include "engine/core/device.h"
#include "engine/core/staging_ring_allocator.h"
#include "engine/resource/resource_manager.h"
#include "engine/utils/log.h"


namespace engine::resource
{
    namespace
    {
        // glTF 采样器常量：ModelData 已归一成枚举，这里只做枚举到 Vulkan 的映射。
        VkSamplerAddressMode ToVkAddressMode(TextureData::WrapMode mode)
        {
            switch (mode)
            {
                case TextureData::WrapMode::Clamp:          return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case TextureData::WrapMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                default:                                    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        }

        VkFilter ToVkFilter(TextureData::FilterMode mode)
        {
            return mode == TextureData::FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        }

        // glTF 的 metallic-roughness 与 specular-glossiness 是两套 BRDF 参数化，
        // shader 用 workflow 分支选择；ModelData 目前只产出前者。
        constexpr float kWorkflowMetallicRoughness = 0.0f;

        bool Fail(std::string* error, const std::string& message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return false;
        }

        // ModelData 把纹理分成外部文件与内嵌字节两种来源。前者由 tinygltf 从磁盘读，
        // 后者需要在这里解码成 tinygltf::Image 才能复用同一条建图路径。
        // 解码失败返回 false，调用方退回空纹理而不是留下半成品资源。
        bool DecodeEncodedImage(const uint8_t* bytes, size_t size, tinygltf::Image* out)
        {
            if (bytes == nullptr || size == 0)
            {
                return false;
            }

            std::string decodeError;
            std::string decodeWarning;
            // 通道策略交给 tinygltf 默认值：结果统一补齐成 RGBA，避开多数设备不支持的 24 位格式。
            if (!tinygltf::LoadImageData(out, 0, &decodeError, &decodeWarning, 0, 0,
                    reinterpret_cast<const unsigned char*>(bytes), static_cast<int>(size), nullptr))
            {
                LOG_ERROR("Model: failed to decode embedded image: " << decodeError);
                return false;
            }
            return true;
        }

        // 读取整个文件到字节数组；失败返回 false。用于把外部图片交给解码器，
        // 而不是让每个调用点各自处理文件 IO。
        bool ReadFileBytes(const std::string& path, std::vector<uint8_t>* out)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return false;
            }

            const std::streamsize fileSize = file.tellg();
            if (fileSize <= 0)
            {
                return false;
            }

            out->resize(static_cast<size_t>(fileSize));
            file.seekg(0, std::ios::beg);
            return static_cast<bool>(file.read(reinterpret_cast<char*>(out->data()), fileSize));
        }

        // KTX2 由 FromglTfImage 自己按 uri 后缀从磁盘转码，不需要预先解码。
        bool HasKtx2Extension(const std::string& path)
        {
            const size_t dot = path.find_last_of('.');
            return dot != std::string::npos && path.compare(dot + 1, std::string::npos, "ktx2") == 0;
        }

        // 把 mesh 局部包围盒变换到模型空间。包围盒经旋转后不再是轴对齐的，
        // 只变换 min/max 端点会得到偏小的盒子；这里取 8 个角点变换后的并集。
        AABB TransformBounds(const AABB& local, const glm::mat4& transform)
        {
            AABB result;
            if (!local.IsValid())
            {
                return result;
            }

            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 point{
                    (corner & 1u) ? local.max.x : local.min.x,
                    (corner & 2u) ? local.max.y : local.min.y,
                    (corner & 4u) ? local.max.z : local.min.z};
                result.Expand(glm::vec3(transform * glm::vec4(point, 1.0f)));
            }
            return result;
        }
    }

    Model::~Model()
    {
        this->Destroy();
    }

    void Model::Destroy()
    {
        // 先于 buffer 释放：VkDescriptorSet 由 allocator 持有，这里只归还引用。
        auto& allocator = core::DescriptorAllocator::Instance();
        for (VkDescriptorSet set : this->materialTextureSets_)
        {
            if (set != VK_NULL_HANDLE)
            {
                allocator.FreePersistent(set);
            }
        }
        if (this->materialSet_ != VK_NULL_HANDLE)
        {
            allocator.FreePersistent(this->materialSet_);
        }
        for (VkDescriptorSet set : this->instanceSets_)
        {
            if (set != VK_NULL_HANDLE)
            {
                allocator.FreePersistent(set);
            }
        }

        this->materialTextureSets_.clear();
        this->instanceSets_.clear();
        this->materialSet_ = VK_NULL_HANDLE;

        this->meshes_.clear();
        this->textures_.clear();

        if (this->materialBuffer_.buffer != VK_NULL_HANDLE)
        {
            this->materialBuffer_.Destroy();
        }
        for (core::Buffer& buffer : this->instanceBuffers_)
        {
            if (buffer.buffer != VK_NULL_HANDLE)
            {
                buffer.Destroy();
            }
        }
        this->instanceBuffers_.clear();

        this->nodes_.clear();
        this->instanceData_.clear();
        this->materials_.clear();
        this->shaderMaterials_.clear();
        this->bounds_ = AABB{};
        this->instanceReadyAt_ = 0;
        this->materialReadyAt_ = 0;
    }

    bool Model::Create(const ModelData& data, std::string* error)
    {
        if (error != nullptr)
        {
            error->clear();
        }

        // 资产数据必须先通过契约校验：本类只负责 GPU 化，不重复做数据合法性判断。
        std::string validationError;
        if (!ValidateModelData(data, &validationError))
        {
            return Fail(error, "Model: ModelData is invalid: " + validationError);
        }

        this->Destroy();

        // ① 几何：一份 MeshData 建一对 VBO/IBO，节点后续按 meshIndex 索引。
        this->meshes_.reserve(data.meshes.size());
        for (size_t meshIndex = 0; meshIndex < data.meshes.size(); ++meshIndex)
        {
            auto resource = std::make_unique<MeshResource>();
            if (!resource->Create(data.meshes[meshIndex]))
            {
                // 失败时不留半成品：已建资源全部释放，由调用方决定后续。
                core::StagingRingAllocator::Instance().WaitAll();
                this->Destroy();
                return Fail(error, "Model: failed to create GPU geometry for mesh " + std::to_string(meshIndex));
            }
            this->meshes_.push_back(std::move(resource));
        }

        // ② 节点层级：保持 ModelData 的扁平下标与父子关系，并算出模型空间的世界变换。
        this->nodes_.reserve(data.nodes.size());
        for (const NodeData& source : data.nodes)
        {
            ModelNode node;
            node.name = source.name;
            node.meshIndex = source.meshIndex;
            node.parentIndex = source.parentIndex;
            node.childIndices = source.childIndices;
            node.localTransform = source.localTransform;
            this->nodes_.push_back(std::move(node));
        }

        // 世界变换必须自顶向下算：父节点的结果先于子节点就绪，且每个节点只能乘一次。
        // 从 rootNodeIndices 出发用显式栈下推，不依赖节点在数组里的排列顺序；
        // 环、父子不一致、根列表不完整已由 ValidateModelData 拒绝，这里不重复检查。
        for (uint32_t rootIndex : data.rootNodeIndices)
        {
            // 根节点没有父节点：局部变换即模型空间变换。
            this->nodes_[rootIndex].worldTransform = this->nodes_[rootIndex].localTransform;
        }

        std::vector<uint32_t> pending(data.rootNodeIndices.begin(), data.rootNodeIndices.end());
        while (!pending.empty())
        {
            const uint32_t nodeIndex = pending.back();
            pending.pop_back();

            const glm::mat4& parentWorld = this->nodes_[nodeIndex].worldTransform;
            for (uint32_t childIndex : this->nodes_[nodeIndex].childIndices)
            {
                this->nodes_[childIndex].worldTransform =
                    parentWorld * this->nodes_[childIndex].localTransform;
                pending.push_back(childIndex);
            }
        }

        // 资产包围盒由“带 mesh 的节点”决定：mesh 局部包围盒经节点世界变换后的并集。
        // Scene 的自动缩放与居中依赖这个量，因此必须含节点变换，不能用 mesh 局部包围盒。
        this->bounds_ = AABB{};
        for (const ModelNode& node : this->nodes_)
        {
            if (node.meshIndex == InvalidIndex)
            {
                continue;
            }
            this->bounds_.Expand(TransformBounds(this->meshes_[node.meshIndex]->GetBounds(), node.worldTransform));
        }

        // ③ 逐实例矩阵：只为"带 mesh 的节点"建槽位。槽位按节点实例编号，
        //    与 meshIndex 是两种编号——多个节点可共享同一份几何但位置不同。
        this->instanceData_.reserve(this->nodes_.size());
        for (ModelNode& node : this->nodes_)
        {
            if (node.meshIndex == InvalidIndex)
            {
                continue;
            }

            node.instanceSlot = static_cast<uint32_t>(this->instanceData_.size());

            ShaderMeshData instance{};
            instance.matrix = node.worldTransform;
            instance.jointCount = 0;    // 静态几何：jointCount 为 0 时 shader 跳过蒙皮分支
            this->instanceData_.push_back(instance);
        }

        // ④ 材质参数：ModelData 的六个表面量与 shader 的 ShaderMaterial 一一对应。
        //    贴图是否存在用 UV 集选择字段表达（-1 表示不采样），与 shader 约定一致。
        this->materials_ = data.materials;
        this->shaderMaterials_.reserve(data.materials.size());
        for (const MaterialData& material : data.materials)
        {
            ShaderMaterial shader{};
            shader.baseColorFactor = material.baseColor.factor;
            shader.emissiveFactor = material.emissive.factor;
            shader.workflow = kWorkflowMetallicRoughness;
            shader.metallicFactor = material.metallic.factor.r;
            shader.roughnessFactor = material.roughness.factor.r;
            shader.alphaMask = material.alphaMode == MaterialData::AlphaMode::Mask ? 1.0f : 0.0f;
            shader.alphaMaskCutoff = material.alphaCutoff;
            shader.emissiveStrength = 1.0f;

            // 0 表示用 uv0（ModelData 只有一套 UV）；-1 表示该表面量不采样纹理。
            shader.baseColorTextureSet = material.baseColor.textureIndex != InvalidIndex ? 0 : -1;
            shader.physicalDescriptorTextureSet = material.metallic.textureIndex != InvalidIndex ? 0 : -1;
            shader.normalTextureSet = material.normal.textureIndex != InvalidIndex ? 0 : -1;
            shader.occlusionTextureSet = material.occlusion.textureIndex != InvalidIndex ? 0 : -1;
            shader.emissiveTextureSet = material.emissive.textureIndex != InvalidIndex ? 0 : -1;

            this->shaderMaterials_.push_back(shader);
        }

        // ⑤～⑧ GPU 资源。顺序固定：描述符写入依赖 buffer 与纹理都已建好。
        this->CreateTextures(data);
        this->CreateInstanceBuffers();
        this->CreateMaterialBuffer();
        this->CreateDescriptorSets();
        this->UpdateDescriptorSets();
        return true;
    }

    void Model::CreateTextures(const ModelData& data)
    {
        this->textures_.resize(data.textures.size());

        for (size_t index = 0; index < data.textures.size(); ++index)
        {
            const TextureData& source = data.textures[index];

            TextureSampler sampler{};
            sampler.magFilter = ToVkFilter(source.magFilter);
            sampler.minFilter = ToVkFilter(source.minFilter);
            sampler.addressModeU = ToVkAddressMode(source.wrapU);
            sampler.addressModeV = ToVkAddressMode(source.wrapV);
            sampler.addressModeW = sampler.addressModeV;

            tinygltf::Image image;
            std::string assetDir;

            if (source.source == TextureData::Source::File)
            {
                // FromglTfImage 的 KTX2 分支要求 path 为目录、uri 为文件名，这里按最后一段切分。
                const size_t slash = source.path.find_last_of("/\\");
                if (slash != std::string::npos)
                {
                    assetDir = source.path.substr(0, slash);
                    image.uri = source.path.substr(slash + 1);
                }
                else
                {
                    image.uri = source.path;
                }

                if (!HasKtx2Extension(source.path))
                {
                    std::vector<uint8_t> bytes;
                    if (!ReadFileBytes(source.path, &bytes) ||
                        !DecodeEncodedImage(bytes.data(), bytes.size(), &image))
                    {
                        LOG_ERROR("Model: texture " << index << " could not be read or decoded: " << source.path);
                        continue;   // 保持 textures_ 中该槽位为空，descriptor 阶段沿用空纹理
                    }
                }
            }
            else
            {
                if (!DecodeEncodedImage(source.bytes.data(), source.bytes.size(), &image))
                {
                    LOG_ERROR("Model: embedded texture " << index << " could not be decoded");
                    continue;
                }
            }

            auto texture = std::make_unique<Texture2D>();
            texture->FromglTfImage(image, assetDir, sampler);
            this->textures_[index] = std::move(texture);
        }
    }

    void Model::CreateInstanceBuffers()
    {
        auto& device = core::Device::Instance();
        const uint32_t frameCount = device.GetSetting().frameCount_;
        const VkDeviceSize bufferSize = this->instanceData_.size() * sizeof(ShaderMeshData);

        this->instanceBuffers_.resize(frameCount);
        if (bufferSize == 0)
        {
            return;   // 没有带 mesh 的节点：不建 buffer，descriptor 写入时跳过
        }

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            core::Buffer& buffer = this->instanceBuffers_[frameIndex];

            if (!device.GetRequireStaging())
            {
                // 可直接映射的显存（ReBAR / 集显）：省掉 staging 与拷贝。
                SUCCESS_OR_LOG(
                    device.CreateBuffer(
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        bufferSize, &buffer.buffer, &buffer.memory),
                    "Model: failed to create instance buffer.");

                buffer.device = device.GetLogicalDeviceHandle();
                buffer.Map();
                memcpy(buffer.mapped, this->instanceData_.data(), bufferSize);
                // 直接写入可映射显存：没有待达成的回执，instanceReadyAt_ 保持 0（已完成）。
            }
            else
            {
                SUCCESS_OR_LOG(
                    device.CreateBuffer(
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        bufferSize, &buffer.buffer, &buffer.memory),
                    "Model: failed to create instance buffer.");

                // 多个 frame 上传相同内容，回执取最晚的一个。
                this->instanceReadyAt_ = std::max(
                    this->instanceReadyAt_,
                    core::StagingRingAllocator::Instance().SubmitBufferCopy(
                        this->instanceData_.data(), bufferSize, buffer.buffer).readyAt);
            }

            buffer.descriptor.buffer = buffer.buffer;
            buffer.descriptor.offset = 0;
            buffer.descriptor.range = bufferSize;
            buffer.device = device.GetLogicalDeviceHandle();
        }
    }

    void Model::CreateMaterialBuffer()
    {
        const VkDeviceSize bufferSize = this->shaderMaterials_.size() * sizeof(ShaderMaterial);
        if (bufferSize == 0)
        {
            return;
        }

        auto& device = core::Device::Instance();
        SUCCESS_OR_LOG(
            device.CreateBuffer(
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                bufferSize, &this->materialBuffer_.buffer, &this->materialBuffer_.memory),
            "Model: failed to create material buffer.");

        // 异步搬运：提交不等待，就绪状态由 materialReadyAt_ 回执判断。
        this->materialReadyAt_ = core::StagingRingAllocator::Instance().SubmitBufferCopy(
            this->shaderMaterials_.data(), bufferSize, this->materialBuffer_.buffer).readyAt;

        this->materialBuffer_.descriptor.buffer = this->materialBuffer_.buffer;
        this->materialBuffer_.descriptor.offset = 0;
        this->materialBuffer_.descriptor.range = bufferSize;
        this->materialBuffer_.device = device.GetLogicalDeviceHandle();
    }

    void Model::CreateDescriptorSets()
    {
        auto& allocator = core::DescriptorAllocator::Instance();
        auto& registry = core::DescriptorLayoutRegistry::Instance();

        // set 1：逐材质纹理；本阶段仍是"每材质一个 set"。
        this->materialTextureSets_.assign(
            this->materials_.size(), VK_NULL_HANDLE);
        if (!this->materials_.empty())
        {
            const VkDescriptorSetLayout layout = registry.GetOrCreate(schema::kMaterialSet);
            for (VkDescriptorSet& set : this->materialTextureSets_)
            {
                set = allocator.AllocatePersistent(layout);
            }
        }

        // set 3：材质参数 SSBO。
        if (this->materialBuffer_.buffer != VK_NULL_HANDLE)
        {
            const VkDescriptorSetLayout layout = registry.GetOrCreate(schema::kMaterialSSBO);
            this->materialSet_ = allocator.AllocatePersistent(layout);
        }

        // set 2：逐帧实例矩阵 SSBO。
        if (!this->instanceData_.empty())
        {
            const VkDescriptorSetLayout layout = registry.GetOrCreate(schema::kMeshDataSSBO);
            this->instanceSets_.resize(this->instanceBuffers_.size());
            for (VkDescriptorSet& set : this->instanceSets_)
            {
                set = allocator.AllocatePersistent(layout);
            }
        }
    }

    void Model::UpdateDescriptorSets()
    {
        const VkDevice device = core::Device::Instance().GetLogicalDeviceHandle();
        const Texture* emptyTexture = ResourceManager::Instance().GetEmptyTexture2D();
        if (emptyTexture == nullptr)
        {
            throw std::logic_error("Model: the shared empty texture must exist before updating descriptor sets");
        }

        // 六个表面量到 set 1 五个 binding 的对应关系；缺纹理的槽位沿用空纹理，
        // 采样结果由 ShaderMaterial 的 UV 集字段（-1）在 shader 里短路，不会真正参与着色。
        struct SlotMap
        {
            MaterialSlot slot;
            uint32_t     binding;
        };
        static constexpr std::array<SlotMap, 5> kSlotMap{{
            {MaterialSlot::BaseColor, 0},
            {MaterialSlot::Roughness, 1},   // 与 metallic 共享同一张打包贴图
            {MaterialSlot::Normal,    2},
            {MaterialSlot::Occlusion, 3},
            {MaterialSlot::Emissive,  4},
        }};

        for (size_t materialIndex = 0; materialIndex < this->materials_.size(); ++materialIndex)
        {
            const MaterialData& material = this->materials_[materialIndex];

            std::array<VkDescriptorImageInfo, schema::kMaterialSet.size()> images{};
            images.fill(emptyTexture->descriptor_);

            for (const SlotMap& map : kSlotMap)
            {
                const SurfaceQuantity* quantity = GetSurfaceQuantity(material, map.slot);
                if (quantity == nullptr || quantity->textureIndex == InvalidIndex)
                {
                    continue;
                }

                const Texture* texture = quantity->textureIndex < this->textures_.size()
                    ? this->textures_[quantity->textureIndex].get()
                    : nullptr;
                if (texture != nullptr)
                {
                    images[map.binding] = texture->descriptor_;
                }
            }

            std::array<VkWriteDescriptorSet, schema::kMaterialSet.size()> writes{};
            for (size_t binding = 0; binding < writes.size(); ++binding)
            {
                VkWriteDescriptorSet& write = writes[binding];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = this->materialTextureSets_[materialIndex];
                write.dstBinding = schema::kMaterialSet[binding].binding;
                write.descriptorType = schema::kMaterialSet[binding].descriptorType;
                write.descriptorCount = schema::kMaterialSet[binding].descriptorCount;
                write.pImageInfo = &images[binding];
            }
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        // set 3：材质参数 SSBO。
        if (this->materialSet_ != VK_NULL_HANDLE)
        {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = this->materialSet_;
            write.dstBinding = schema::kMaterialSSBO[0].binding;
            write.descriptorType = schema::kMaterialSSBO[0].descriptorType;
            write.descriptorCount = schema::kMaterialSSBO[0].descriptorCount;
            write.pBufferInfo = &this->materialBuffer_.descriptor;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        // set 2：每个 frame 的 Set 指向该 frame 的实例矩阵 buffer。
        for (size_t frameIndex = 0; frameIndex < this->instanceSets_.size(); ++frameIndex)
        {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = this->instanceSets_[frameIndex];
            write.dstBinding = schema::kMeshDataSSBO[0].binding;
            write.descriptorType = schema::kMeshDataSSBO[0].descriptorType;
            write.descriptorCount = schema::kMeshDataSSBO[0].descriptorCount;
            write.pBufferInfo = &this->instanceBuffers_[frameIndex].descriptor;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    bool Model::IsReady() const
    {
        auto& allocator = core::StagingRingAllocator::Instance();
        // 几何、实例、材质与纹理回执互相独立；任一份未达成都不能绘制。
        // 材质为空时其回执保持 0（已完成），
        // 因此不能只看材质回执——实例矩阵必须单独检查。
        if (!allocator.IsCompleted(this->instanceReadyAt_))
        {
            return false;
        }
        if (!allocator.IsCompleted(this->materialReadyAt_))
        {
            return false;
        }
        for (const auto& mesh : this->meshes_)
        {
            if (!mesh->IsReady())
            {
                return false;
            }
        }
        for (const auto& texture : this->textures_)
        {
            if (texture != nullptr && !allocator.IsCompleted(texture->readyAt_))
            {
                return false;
            }
        }
        return true;
    }

    const MeshResource* Model::GetMesh(uint32_t meshIndex) const
    {
        if (meshIndex >= this->meshes_.size())
        {
            return nullptr;
        }
        return this->meshes_[meshIndex].get();
    }

    VkDescriptorSet Model::GetInstanceSet(uint32_t frameIndex) const
    {
        if (frameIndex >= this->instanceSets_.size())
        {
            return VK_NULL_HANDLE;
        }
        return this->instanceSets_[frameIndex];
    }

    VkDescriptorSet Model::GetMaterialTextureSet(uint32_t materialIndex) const
    {
        if (materialIndex >= this->materialTextureSets_.size())
        {
            return VK_NULL_HANDLE;
        }
        return this->materialTextureSets_[materialIndex];
    }

    const MaterialData* Model::GetMaterial(uint32_t materialIndex) const
    {
        if (materialIndex >= this->materials_.size())
        {
            return nullptr;
        }
        return &this->materials_[materialIndex];
    }

    void Model::BindGeometry(VkCommandBuffer cb, uint32_t meshIndex) const
    {
        const MeshResource* mesh = this->GetMesh(meshIndex);
        if (mesh == nullptr)
        {
            // 越界时不改变已有绑定：调用方可能复用上一次的绑定继续绘制。
            return;
        }

        const VkDeviceSize offset = 0;
        const VkBuffer vertexBuffer = mesh->GetVertexBuffer();
        vkCmdBindVertexBuffers(cb, 0, 1, &vertexBuffer, &offset);

        const VkBuffer indexBuffer = mesh->GetIndexBuffer();
        if (indexBuffer != VK_NULL_HANDLE)
        {
            vkCmdBindIndexBuffer(cb, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
    }

    void Model::Draw(VkCommandBuffer cb) const
    {
        // 只按几何遍历：材质与 descriptor 由调用方自己绑定。
        // 用于天空盒这类自备管线与描述符的离线路径。
        for (const ModelNode& node : this->nodes_)
        {
            if (node.meshIndex == InvalidIndex)
            {
                continue;
            }

            const MeshResource* mesh = this->GetMesh(node.meshIndex);
            if (mesh == nullptr)
            {
                continue;
            }

            this->BindGeometry(cb, node.meshIndex);
            for (const PrimitiveData& primitive : mesh->GetPrimitives())
            {
                if (primitive.indexCount == 0)
                {
                    continue;
                }
                vkCmdDrawIndexed(cb, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
            }
        }
    }
}
