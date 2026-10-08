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
    }

    void Scene::AddObject(resource::ModelHandle modelHandle, glm::mat4 transform)
    {
        auto& rm = resource::ResourceManager::Instance();

        resource::Model* model = rm.PeekModel(modelHandle);
        if (model == nullptr)
        {
            // 显式传入变换时也不能把失效 handle 放进 Scene。
            LOG_WARN("Scene: AddObject called with invalid model handle, skipped.");
            return;
        }

        if (transform == glm::mat4(1.0f))
        {
            const resource::AABB& bounds = model->GetBounds();
            if (bounds.IsValid())
            {
                const glm::vec3 size = bounds.max - bounds.min;
                const glm::vec3 center = bounds.min + 0.5f * size;
                const float extent = std::max(size.x, std::max(size.y, size.z));
                // 退化为点的模型只居中，避免自动缩放除以零。
                const float scale = extent > 0.0f ? 0.5f / extent : 1.0f;
                transform = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(scale)), -center);
            }
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

    

} 














