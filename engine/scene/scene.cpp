#include <algorithm>
#include <cstring>
#include "engine/scene/scene.h"
#include "engine/resource/resource_manager.h"

namespace engine::scene
{


    void Scene::Init()
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

        this->LoadAsset();
        this->UpdateUniformData(0);
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


    void Scene::LoadAsset()
    {
        LOG_INFO("Scene: start to load assets...");
        // this->AddObject(resource::ResourceManager::Instance().usageModel_.get());
        for (int i = 0; i < resource::ResourceManager::Instance().modelArray_.size(); i++)
        {
            this->AddObject(resource::ResourceManager::Instance().GetModelInstance(i));
        }
        this->skybox_ = resource::ResourceManager::Instance().skybox_.get();
        this->cubeMap_ = resource::ResourceManager::Instance().GetEnvironmentCubeMap(0);
    }

    void Scene::AddObject(resource::Model* model, glm::mat4 transform)
    {
        // If the caller didn't provide a transform, compute one that centers the
        // model's AABB at the origin and scales its longest axis to fit the screen
        if (transform == glm::mat4(1.0f))
        {
            glm::mat4 aabb = model->GetAABBBox();
            glm::vec3 size  = glm::vec3(aabb[0][0], aabb[1][1], aabb[2][2]);
            glm::vec3 min   = glm::vec3(aabb[3][0], aabb[3][1], aabb[3][2]);
            glm::vec3 center = min + 0.5f * size;

            float scale = (1.0f / std::max(size.x, std::max(size.y, size.z))) * 0.5f;
            transform = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(scale)), -center);
        }

        SceneObject object;
        object.model = model;
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
    

} 















