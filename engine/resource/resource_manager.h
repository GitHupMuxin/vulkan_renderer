#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include "engine/resource/gltf_model.h"
#include "engine/resource/model.h"
#include "engine/core/device.h"
#include "engine/resource/environment_lighting.h"

namespace engine::resource
{
    enum class SimpleModelType
    {
        CUBE = 0
    };

    struct ModelHandle
    {
        uint32_t index = UINT32_MAX;    // 资源池下标
        uint32_t generation = 0;        // 代际（防悬垂）
    };

    struct EnvironmentCubeMapHandle
    {
        uint32_t index = UINT32_MAX;    // 资源池下标
        uint32_t generation = 0;        // 代际（防悬垂）
    };

    struct TextureHandle
    {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;
    };

    // 资源生命周期状态；"槽位空闲"不在此表达，由 ResourceSlot::denseIndex 表示
    enum class ResourceState
    {
        Uploading,      // GPU 上传在途（回执未达成，Get 返回 nullptr，暂不渲染）
        Ready,          // 就绪（可渲染）
        PendingDelete   // 待删（Handle 已失效，GPU 用完就销毁）
    };

    // 稳定槽位：外部 handle 直接指向它，数组位置永不搬移，因此 handle 长期有效
    struct ResourceSlot
    {
        uint32_t    denseIndex = UINT32_MAX;    // 指向紧凑存储；UINT32_MAX 表示槽位空闲
        uint32_t    generation = 0;             // 槽位每次被释放递增，用于识别悬垂 handle
    };

    // 资源池：handle 经稳定槽位映射到紧凑存储。删除时对紧凑存储 swap-remove，并回写被搬移
    // 项自己的 denseIndex；漏写会让该资源与它的 handle 永久失联。
    template <typename T>
    class ResourcePool
    {
        public:
            struct Entry
            {
                std::unique_ptr<T>  resource;
                ResourceState       state = ResourceState::Ready;
                uint32_t            slotIndex = UINT32_MAX;     // 反向指回稳定槽位，搬移时靠它回写
            };

        private:
            std::vector<ResourceSlot>   slots_;
            std::vector<Entry>          entries_;
            std::vector<uint32_t>       freeSlots_;

        public:
            // 接管资源并占用槽位；无空闲槽位时追加。返回的 generation 随槽位复用递增。
            void Acquire(std::unique_ptr<T> resource, ResourceState state, uint32_t* outIndex, uint32_t* outGeneration)
            {
                uint32_t slotIndex = UINT32_MAX;
                if (this->freeSlots_.empty())
                {
                    slotIndex = static_cast<uint32_t>(this->slots_.size());
                    this->slots_.push_back(ResourceSlot{});
                }
                else
                {
                    slotIndex = this->freeSlots_.back();
                    this->freeSlots_.pop_back();
                }

                ResourceSlot& slot = this->slots_[slotIndex];
                slot.denseIndex = static_cast<uint32_t>(this->entries_.size());

                Entry entry;
                entry.resource = std::move(resource);
                entry.state = state;
                entry.slotIndex = slotIndex;
                this->entries_.push_back(std::move(entry));

                *outIndex = slotIndex;
                *outGeneration = slot.generation;
            }

            // 校验下标、generation 与槽位占用；任一不满足都视为无效 handle。
            Entry* FindEntry(uint32_t slotIndex, uint32_t generation)
            {
                if (slotIndex >= this->slots_.size())
                {
                    return nullptr;
                }
                const ResourceSlot& slot = this->slots_[slotIndex];
                if (slot.denseIndex == UINT32_MAX || slot.generation != generation)
                {
                    return nullptr;
                }
                return &this->entries_[slot.denseIndex];
            }

            const Entry* FindEntry(uint32_t slotIndex, uint32_t generation) const
            {
                if (slotIndex >= this->slots_.size())
                {
                    return nullptr;
                }
                const ResourceSlot& slot = this->slots_[slotIndex];
                if (slot.denseIndex == UINT32_MAX || slot.generation != generation)
                {
                    return nullptr;
                }
                return &this->entries_[slot.denseIndex];
            }

            // 语义：handle 有效且资源已就绪，可用于渲染。
            T* Get(uint32_t slotIndex, uint32_t generation)
            {
                Entry* entry = this->FindEntry(slotIndex, generation);
                return (entry != nullptr && entry->state == ResourceState::Ready) ? entry->resource.get() : nullptr;
            }

            const T* Get(uint32_t slotIndex, uint32_t generation) const
            {
                const Entry* entry = this->FindEntry(slotIndex, generation);
                return (entry != nullptr && entry->state == ResourceState::Ready) ? entry->resource.get() : nullptr;
            }

            // 语义：handle 有效即可取到资源，不要求已就绪；供 descriptor 分配等 CPU 侧使用。
            T* Peek(uint32_t slotIndex, uint32_t generation)
            {
                Entry* entry = this->FindEntry(slotIndex, generation);
                return entry != nullptr ? entry->resource.get() : nullptr;
            }

            bool IsAlive(uint32_t slotIndex, uint32_t generation) const
            {
                const Entry* entry = this->FindEntry(slotIndex, generation);
                return entry != nullptr && entry->state == ResourceState::Ready;
            }

            // 标记待删并立即递增 generation 使既有 handle 失效；真正销毁由 DestroySlot 在 GPU
            // 完成后执行。仅 Ready 中的资源可释放，重复释放无效。
            bool Release(uint32_t slotIndex, uint32_t generation)
            {
                Entry* entry = this->FindEntry(slotIndex, generation);
                if (entry == nullptr || entry->state != ResourceState::Ready)
                {
                    return false;
                }
                entry->state = ResourceState::PendingDelete;
                ++this->slots_[slotIndex].generation;
                return true;
            }

