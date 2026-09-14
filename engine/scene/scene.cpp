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

    

} 














