/*
* Vulkan texture loader
*
* Copyright(C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "engine/core/macros.h"
#include "engine/core/VulkanDevice.hpp"

#include <gli/gli.hpp>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace engine::resource
{
	class VulkanTexture {
	public:
		core::VulkanDevice *device;
		VkImage image = VK_NULL_HANDLE;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;

		void updateDescriptor();

		void destroy();
	};

	class VulkanTexture2D : public VulkanTexture {
	public:
		void loadFromFile(
			std::string filename, 
			VkFormat format,
			core::VulkanDevice *device,
			VkQueue copyQueue,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		

		void loadFromBuffer(
			void* buffer,
			VkDeviceSize bufferSize,
			VkFormat format,
			uint32_t width,
			uint32_t height,
			core::VulkanDevice *device,
			VkQueue copyQueue,
			VkFilter filter = VK_FILTER_LINEAR,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	};

	class VulkanTextureCubeMap : public VulkanTexture {
	public:
		void loadFromFile(
			std::string filename,
			VkFormat format,
			core::VulkanDevice *device,
			VkQueue copyQueue,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	};

}