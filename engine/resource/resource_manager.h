#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
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

    // 资源生命周期状态（类似进程控制块的状态）
    enum class ResourceState
    {
        Free,           // 槽位空闲（未使用，可复用）
        Uploading,      // 已分配，GPU 上传在途（回执未达成，GetModel 返回 nullptr，暂不渲染）
        Ready,          // 就绪（可渲染）
        PendingDelete   // 待删（Handle 已失效，GPU 用完就销毁）
    };

    // 资源槽（控制块）：一个资源的全部属性内聚在一起
    template <typename T>
    struct ResourceSlot
    {
        std::unique_ptr<T>  resource;
        uint32_t            generation = 0;
        ResourceState       state = ResourceState::Free;
    };

    // 延迟删除队列项：记录"哪个资源在哪个帧后可以真正销毁"
    struct PendingDeletion
    {
        uint32_t    poolIndex;      // 资源池下标
        bool        isModel;        // true=Model, false=EnvironmentCubeMap
        uint32_t    retireFrame;    // 到期帧号：此帧完成后安全销毁
    };

    class ResourceManager
    {
        private:
            std::vector<ResourceSlot<Model>>                modelSlots_;
            std::vector<ResourceSlot<EnvironmentCubeMap>>   envSlots_;
            std::vector<PendingDeletion>                    pendingDeletions_;

            uint32_t                                        currentFrame_ = 0;
            uint32_t                                        frameCount_ = 2;

            ResourceManager() = default;
            ~ResourceManager();

        public:
            static const std::string                        assetPath_;

            std::unique_ptr<Model>                          skybox_;

            std::unique_ptr<Texture2D>                      emptyTexture2D_;

            ResourceManager(const ResourceManager&) = delete;
            ResourceManager& operator=(const ResourceManager&) = delete;

            static ResourceManager&                         Instance();

            void                                            Init();          

            // ---- 用户资产访问（Handle 校验） ----
            // 三层校验：index 越界 → state == Ready → generation 匹配，任一失败返回 nullptr
            Model*                                          GetModel(ModelHandle handle);
            EnvironmentCubeMap*                             GetEnvironmentCubeMap(EnvironmentCubeMapHandle handle);
            // 仅校验 index + generation（资源存在、GPU 对象可访问），不要求 state==Ready。
            // 语义：资源已注册且未失效。用于"分配 descriptor set / 生成 RenderItem"等
            // 只看资源存在性的场景；"是否可渲染"由 GetModel（state==Ready）决定。
            Model*                                          PeekModel(ModelHandle handle);
            bool                                            IsModelAlive(ModelHandle handle) const;
            bool                                            IsEnvironmentAlive(EnvironmentCubeMapHandle handle) const;
            uint32_t                                        GetModelSize() const;
            uint32_t                                        GetEnvironmentCubeMapSize() const;
            // 释放资源：Handle 立即失效（state→PendingDelete + generation++），
            // 真正的 GPU 资源销毁延迟到 GPU 使用结束后（见 OnFrameCompleted）
            void                                            ReleaseModel(ModelHandle handle);
            void                                            ReleaseEnvironmentCubeMap(EnvironmentCubeMapHandle handle);

            // 每帧由 Renderer 在 wait fence 后调用：推进帧号，清理到期的待删资源
            void                                            OnFrameCompleted();

            // ---- 系统资源（直接成员，永不销毁） ----
            Texture2D*                                      GetEmptyTexture2D();
            Model*                                          GetSkybox();

            ModelHandle                                     LoadModel(const std::string& fileName);
            EnvironmentCubeMapHandle                        LoadSkyBox(const std::string& fileName);
    };
}





