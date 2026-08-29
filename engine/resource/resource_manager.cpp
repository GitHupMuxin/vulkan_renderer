#include <chrono>
#include "engine/resource/model.h"
#include "engine/resource/resource_manager.h"
#include "engine/core/staging_ring_allocator.h"
#include "engine/utils/log.h"


namespace engine::resource
{
    const std::string ResourceManager::assetPath_ = VK_EXAMPLE_DATA_DIR;

    ResourceManager& ResourceManager::Instance()
    {
        static ResourceManager instance;
        return instance;
    }

    ResourceManager::~ResourceManager()
    {

    }

    void ResourceManager::Init()
    {
        LOG_INFO("ResourceManager: start to init resource manager...");
        this->frameCount_ = core::Device::Instance().GetSetting().frameCount_;
        std::string skyboxFile = ResourceManager::assetPath_ + "models/Box/glTF-Embedded/Box.gltf";
        std::string emptyTexture2DFile = this->assetPath_ + "textures/empty.ktx";

        // 系统资源：skybox 立方体 + 空纹理，应用整个生命周期存活，不走 Handle
        this->skybox_ = std::make_unique<GLTFModel>();
        this->skybox_->LoadFromFile(skyboxFile);
        this->skybox_->CreateMaterialBuffer();
        this->skybox_->CreateMeshDataBuffer();

        this->emptyTexture2D_ = std::make_unique<Texture2D>();
        this->emptyTexture2D_->LoadFromFile(emptyTexture2DFile, VK_FORMAT_R8G8B8A8_UNORM);

        core::StagingRingAllocator::Instance().WaitAll();
    }

    void ResourceManager::Cleanup()
    {
        for (auto& slot : this->modelSlots_)
        {
            slot.resource.reset();
        }
        for (auto& slot : this->envSlots_)
        {
            slot.resource.reset();
        }
        this->pendingDeletions_.clear();
        this->skybox_.reset();
        this->emptyTexture2D_.reset();
    }

    Model* ResourceManager::GetModel(ModelHandle handle)
    {
        if (handle.index >= this->modelSlots_.size())
        {
            return nullptr;
        }
        auto& slot = this->modelSlots_[handle.index];
        if (slot.state != ResourceState::Ready)
        {
            return nullptr;
        }
        if (slot.generation != handle.generation)
        {
            return nullptr;
        }
        return slot.resource.get();
    }

    Model* ResourceManager::PeekModel(ModelHandle handle)
    {
        if (handle.index >= this->modelSlots_.size())
        {
            return nullptr;
        }
        auto& slot = this->modelSlots_[handle.index];
        if (slot.generation != handle.generation)
        {
            return nullptr;
        }
        return slot.resource.get();
    }

    EnvironmentCubeMap* ResourceManager::GetEnvironmentCubeMap(EnvironmentCubeMapHandle handle)
    {
        if (handle.index >= this->envSlots_.size())
        {
            return nullptr;
        }
        auto& slot = this->envSlots_[handle.index];
        if (slot.state != ResourceState::Ready)
        {
            return nullptr;
        }
        if (slot.generation != handle.generation)
        {
            return nullptr;
        }
        return slot.resource.get();
    }

    bool ResourceManager::IsModelAlive(ModelHandle handle) const
    {
        return handle.index < this->modelSlots_.size()
            && this->modelSlots_[handle.index].state == ResourceState::Ready
            && this->modelSlots_[handle.index].generation == handle.generation;
    }

    bool ResourceManager::IsEnvironmentAlive(EnvironmentCubeMapHandle handle) const
    {
        return handle.index < this->envSlots_.size()
            && this->envSlots_[handle.index].state == ResourceState::Ready
            && this->envSlots_[handle.index].generation == handle.generation;
    }

    uint32_t ResourceManager::GetModelSize() const
    {
        return static_cast<uint32_t>(this->modelSlots_.size());
    }

    uint32_t ResourceManager::GetEnvironmentCubeMapSize() const
    {
        return static_cast<uint32_t>(this->envSlots_.size());
    }

    void ResourceManager::ReleaseModel(ModelHandle handle)
    {
        if (handle.index >= this->modelSlots_.size())
        {
            return;
        }
        auto& slot = this->modelSlots_[handle.index];
        if (slot.state != ResourceState::Ready)
        {
            return;
        }
        slot.state = ResourceState::PendingDelete;
        slot.generation++;
        // ② 不立即 reset()：GPU 可能还在用该模型的 VBO/IBO。
        //    进待删队列，等 frameCount 帧（保证所有 in-flight 帧完成）后由 OnFrameCompleted 真正销毁
        this->pendingDeletions_.push_back({
            handle.index, true, this->currentFrame_ + this->frameCount_
        });
        LOG_INFO("ResourceManager: model[" << handle.index << "] queued for deferred delete, retire at frame " << (this->currentFrame_ + this->frameCount_));
    }

