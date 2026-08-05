/*
* Basic camera class
*
* Copyright (C) 2016-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace engine::core
{
	class Camera
	{
	private:
		float fov;
		float znear, zfar;
	public:
		enum CameraType { lookat = 0, firstperson = 1 };
		CameraType type = CameraType::lookat;

		glm::vec3 rotation = glm::vec3();
		glm::vec3 position = glm::vec3();

		float rotationSpeed = 1.0f;
		float movementSpeed = 1.0f;

		bool updated = false;

		struct
		{
			glm::mat4 perspective;
			glm::mat4 view;
			
		} matrices;

		struct
		{
			bool left = false;
			bool right = false;
			bool up = false;
			bool down = false;
		} keys;

		bool moving();

		float getNearClip();

		float getFarClip(); 

		void setPerspective(float fov, float aspect, float znear, float zfar);

		void updateAspectRatio(float aspect);

		void setPosition(glm::vec3 position);

		void setRotation(glm::vec3 rotation);
		
		void rotate(glm::vec3 delta);

		void setTranslation(glm::vec3 translation);

		void translate(glm::vec3 delta);

		void updateViewMatrix();

		void update(float deltaTime);

		// Update camera passing separate axis data (gamepad)
		// Returns true if view or position has been changed
		bool updatePad(glm::vec2 axisLeft, glm::vec2 axisRight, float deltaTime);
	};

	
}