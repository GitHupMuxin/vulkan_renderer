#include <chrono>
#include "engine/resource/model.h"
#include"engine/resource/resource_manager.h"


namespace engine::resource
{
    const std::string ResourceManager::assetPath_ = VK_EXAMPLE_DATA_DIR;

    ResourceManager& ResourceManager::Instance()
    {
        static ResourceManager instance;
        return instance;
    }

    void ResourceManager::Init()
    {
        LOG_INFO("ResourceManager: start to init resource manager...");
        std::string skyboxFile = ResourceManager::assetPath_ + "models/Box/glTF-Embedded/Box.gltf";
        std::string emptyTexture2DFile = this->assetPath_ + "textures/empty.ktx";

        // 系统资源：skybox 立方体 + 空纹理，应用整个生命周期存活，不走 Handle
        this->skybox_ = std::make_unique<GLTFModel>();
        this->skybox_->LoadFromFile(skyboxFile);
        this->skybox_->CreateMaterialBuffer();
        this->skybox_->CreateMeshDataBuffer();

        this->emptyTexture2D_ = std::make_unique<Texture2D>();
        this->emptyTexture2D_->LoadFromFile(emptyTexture2DFile, VK_FORMAT_R8G8B8A8_UNORM);

        // 用户资产（场景模型、环境贴图）由 Scene 按 SceneDescription 加载，
        // 见 Scene::Init(const SceneDescription&)，这里不再预加载。
    }

    Model* ResourceManager::GetModel(ModelHandle handle)
    {
        if (handle.index >= this->modelsPool_.size())
        {
            return nullptr;
        }
        if (!this->modelsAlive_[handle.index])
        {
            return nullptr;
        }
        if (this->modelsGeneration_[handle.index] != handle.generation)
        {
            return nullptr;
        }
        return this->modelsPool_[handle.index].get();
    }

    EnvironmentCubeMap* ResourceManager::GetEnvironmentCubeMap(EnvironmentCubeMapHandle handle)
    {
        if (handle.index >= this->envCubeMapsPool_.size())
        {
            return nullptr;
        }
        if (!this->envCubeMapAlive_[handle.index])
        {
            return nullptr;
        }
        if (this->envCubeMapGeneration_[handle.index] != handle.generation)
        {
            return nullptr;
        }
        return this->envCubeMapsPool_[handle.index].get();
    }

    bool ResourceManager::IsModelAlive(ModelHandle handle) const
    {
        return handle.index < this->modelsPool_.size()
            && this->modelsAlive_[handle.index]
            && this->modelsGeneration_[handle.index] == handle.generation;
    }

    bool ResourceManager::IsEnvironmentAlive(EnvironmentCubeMapHandle handle) const
    {
        return handle.index < this->envCubeMapsPool_.size()
            && this->envCubeMapAlive_[handle.index]
            && this->envCubeMapGeneration_[handle.index] == handle.generation;
    }

    uint32_t ResourceManager::GetModelSize() const
    {
        return static_cast<uint32_t>(this->modelsPool_.size());
    }

    uint32_t ResourceManager::GetEnvironmentCubeMapSize() const
    {
        return static_cast<uint32_t>(this->envCubeMapsPool_.size());
    }

    void ResourceManager::ReleaseModel(ModelHandle handle)
    {
        if (handle.index >= this->modelsPool_.size())
        {
            return;
        }
        if (!this->modelsAlive_[handle.index])
        {
            return;
        }
        this->modelsAlive_[handle.index] = false;
        this->modelsGeneration_[handle.index]++;
        this->modelsPool_[handle.index].reset();
    }

    void ResourceManager::ReleaseEnvironmentCubeMap(EnvironmentCubeMapHandle handle)
    {
        if (handle.index >= this->envCubeMapsPool_.size())
        {
            return;
        }
        if (!this->envCubeMapAlive_[handle.index])
        {
            return;
        }
        this->envCubeMapAlive_[handle.index] = false;
        this->envCubeMapGeneration_[handle.index]++;
        this->envCubeMapsPool_[handle.index].reset();
    }

    // Model* ResourceManager::GetPrototype(SimpleModelType type)
    // {
    //     return this->prototypes_[type].get();
    // }

    Texture2D* ResourceManager::GetEmptyTexture2D()
    {
        return this->emptyTexture2D_.get();
    }

    Model* ResourceManager::GetSkybox()
    {
        return this->skybox_.get();
    }

    // std::unique_ptr<Model> ResourceManager::CreateFromPrototype(SimpleModelType type)
    // {
    //     return this->prototypes_[type]->Clone();
    // }

    ModelHandle ResourceManager::LoadModel(const std::string& fileName)
    {
        LOG_INFO("ResourceManager: start to load model from file: " + fileName);
        auto& device = core::Device::Instance();

        std::cout << "Loading scene from " << fileName << std::endl;
		// animationIndex = 0;
		// animationTimer = 0.0f;
		auto tStart = std::chrono::high_resolution_clock::now();
        std::unique_ptr<Model> model = std::make_unique<GLTFModel>();
        model->LoadFromFile(fileName);
		model->CreateMaterialBuffer();
		model->CreateMeshDataBuffer();
		auto tFileLoad = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();
		std::cout << "Loading took " << tFileLoad << " ms" << std::endl;

        // this->modelArray_.emplace_back(std::move(model));
        this->modelsPool_.emplace_back(std::move(model));
        this->modelsGeneration_.emplace_back(0);
        this->modelsAlive_.emplace_back(true);
        return ModelHandle{ static_cast<uint32_t>(this->modelsPool_.size() - 1), 0 };
    }
   
    
    EnvironmentCubeMapHandle ResourceManager::LoadSkyBox(const std::string& fileName)
    {
        std::cout << "Loading environment from " << fileName << std::endl;
        std::unique_ptr<EnvironmentCubeMap> cubeMap = std::make_unique<EnvironmentCubeMap>();
        // LoadFromFile 内部已调用 GenerateCubemaps，不要重复生成
        cubeMap->LoadFromFile(fileName, VK_FORMAT_R16G16B16A16_SFLOAT);
        this->envCubeMapsPool_.emplace_back(std::move(cubeMap));
        this->envCubeMapGeneration_.emplace_back(0);
        this->envCubeMapAlive_.emplace_back(true);
        return EnvironmentCubeMapHandle{ static_cast<uint32_t>(this->envCubeMapsPool_.size() - 1), 0 };
    }

}
