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
#include "engine/core/device.h"
#include "engine/utils/log.h"

#include <gli/gli.hpp>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

#include "basisu_transcoder.h"

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

namespace engine::resource
{
	struct TextureSampler 
	{
		VkFilter 				magFilter;
		VkFilter 				minFilter;
		VkSamplerAddressMode 	addressModeU;
		VkSamplerAddressMode 	addressModeV;
		VkSamplerAddressMode 	addressModeW;
	};

	class Texture {
	public:
		VkImage 				image_ = VK_NULL_HANDLE;
		VkImageLayout 			imageLayout_;
		VkDeviceMemory 			deviceMemory_ = VK_NULL_HANDLE;
		VkImageView 			view_ = VK_NULL_HANDLE;
		uint32_t 				width_;
		uint32_t 				height_;
		uint32_t 				mipLevels_;
		uint32_t 				layerCount_;
		VkDescriptorImageInfo 	descriptor_;
		VkSampler 				sampler_ = VK_NULL_HANDLE;

		Texture() = default;
		virtual ~Texture();
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;

		void 					UpdateDescriptor();

		void 					Destroy();

		void 					FromglTfImage(tinygltf::Image& gltfimage, std::string path, TextureSampler textureSampler);
	};

	class Texture2D : public Texture {
	public:
		void LoadFromFile(
			std::string filename, 
			VkFormat format,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		

		void LoadFromBuffer(
			void* buffer,
			VkDeviceSize bufferSize,
			VkFormat format,
			uint32_t width,
			uint32_t height,
			VkFilter filter = VK_FILTER_LINEAR,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	};

	class TextureCubeMap : public Texture {
	public:
		void LoadFromFile(
			std::string filename,
			VkFormat format,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	};

}