            // GPU 已不再使用该资源：销毁并从紧凑存储移除，槽位放回空闲链等待复用。
            void DestroySlot(uint32_t slotIndex)
            {
                if (slotIndex >= this->slots_.size())
                {
                    return;
                }
                ResourceSlot& slot = this->slots_[slotIndex];
                if (slot.denseIndex == UINT32_MAX)
                {
                    return;
                }

                const uint32_t denseIndex = slot.denseIndex;
                const uint32_t lastIndex = static_cast<uint32_t>(this->entries_.size() - 1);
                if (denseIndex != lastIndex)
                {
                    this->entries_[denseIndex] = std::move(this->entries_[lastIndex]);
                    // 回写被搬移项自己的槽位：它记录的 denseIndex 已经变了。
                    this->slots_[this->entries_[denseIndex].slotIndex].denseIndex = denseIndex;
                }
                this->entries_.pop_back();

                slot.denseIndex = UINT32_MAX;
                this->freeSlots_.push_back(slotIndex);
            }

            // 释放全部资源并让既有 handle 失效；槽位数组保留、索引位置不变，
            // 这样后续重新 Init 也不会复活旧 handle。
            void Clear()
            {
                for (ResourceSlot& slot : this->slots_)
                {
                    if (slot.denseIndex == UINT32_MAX)
                    {
                        continue;
                    }
                    slot.denseIndex = UINT32_MAX;
                    ++slot.generation;
                }
                this->entries_.clear();
                this->freeSlots_.clear();
                this->freeSlots_.reserve(this->slots_.size());
                for (uint32_t index = 0; index < static_cast<uint32_t>(this->slots_.size()); ++index)
                {
                    this->freeSlots_.push_back(index);
                }
            }

            // 活跃资源集合：供上传状态推进等整池遍历使用。
            std::vector<Entry>& GetEntries() { return this->entries_; }

            uint32_t GetSlotCount() const { return static_cast<uint32_t>(this->slots_.size()); }
    };

    // 资源种类：延迟删除时据此定位资源池
    enum class ResourceKind
    {
        Model = 0,
        EnvironmentCubeMap = 1,
        Texture = 2
    };

    // 延迟删除队列项：记录"哪个资源的槽位在哪个帧后可以真正销毁"
    struct PendingDeletion
    {
        ResourceKind    kind;           // 资源种类
        uint32_t        slotIndex;      // 稳定槽位下标（不是紧凑下标，搬移不影响）
        uint32_t        retireFrame;    // 到期帧号：此帧完成后安全销毁
    };

    class ResourceManager
    {
        private:
            ResourcePool<Model>                             modelPool_;
            ResourcePool<EnvironmentCubeMap>                envPool_;
            ResourcePool<Texture>                           texturePool_;
            std::vector<PendingDeletion>                    pendingDeletions_;

            uint32_t                                        currentFrame_ = 0;
            uint32_t                                        frameCount_ = 2;

            ResourceManager() = default;
            ~ResourceManager();

        public:
            static const std::string                        assetPath_;

            std::unique_ptr<GLTFModelBase>                  skybox_;

            std::unique_ptr<Texture2D>                      emptyTexture2D_;

            ResourceManager(const ResourceManager&) = delete;
            ResourceManager& operator=(const ResourceManager&) = delete;

            static ResourceManager&                         Instance();

            void                                            Init();          
            void                                            Cleanup();

            const Texture*                                  GetTexture(TextureHandle handle) const noexcept;

            Model*                                          GetModel(ModelHandle handle);
            EnvironmentCubeMap*                             GetEnvironmentCubeMap(EnvironmentCubeMapHandle handle);
            // 语义：资源已注册且未失效。用于"分配 descriptor set / 生成 RenderItem"等
            Model*                                          PeekModel(ModelHandle handle);
            bool                                            IsModelAlive(ModelHandle handle) const;
            bool                                            IsEnvironmentAlive(EnvironmentCubeMapHandle handle) const;
            // 槽位数组长度（下标上界），包含已释放的空闲槽位；不是存活资源数量。
            // 槽位数组只增不减，空闲槽位由查询接口返回 nullptr。
            uint32_t                                        GetModelSlotCount() const;
            uint32_t                                        GetEnvironmentCubeMapSlotCount() const;
            void                                            ReleaseModel(ModelHandle handle);
            void                                            ReleaseEnvironmentCubeMap(EnvironmentCubeMapHandle handle);
            // 释放前调用方必须自行确保没有已写入的 descriptor set 仍引用该纹理；
            // 本函数只负责 GPU 侧的延迟销毁，不清理外部句柄与 descriptor 绑定。
            void                                            ReleaseTexture(TextureHandle handle);

            // 每帧由 Renderer 在 wait fence 后调用：推进帧号，清理到期的待删资源
            void                                            OnFrameCompleted();

            // ---- 系统资源（直接成员，永不销毁） ----
            Texture2D*                                      GetEmptyTexture2D();
            GLTFModelBase*                                  GetSkybox();

            // 加载阶段同步等待上传完成，返回可直接登记到 Registry 的 Handle。
            TextureHandle                                   LoadTexture(const std::string& fileName, VkFormat format, VkImageViewType viewType);
            ModelHandle                                     LoadModel(const std::string& fileName);
            EnvironmentCubeMapHandle                        LoadEnvironment(const std::string& fileName);
    };
}
