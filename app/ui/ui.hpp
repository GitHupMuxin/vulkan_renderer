/*
* Vulkan Example - Physical based rendering a glTF 2.0 model with image based lighting
*
* Copyright (C) 2018-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vector>
#include <array>
#include <map>

#include "vulkan/vulkan.h"
#include "imgui/imgui.h"
#include "engine/core/device.h"
#include "engine/core/buffer.h"

#include "engine/resource/texture.h"

#include "engine/utils/log.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace app::ui
{
	struct MouseButtons 
	{
		bool left = false;
		bool right = false;
		bool middle = false;
	};
	
	struct UI 
	{
		private:

		public:
			engine::core::Buffer 		vertexBuffer;
			engine::core::Buffer 		indexBuffer;
			engine::resource::Texture2D fontTexture;
			VkPipelineLayout 			pipelineLayout;
			VkPipeline 					pipeline;
			VkDescriptorPool 			descriptorPool;
			VkDescriptorSetLayout 		descriptorSetLayout;
			VkDescriptorSet 			descriptorSet;
			float 						updateTimer = 0.0f;

			struct PushConstBlock 
			{
				glm::vec2 scale;
				glm::vec2 translate;
			};
			PushConstBlock 				pushConstBlock;

			UI(VkRenderPass renderPass, VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount); 
			
			~UI();

			void 						Draw(VkCommandBuffer cmdBuffer); 

			bool 						Header(const char *caption);
			
			bool 						Slider(const char* caption, float* value, float min, float max); 
			bool 						Combo(const char *caption, int32_t *itemindex, std::vector<std::string> items);
			bool 						Combo(const char *caption, std::string &selectedkey, std::map<std::string, std::string> items);
			bool 						Button(const char *caption);
			void 						Text(const char *formatstr, ...); 

			
			template<typename T>
			bool Checkbox(const char* caption, T *value) 
			{
				bool val = (*value == 1);
				bool res = ImGui::Checkbox(caption, &val);
				*value = val;
				return res;
			}
	};

}