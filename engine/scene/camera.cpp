#include "engine/scene/camera.h"


namespace engine::scene
{

    bool Camera::Moving()
    {
        return this->keys_.left || this->keys_.right || this->keys_.up || this->keys_.down;
    }

    Camera::Camera()
    {

    }

    Camera::~Camera()
    {

    }        
 
    void Camera::SetPerspective(float fov, float aspect, float znear, float zfar)
    {
        this->fov_ = fov;
        this->znear_ = znear;
        this->zfar_ = zfar;
        this->matrices_.perspective_ = glm::perspective(glm::radians(fov), aspect, this->znear_, this->zfar_);
    }            
    
    
    void Camera::SetPosition(glm::vec3 position)
    {
        this->position_ = position;
        this->UpdateViewMatrix();
    }

    void Camera::SetRotation(glm::vec3 rotation)
    {
        this->rotation_ = rotation;
        this->UpdateViewMatrix(); 
    }

    void Camera::UpdateAspectRatio(float aspect)
    {
        this->matrices_.perspective_ = glm::perspective(glm::radians(this->fov_), aspect, this->znear_, this->zfar_);
    }  
    
    void Camera::UpdateViewMatrix()
    {
        glm::mat4 rotM = glm::mat4(1.0f);
        glm::mat4 transM;

        rotM = glm::rotate(rotM, glm::radians(this->rotation_.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(this->rotation_.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(this->rotation_.z), glm::vec3(0.0f, 0.0f, 1.0f));

        transM = glm::translate(glm::mat4(1.0f), this->position_ * glm::vec3(1.0f, 1.0f, -1.0f));

        if (type == CameraType::firstperson)
        {
            this->matrices_.view_ = rotM * transM;
        }
        else
        {
            this->matrices_.view_ = transM * rotM;
        }

        this->updated_ = true;
    }

    
    void Camera::Update(float deltaTime)
    {
        this->updated_ = false;
        if (type == CameraType::firstperson)
        {
            if (this->Moving())
            {
                glm::vec3 camFront;
                camFront.x = -cos(glm::radians(this->rotation_.x)) * sin(glm::radians(this->rotation_.y));
                camFront.y = sin(glm::radians(this->rotation_.x));
                camFront.z = -cos(glm::radians(this->rotation_.x)) * cos(glm::radians(this->rotation_.y));
                camFront = glm::normalize(camFront);

                float moveSpeed = deltaTime * this->movementSpeed_;

                if (this->keys_.up)
                    this->position_ += camFront * moveSpeed;
                if (this->keys_.down)
                    this->position_ -= camFront * moveSpeed;
                if (this->keys_.left)
                    this->position_ += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * moveSpeed;
                if (this->keys_.right)
                    this->position_ -= glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * moveSpeed;

                this->UpdateViewMatrix();
            }
        }
    }

}
