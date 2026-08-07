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

    class ResourceManager
    {
        private:
            // std::unordered_map<SimpleModelType, std::unique_ptr<Model>>     prototypes_;

            std::vector<std::unique_ptr<Model>>                             modelsPool_;
            std::vector<uint32_t>                                           modelsGeneration_;
            std::vector<bool>                                               modelsAlive_;

            std::vector<std::unique_ptr<EnvironmentCubeMap>>                envCubeMapsPool_; 
            std::vector<uint32_t>                                           envCubeMapGeneration_;
            std::vector<bool>                                               envCubeMapAlive_;

            ResourceManager() = default;
            ~ResourceManager() = default;

        public:
            static const std::string                                        assetPath_;

            std::unique_ptr<Model>                                          skybox_;

            std::unique_ptr<Texture2D>                                      emptyTexture2D_;

            // std::vector<std::unique_ptr<Model>>                             modelArray_;

            ResourceManager(const ResourceManager&) = delete;
            ResourceManager& operator=(const ResourceManager&) = delete;

            static ResourceManager&                                         Instance();

            void                                                            Init();          

            // ---- 用户资产访问（Handle 校验） ----
            // 三层校验：index 越界 → alive → generation 匹配，任一失败返回 nullptr
            Model*                                                          GetModel(ModelHandle handle);
            EnvironmentCubeMap*                                             GetEnvironmentCubeMap(EnvironmentCubeMapHandle handle);
            bool                                                            IsModelAlive(ModelHandle handle) const;
            bool                                                            IsEnvironmentAlive(EnvironmentCubeMapHandle handle) const;
            uint32_t                                                        GetModelSize() const;
            uint32_t                                                        GetEnvironmentCubeMapSize() const;
            // 释放资源：标记销毁 + generation++，旧 Handle 立即失效
            void                                                            ReleaseModel(ModelHandle handle);
            void                                                            ReleaseEnvironmentCubeMap(EnvironmentCubeMapHandle handle);

            // ---- 系统资源（直接成员，永不销毁） ----
            Texture2D*                                                      GetEmptyTexture2D();
            Model*                                                          GetSkybox();

            // std::unique_ptr<Model>                                          CreateFromPrototype(SimpleModelType type);

            ModelHandle                                                     LoadModel(const std::string& fileName);
            EnvironmentCubeMapHandle                                        LoadSkyBox(const std::string& fileName);
    };
}





