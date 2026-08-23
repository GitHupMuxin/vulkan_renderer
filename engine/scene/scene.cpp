#include <algorithm>
#include <cstring>
#include "engine/scene/scene.h"
#include "engine/resource/resource_manager.h"

namespace engine::scene
{


    void Scene::Init(const SceneDescription& desc)
    {
        LOG_INFO("Scene: start to init scene...");

        this->LoadAsset(desc);

        // prefiltered cube map 的 mip 层数由生成阶段决定（numMips），
        // shader 用它计算 specular IBL 的 textureLod 层级，
        // 必须从生成好的环境贴图查询，否则未初始化导致 lod 越界
        if (auto* cubeMap = this->GetCubeMap())
        {
            this->params_.prefilteredCubeMipLevels = static_cast<float>(cubeMap->GetPrefilteredCubeMipLevels());
        }

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
        // PeekModel：上传在途（Uploading）也算资源存在；GetAABBBox 是纯 CPU 数据，无需 GPU 就绪
        if (transform == glm::mat4(1.0f))
        {
            resource::Model* model = rm.PeekModel(modelHandle);
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

    void Scene::SetCamera(Camera* camera)
    {
        this->camera_ = camera;
    }

    const Camera* Scene::GetCamera() const
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















