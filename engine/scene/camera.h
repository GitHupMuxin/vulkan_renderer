#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>


namespace engine::scene
{
    enum class CameraType { lookat = 0, firstperson = 1 };

    class Camera
    {
        private:
            float       fov_;
            float       znear_;
            float       zfar_;
            
        public:
            struct
            {
                bool    left = false;
                bool    right = false;
                bool    up = false;
                bool    down = false;
            } keys_;

            bool        Moving();

            CameraType  type = CameraType::lookat; 

            glm::vec3   rotation_ = glm::vec3();
            glm::vec3   position_ = glm::vec3();

            float       rotationSpeed_ = 1.0f;
            float       movementSpeed_ = 1.0f;

            bool        updated_ = false;

            struct 
            {
                glm::mat4   perspective_;
                glm::mat4   view_;
            } matrices_;            

            Camera();
            ~Camera();
        
            void            SetPerspective(float fov, float aspect, float znear, float zfar);
            void            SetPosition(glm::vec3 delta);
            void            SetRotation(glm::vec3 delta);

            void            UpdateAspectRatio(float aspect);
            void            UpdateViewMatrix();

            void            Update(float deltaTime);
    };
}


