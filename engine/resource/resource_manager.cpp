#include <chrono>
#include <filesystem>
#include <stdexcept>
#include "engine/resource/gltf_model.h"
#include "engine/resource/asset_loader.h"
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

    TextureHandle ResourceManager::LoadTexture(const std::string& fileName, VkFormat format, VkImageViewType viewType)
    {
        if (format == VK_FORMAT_UNDEFINED)
            throw std::invalid_argument("Texture format must be specified: " + fileName);
        if (!std::filesystem::is_regular_file(fileName))
            throw std::invalid_argument("Texture file does not exist: " + fileName);

        std::unique_ptr<Texture> texture;
        switch (viewType)
        {
            case VK_IMAGE_VIEW_TYPE_2D:
            {
                auto image = std::make_unique<Texture2D>();
                image->LoadFromFile(fileName, format, VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
                texture = std::move(image);
                break;
            }
            case VK_IMAGE_VIEW_TYPE_CUBE:
            {
                auto image = std::make_unique<TextureCubeMap>();
                image->LoadFromFile(fileName, format);
                texture = std::move(image);
                break;
            }
            default:
                throw std::invalid_argument("Unsupported texture view type: " + fileName);
        }

        core::StagingRingAllocator::Instance().WaitUntil(texture->readyAt_);
        uint32_t slotIndex = 0;
        uint32_t generation = 0;
        this->texturePool_.Acquire(std::move(texture), ResourceState::Ready, &slotIndex, &generation);
        return TextureHandle{ slotIndex, generation };
    }

    const Texture* ResourceManager::GetTexture(TextureHandle handle) const noexcept
    {
        return this->texturePool_.Get(handle.index, handle.generation);
    }

    void ResourceManager::Cleanup()
    {
        this->texturePool_.Clear();
        this->modelPool_.Clear();
        this->envPool_.Clear();
        this->pendingDeletions_.clear();
        this->skybox_.reset();
        this->emptyTexture2D_.reset();
    }

    Model* ResourceManager::GetModel(ModelHandle handle)
    {
        return this->modelPool_.Get(handle.index, handle.generation);
    }

    Model* ResourceManager::PeekModel(ModelHandle handle)
    {
        return this->modelPool_.Peek(handle.index, handle.generation);
    }

    EnvironmentCubeMap* ResourceManager::GetEnvironmentCubeMap(EnvironmentCubeMapHandle handle)
    {
        return this->envPool_.Get(handle.index, handle.generation);
    }

    bool ResourceManager::IsModelAlive(ModelHandle handle) const
    {
        return this->modelPool_.IsAlive(handle.index, handle.generation);
    }

    bool ResourceManager::IsEnvironmentAlive(EnvironmentCubeMapHandle handle) const
    {
        return this->envPool_.IsAlive(handle.index, handle.generation);
    }

    uint32_t ResourceManager::GetModelSlotCount() const
    {
        return this->modelPool_.GetSlotCount();
    }

    uint32_t ResourceManager::GetEnvironmentCubeMapSlotCount() const
    {
        return this->envPool_.GetSlotCount();
    }

    void ResourceManager::ReleaseModel(ModelHandle handle)
    {
        // 不立即销毁：GPU 可能还在用该模型的 VBO/IBO。进待删队列，等 frameCount 帧
        // （保证所有 in-flight 帧完成）后由 OnFrameCompleted 真正销毁。
        if (!this->modelPool_.Release(handle.index, handle.generation))
        {
            return;
        }
        const uint32_t retireFrame = this->currentFrame_ + this->frameCount_;
        this->pendingDeletions_.push_back({ ResourceKind::Model, handle.index, retireFrame });
        LOG_INFO("ResourceManager: model[" << handle.index << "] queued for deferred delete, retire at frame " << retireFrame);
    }

    void ResourceManager::ReleaseEnvironmentCubeMap(EnvironmentCubeMapHandle handle)
    {
        if (!this->envPool_.Release(handle.index, handle.generation))
        {
            return;
        }
        this->pendingDeletions_.push_back({
            ResourceKind::EnvironmentCubeMap, handle.index, this->currentFrame_ + this->frameCount_
        });
    }

    void ResourceManager::ReleaseTexture(TextureHandle handle)
    {
        if (!this->texturePool_.Release(handle.index, handle.generation))
        {
            return;
        }
        this->pendingDeletions_.push_back({
            ResourceKind::Texture, handle.index, this->currentFrame_ + this->frameCount_
        });
    }

    void ResourceManager::OnFrameCompleted()
    {
        this->currentFrame_++;

        // Uploading → Ready：GPU 上传回执达成（顶点/索引/材质/纹理全部完成）的模型转为可渲染
        for (auto& entry : this->modelPool_.GetEntries())
        {
            if (entry.state == ResourceState::Uploading && entry.resource->IsReady())
            {
                entry.state = ResourceState::Ready;
                LOG_INFO("ResourceManager: model upload completed, ready to render.");
            }
        }

        for (auto it = this->pendingDeletions_.begin(); it != this->pendingDeletions_.end(); )
        {
            if (it->retireFrame > this->currentFrame_)
            {
                ++it;
                continue;
            }

            // 到期：该帧的 GPU 工作已确认完成，资源不再被使用，安全销毁
            switch (it->kind)
            {
                case ResourceKind::Model:
                    this->modelPool_.DestroySlot(it->slotIndex);
                    LOG_INFO("ResourceManager: model[" << it->slotIndex << "] destroyed at frame " << this->currentFrame_);
                    break;
                case ResourceKind::EnvironmentCubeMap:
                    this->envPool_.DestroySlot(it->slotIndex);
                    break;
                case ResourceKind::Texture:
                    this->texturePool_.DestroySlot(it->slotIndex);
                    break;
            }
            it = this->pendingDeletions_.erase(it);
        }
    }

    Texture2D* ResourceManager::GetEmptyTexture2D()
    {
        return this->emptyTexture2D_.get();
    }

    GLTFModelBase* ResourceManager::GetSkybox()
    {
        return this->skybox_.get();
    }

    ModelHandle ResourceManager::LoadModel(const std::string& fileName)
    {
        LOG_INFO("ResourceManager: start to load model from file: " + fileName);
        const auto tStart = std::chrono::high_resolution_clock::now();
        ModelData data;
        std::string error;
        if (!AssetLoader::LoadModelData(fileName, &data, &error))
        {
            // 导入失败不能登记空资源；沿用加载异常交给调用方处理。
            throw std::runtime_error(error);
        }

        auto model = std::make_unique<Model>();
        try
        {
            if (!model->Create(data, &error))
            {
                throw std::runtime_error(error);
            }
        }
        catch (...)
        {
            // 部分资源可能已提交上传，局部 Model 析构前必须等这些拷贝结束。
            core::StagingRingAllocator::Instance().WaitAll();
            throw;
        }
        const auto tFileLoad = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - tStart).count();
        LOG_INFO("ResourceManager: model loaded in " << tFileLoad << " ms: " << fileName);

        uint32_t slotIndex = 0;
        uint32_t generation = 0;
        // 异步上传已提交但未等待：标记 Uploading，GPU 回执达成后由 OnFrameCompleted 转 Ready。
        // PeekModel 可提取 CPU 绘制任务；GetModel 在绘制端拦住未就绪资源。
        this->modelPool_.Acquire(std::move(model), ResourceState::Uploading, &slotIndex, &generation);

        return ModelHandle{ slotIndex, generation };
    }
   
    
    EnvironmentCubeMapHandle ResourceManager::LoadEnvironment(const std::string& fileName)
    {
        LOG_INFO("ResourceManager: loading environment from file: " << fileName);
        std::unique_ptr<EnvironmentCubeMap> cubeMap = std::make_unique<EnvironmentCubeMap>();
        // LoadFromFile 内部已调用 GenerateCubemaps，不要重复生成
        cubeMap->LoadFromFile(fileName, VK_FORMAT_R16G16B16A16_SFLOAT);
        uint32_t slotIndex = 0;
        uint32_t generation = 0;
        this->envPool_.Acquire(std::move(cubeMap), ResourceState::Ready, &slotIndex, &generation);
        return EnvironmentCubeMapHandle{ slotIndex, generation };
    }

}
