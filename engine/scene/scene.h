#pragma once 
#include <vector>
#include <glm/glm.hpp>
#include "engine/resource/model.h"
#include "engine/scene/camera.h"
#include "engine/scene/config/scene_description.h"
#include "engine/resource/resource_manager.h"

namespace engine::scene
{
    struct SceneParams
    {
        glm::vec4   lightDir;
        float       exposure = 4.5f;
        float       gamma = 2.2f;
        float       scaleIBLAmbient = 1.0f;
        float       debugViewInputs = 0;
        float       debugViewEquation = 0;
        float       debugBsdfType = 0;   
    };

    struct SceneObject
    {
        resource::ModelHandle   modelHandle;    // 用户资产 → Handle（可校验失效）
        glm::mat4               transform = glm::mat4(1.0f);
    };

    class Scene
    {
        private:
            Camera*                             camera_;

        public:
            std::vector<SceneObject>            sceneObjects_;

            SceneParams                         params_;

            struct LightSource 
            {
    		    glm::vec3                       color = glm::vec3(1.0f);
    		    glm::vec3                       rotation = glm::vec3(75.0f, -40.0f, 0.0f);
    	    } lightSource_;

            void                                Init(const SceneDescription& desc);
            void                                LoadAsset(const SceneDescription& desc);

            void                                AddObject(resource::ModelHandle modelHandle, glm::mat4 transform = glm::mat4(1.0f));

            void                                SetCamera(Camera* camera);
            const Camera*                       GetCamera() const;

            // 便捷解析：handle → 指针（供 SceneExtractor/应用层调用，内部做校验，失败返回 nullptr）
            resource::Model*                    GetModelAt(size_t index);
            size_t                              GetModelCount() const;
    };
    

}





