/*
* Vulkan utilities
*
* Copyright(C) 2018-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include "vulkan/vulkan.h"
#include "engine/core/VulkanDevice.hpp"
#if defined(__ANDROID__)
#include <android/asset_manager.h>
#elif defined(__linux__)
#include <dirent.h>
#endif

namespace engine::core
{

	extern std::string dataPath;
	/*
		Vulkan buffer object
	*/
	struct Buffer {
		VkDevice device;
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDescriptorBufferInfo descriptor;
		int32_t count = 0;
		VkDeviceSize actualBufferSize{ 0 };
		void *mapped = nullptr;
		void create(VulkanDevice *device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool map = true); 
		
		void destroy(); 
		
		void map(); 
		void unmap(); 
		void flush(VkDeviceSize size = VK_WHOLE_SIZE); 
	};

	VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage);
	

	void readDirectory(const std::string& directory, const std::string &extension, std::map<std::string, std::string> &filelist, bool recursive);
}