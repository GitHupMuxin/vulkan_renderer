#include <algorithm>
#include <cstring>
#include "engine/scene/scene.h"
#include "engine/resource/resource_manager.h"

namespace engine::scene
{


    void Scene::Init(const SceneDescription& desc)
    {
        LOG_INFO("Scene: start to init scene...");
        auto& device = core::Device::Instance();

        uint32_t frameCount = device.GetSetting().frameCount_;

        this->ParamsUBOBuffers_.resize(frameCount);
        this->MatricesUBOBuffers_.resize(frameCount);
        
        for (int i = 0; i < this->MatricesUBOBuffers_.size(); i++)
        {
            this->ParamsUBOBuffers_[i].Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(this->params_));
            this->MatricesUBOBuffers_[i].Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(this->UBOMatrices_));
        }

        this->LoadAsset(desc);

        // prefiltered cube map 的 mip 层数由生成阶段决定（numMips），
        // shader 用它计算 specular IBL 的 textureLod 层级，
        // 必须从生成好的环境贴图查询，否则未初始化导致 lod 越界
        if (auto* cubeMap = this->GetCubeMap())
        {
            this->params_.prefilteredCubeMipLevels = static_cast<float>(cubeMap->GetPrefilteredCubeMipLevels());
        }

        this->UpdateUniformData(0);

        // Handle 失效自检：加载一个临时模型 → 释放 → 应返回 nullptr。
        // 纯 CPU 操作，不进入渲染热路径，不碰场景已有资源。
        {
            auto& rm = resource::ResourceManager::Instance();
            resource::ModelHandle probe = rm.LoadModel(rm.assetPath_ + "models/Box/glTF-Embedded/Box.gltf");
            rm.ReleaseModel(probe);
            if (rm.GetModel(probe) == nullptr)
            {
                LOG_INFO("Resource handle invalidation self-check PASSED");
            }
            else
            {
                LOG_ERROR("Resource handle invalidation self-check FAILED");
            }
        }
    }

    void Scene::UpdateUniformData(uint32_t frameIndex)
    {
        // Scene-level camera matrices
		this->UBOMatrices_.projection = this->camera_->matrices_.perspective_;
		this->UBOMatrices_.view = this->camera_->matrices_.view_;

        // Model matrix comes from the (single) scene object's own transform
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        if (!this->sceneObjects_.empty())
        {
            modelMatrix = this->sceneObjects_[0].transform;
        }
		this->UBOMatrices_.model = modelMatrix;

		// Shader requires camera position in world space
		glm::mat4 cv = glm::inverse(this->camera_->matrices_.view_);
		this->UBOMatrices_.camPos = glm::vec3(cv[3]);

        // Upload scene UBOs for this frame in flight
        memcpy(this->MatricesUBOBuffers_[frameIndex].mapped, &this->UBOMatrices_, sizeof(this->UBOMatrices_));
        memcpy(this->ParamsUBOBuffers_[frameIndex].mapped, &this->params_, sizeof(this->params_));
    }

    void Scene::UpdateParams()
	{
		this->params_.lightDir = glm::vec4(
			sin(glm::radians(this->lightSource_.rotation.x)) * cos(glm::radians(this->lightSource_.rotation.y)),
			sin(glm::radians(this->lightSource_.rotation.y)),
			cos(glm::radians(this->lightSource_.rotation.x)) * cos(glm::radians(this->lightSource_.rotation.y)),
			0.0f);
	}


    void Scene::LoadAsset(const SceneDescription& desc)
    {
        LOG_INFO("Scene: start to load assets...");
        auto& rm = resource::ResourceManager::Instance();

        // 按 SceneDescription 加载用户资产，而非默认获取全部资源
        for (const auto& path : desc.modelPaths)
        {
            resource::ModelHandle handle = rm.LoadModel(rm.assetPath_ + path);
            this->AddObject(handle);
        }

        // skybox 是系统资源，直接取指针（永存活）
        this->skybox_ = rm.GetSkybox();

        // 环境贴图是用户资产，存 Handle（可校验/未来可替换）
        if (!desc.environmentPath.empty())
        {
            this->cubeMapHandle_ = rm.LoadSkyBox(rm.assetPath_ + desc.environmentPath);
        }
    }

    void Scene::AddObject(resource::ModelHandle modelHandle, glm::mat4 transform)
    {
        auto& rm = resource::ResourceManager::Instance();

        // 计算默认 transform 需要 model 的 AABB（此时 handle 一定有效，因为刚加载）
        if (transform == glm::mat4(1.0f))
        {
            resource::Model* model = rm.GetModel(modelHandle);
            if (model == nullptr)
            {
                LOG_WARN("Scene: AddObject called with invalid model handle, skipped.");
                return;
            }
            glm::mat4 aabb = model->GetAABBBox();
            glm::vec3 size  = glm::vec3(aabb[0][0], aabb[1][1], aabb[2][2]);
            glm::vec3 min   = glm::vec3(aabb[3][0], aabb[3][1], aabb[3][2]);
            glm::vec3 center = min + 0.5f * size;

            float scale = (1.0f / std::max(size.x, std::max(size.y, size.z))) * 0.5f;
            transform = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(scale)), -center);
        }

        SceneObject object;
        object.modelHandle = modelHandle;
        object.transform = transform;
        this->sceneObjects_.emplace_back(object);
    }

    void Scene::SetObjectTransform(uint32_t index, const glm::mat4& transform)
    {
        if (index < this->sceneObjects_.size())
        {
            this->sceneObjects_[index].transform = transform;
        }
    }


    void Scene::SetCamera(Camera* camera)
    {
        this->camera_ = camera;
    }

    Camera* Scene::GetCamera()
    {
        return this->camera_;
    }

    resource::Model* Scene::GetModelAt(size_t index)
    {
        if (index >= this->sceneObjects_.size())
        {
            return nullptr;
        }
        return resource::ResourceManager::Instance().GetModel(this->sceneObjects_[index].modelHandle);
    }

    size_t Scene::GetModelCount() const
    {
        return this->sceneObjects_.size();
    }

    resource::EnvironmentCubeMap* Scene::GetCubeMap()
    {
        return resource::ResourceManager::Instance().GetEnvironmentCubeMap(this->cubeMapHandle_);
    }
    

} 