    void ResourceManager::ReleaseEnvironmentCubeMap(EnvironmentCubeMapHandle handle)
    {
        if (handle.index >= this->envSlots_.size())
        {
            return;
        }
        auto& slot = this->envSlots_[handle.index];
        if (slot.state != ResourceState::Ready)
        {
            return;
        }
        slot.state = ResourceState::PendingDelete;
        slot.generation++;
        this->pendingDeletions_.push_back({
            handle.index, false, this->currentFrame_ + this->frameCount_
        });
    }

    void ResourceManager::OnFrameCompleted()
    {
        this->currentFrame_++;

        // Uploading → Ready：GPU 上传回执达成（顶点/索引/材质/纹理全部完成）的模型转为可渲染
        for (auto& slot : this->modelSlots_)
        {
            if (slot.state == ResourceState::Uploading && slot.resource->IsReady())
            {
                slot.state = ResourceState::Ready;
                LOG_INFO("ResourceManager: model upload completed, ready to render.");
            }
        }

        for (auto it = this->pendingDeletions_.begin(); it != this->pendingDeletions_.end(); )
        {
            if (it->retireFrame <= this->currentFrame_)
            {
                // 到期：该帧的 GPU 工作已确认完成，资源不再被使用，安全销毁
                if (it->isModel && it->poolIndex < this->modelSlots_.size())
                {
                    auto& slot = this->modelSlots_[it->poolIndex];
                    slot.resource.reset();          // 真正销毁 GPU 资源
                    slot.state = ResourceState::Free;
                    LOG_INFO("ResourceManager: model[" << it->poolIndex << "] destroyed at frame " << this->currentFrame_);
                }
                else if (!it->isModel && it->poolIndex < this->envSlots_.size())
                {
                    auto& slot = this->envSlots_[it->poolIndex];
                    slot.resource.reset();
                    slot.state = ResourceState::Free;
                }
                it = this->pendingDeletions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    Texture2D* ResourceManager::GetEmptyTexture2D()
    {
        return this->emptyTexture2D_.get();
    }

    Model* ResourceManager::GetSkybox()
    {
        return this->skybox_.get();
    }

    ModelHandle ResourceManager::LoadModel(const std::string& fileName)
    {
        LOG_INFO("ResourceManager: start to load model from file: " + fileName);
        auto& device = core::Device::Instance();

		// animationIndex = 0;
		// animationTimer = 0.0f;
		auto tStart = std::chrono::high_resolution_clock::now();
        std::unique_ptr<Model> model = std::make_unique<GLTFModel>();
        model->LoadFromFile(fileName);
		model->CreateMaterialBuffer();
		model->CreateMeshDataBuffer();
        model->CreateDescriptorSet();
		auto tFileLoad = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();
		LOG_INFO("ResourceManager: model loaded in " << tFileLoad << " ms: " << fileName);

        // this->modelArray_.emplace_back(std::move(model));
        ResourceSlot<Model> slot;
        slot.resource = std::move(model);
        slot.generation = 0;
        // 异步上传已提交但未等待：标记 Uploading，GPU 回执达成后由 OnFrameCompleted 转 Ready。
        // 期间 GetModel 校验 state != Ready 返回 nullptr → SceneExtractor 不生成 RenderItem → 不渲染。
        slot.state = ResourceState::Uploading;
        this->modelSlots_.emplace_back(std::move(slot));

        return ModelHandle{ static_cast<uint32_t>(this->modelSlots_.size() - 1), 0 };
    }
   
    
    EnvironmentCubeMapHandle ResourceManager::LoadSkyBox(const std::string& fileName)
    {
        LOG_INFO("ResourceManager: loading environment from file: " << fileName);
        std::unique_ptr<EnvironmentCubeMap> cubeMap = std::make_unique<EnvironmentCubeMap>();
        // LoadFromFile 内部已调用 GenerateCubemaps，不要重复生成
        cubeMap->LoadFromFile(fileName, VK_FORMAT_R16G16B16A16_SFLOAT);
        ResourceSlot<EnvironmentCubeMap> slot;
        slot.resource = std::move(cubeMap);
        slot.generation = 0;
        slot.state = ResourceState::Ready;
        this->envSlots_.emplace_back(std::move(slot));
        return EnvironmentCubeMapHandle{ static_cast<uint32_t>(this->envSlots_.size() - 1), 0 };
    }

}
