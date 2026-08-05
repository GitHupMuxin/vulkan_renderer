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

    class ResourceManager
    {
        private:
            std::unordered_map<SimpleModelType, std::unique_ptr<Model>>     prototypes_;
            std::vector<std::unique_ptr<EnvironmentCubeMap>>                CubeMap_; 

            ResourceManager() = default;
            ~ResourceManager() = default;

        public:
            static const std::string                                        assetPath_;

            // std::unique_ptr<GLTFModel>                                      usageModel_;
            std::unique_ptr<Model>                                      skybox_;

            std::unique_ptr<Texture2D>                                      emptyTexture2D_;

            std::vector<std::unique_ptr<Model>>                             modelArray_;

            ResourceManager(const ResourceManager&) = delete;
            ResourceManager& operator=(const ResourceManager&) = delete;

            static ResourceManager&                                         Instance();

            void                                                            Init();          
            Model*                                                          GetModelInstance(uint32_t i);
            EnvironmentCubeMap*                                             GetEnvironmentCubeMap(uint32_t i);
            Model*                                                          GetPrototype(SimpleModelType type);
            Texture2D*                                                      GetEmptyTexture2D();

            std::unique_ptr<Model>                                          CreateFromPrototype(SimpleModelType type);
            uint32_t                                                        LoadModel(const std::string& fileName);
            uint32_t                                                        LoadSkyBox(const std::string& fileName);
    };
}





