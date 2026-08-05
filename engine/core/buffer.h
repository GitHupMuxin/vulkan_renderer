#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include "vulkan/vulkan.h"
#include "engine/core/Device.h"

namespace engine::core
{

	extern std::string dataPath;
	/*
		Vulkan buffer object
	*/
	struct Buffer {
		VkDevice device = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDescriptorBufferInfo descriptor;
		int32_t count = 0;
		VkDeviceSize actualBufferSize{ 0 };
		void *mapped = nullptr;

		Buffer() = default;
		~Buffer();
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;
		Buffer(Buffer&& other) noexcept;
		Buffer& operator=(Buffer&& other) noexcept;

		void Create(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool map = true);
		void Destroy();
		void Map();
		void Unmap();
		void Flush(VkDeviceSize size = VK_WHOLE_SIZE);
	};

	VkPipelineShaderStageCreateInfo LoadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage);
	

	void ReadDirectory(const std::string& directory, const std::string &extension, std::map<std::string, std::string> &filelist, bool recursive);
}