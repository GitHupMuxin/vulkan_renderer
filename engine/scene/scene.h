#pragma once 
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "engine/resource/model.h"
#include "engine/core/buffer.h"
#include "engine/scene/camera.h"
#include "engine/scene/scene_description.h"
#include "engine/resource/environment_lighting.h"
#include "engine/resource/resource_manager.h"

namespace engine::scene
{
    struct SceneParams
    {
        glm::vec4   lightDir;
        float       exposure = 4.5f;
        float       gamma = 2.2f;
        float       prefilteredCubeMipLevels;
        float       scaleIBLAmbient = 1.0f;
        float       debugViewInputs = 0;
        float       debugViewEquation = 0;
        float       debugBsdfType = 0;   
    };

    struct UBOMatrices
    {
        glm::mat4   projection;  // 透视投影矩阵，来自 Camera.fov + aspect
        glm::mat4   model;       // 模型的世界变换（缩放+平移居中）
        glm::mat4   view;        // 观察矩阵，来自 Camera.position + rotation
        glm::vec3   camPos;      // 相机世界坐标（shader 计算视线方向用）
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
            resource::Model*                    skybox_;        // 系统资源 → 直接指针（RM::skybox_，永存活）
            resource::EnvironmentCubeMapHandle  cubeMapHandle_; // 用户资产 → Handle

            SceneParams                         params_;
            UBOMatrices                         UBOMatrices_;
            std::vector<core::Buffer>           MatricesUBOBuffers_;
            std::vector<core::Buffer>           ParamsUBOBuffers_;

            struct LightSource 
            {
    		    glm::vec3                       color = glm::vec3(1.0f);
    		    glm::vec3                       rotation = glm::vec3(75.0f, -40.0f, 0.0f);
    	    } lightSource_;

            void                                Init(const SceneDescription& desc);
            void                                UpdateUniformData(uint32_t frameIndex);
            void                                UpdateParams();
            void                                LoadAsset(const SceneDescription& desc);

            void                                AddObject(resource::ModelHandle modelHandle, glm::mat4 transform = glm::mat4(1.0f));
            void                                SetObjectTransform(uint32_t index, const glm::mat4& transform);

            void                                SetCamera(Camera* camera);
            Camera*                             GetCamera();

            // 便捷解析：handle → 指针（供渲染层每帧调用，内部做校验，失败返回 nullptr）
            resource::Model*                    GetModelAt(size_t index);
            size_t                              GetModelCount() const;
            resource::EnvironmentCubeMap*       GetCubeMap();
    };
    

}





