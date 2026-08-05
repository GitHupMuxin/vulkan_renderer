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
        std::string sceneFile1 = ResourceManager::assetPath_ + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
		std::string sceneFile2 = ResourceManager::assetPath_ + "models/MetalRoughSpheres/glTF-Embedded/MetalRoughSpheres.gltf";
		std::string envMapFile = ResourceManager::assetPath_ + "environments/papermill.ktx";
        std::string skyboxFile = ResourceManager::assetPath_ + "models/Box/glTF-Embedded/Box.gltf";
        std::string emptyTexture2DFile = this->assetPath_ + "textures/empty.ktx";


        this->skybox_ = std::make_unique<GLTFModel>();
        this->skybox_->LoadFromFile(skyboxFile);
        this->skybox_->CreateMaterialBuffer();
        this->skybox_->CreateMeshDataBuffer();

        this->emptyTexture2D_ = std::make_unique<Texture2D>();
        this->emptyTexture2D_->LoadFromFile(emptyTexture2DFile, VK_FORMAT_R8G8B8A8_UNORM);

        // this->usageModel_ = std::make_unique<GLTFModel>();
        // this->usageModel_->LoadFromFile(sceneFile1); 
        // this->usageModel_->CreateMaterialBuffer();
        // this->usageModel_->CreateMeshDataBuffer();

        this->LoadModel(sceneFile1);
        this->LoadModel(sceneFile2);

        auto cubeMap_ = std::make_unique<EnvironmentCubeMap>();
        cubeMap_->LoadFromFile(envMapFile, VK_FORMAT_R16G16B16A16_SFLOAT);
        this->CubeMap_.emplace_back(std::move(cubeMap_));

    }

    Model* ResourceManager::GetModelInstance(uint32_t i)
    {
        return this->modelArray_[i].get();
    }

    EnvironmentCubeMap* ResourceManager::GetEnvironmentCubeMap(uint32_t i)
    {
        return this->CubeMap_[i].get();
    }

    Model* ResourceManager::GetPrototype(SimpleModelType type)
    {
        return this->prototypes_[type].get();
    }

    Texture2D* ResourceManager::GetEmptyTexture2D()
    {
        return this->emptyTexture2D_.get();
    }

    std::unique_ptr<Model> ResourceManager::CreateFromPrototype(SimpleModelType type)
    {
        return this->prototypes_[type]->Clone();
    }

    uint32_t ResourceManager::LoadModel(const std::string& fileName)
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

        this->modelArray_.emplace_back(std::move(model));
        return this->modelArray_.size() - 1;
    }
   
    
    uint32_t ResourceManager::LoadSkyBox(const std::string& fileName)
    {
        std::cout << "Loading environment from " << fileName << std::endl;
        std::unique_ptr<EnvironmentCubeMap> cubeMap = std::make_unique<EnvironmentCubeMap>();
        if (cubeMap->environmentCube_.image_) {
			cubeMap->environmentCube_.Destroy();
			cubeMap->irradianceCube_.Destroy();
			cubeMap->prefilteredCube_.Destroy();
		}
		cubeMap->LoadFromFile(fileName, VK_FORMAT_R16G16B16A16_SFLOAT);
		cubeMap->GenerateCubemaps();
        this->CubeMap_.emplace_back(std::move(cubeMap));
        return this->CubeMap_.size();
    }

}


