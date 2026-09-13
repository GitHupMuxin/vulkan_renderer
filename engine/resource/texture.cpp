#include "engine/resource/texture.h"
#include "engine/core/staging_ring_allocator.h"

namespace engine::resource
{
    Texture::Texture(Texture&& other) noexcept
        : image_(other.image_)
        , imageLayout_(other.imageLayout_)
        , deviceMemory_(other.deviceMemory_)
        , view_(other.view_)
        , width_(other.width_)
        , height_(other.height_)
        , mipLevels_(other.mipLevels_)
        , layerCount_(other.layerCount_)
        , descriptor_(other.descriptor_)
        , sampler_(other.sampler_)
    {
        other.image_ = VK_NULL_HANDLE;
        other.deviceMemory_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other) {
            this->Destroy();
            this->image_ = other.image_;
            this->imageLayout_ = other.imageLayout_;
            this->deviceMemory_ = other.deviceMemory_;
            this->view_ = other.view_;
            this->width_ = other.width_;
            this->height_ = other.height_;
            this->mipLevels_ = other.mipLevels_;
            this->layerCount_ = other.layerCount_;
            this->descriptor_ = other.descriptor_;
            this->sampler_ = other.sampler_;
            other.image_ = VK_NULL_HANDLE;
            other.deviceMemory_ = VK_NULL_HANDLE;
            other.view_ = VK_NULL_HANDLE;
            other.sampler_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    Texture::~Texture()
    {
        this->Destroy();
    }

    void Texture::UpdateDescriptor()
    {
        this->descriptor_.sampler = this->sampler_;
        this->descriptor_.imageView = this->view_;
        this->descriptor_.imageLayout = this->imageLayout_;
    }

    void Texture::Destroy()
    {
        auto& device = core::Device::Instance();
        if (this->view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device.GetLogicalDeviceHandle(), this->view_, nullptr);
            this->view_ = VK_NULL_HANDLE;
        }
        if (this->sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device.GetLogicalDeviceHandle(), this->sampler_, nullptr);
            this->sampler_ = VK_NULL_HANDLE;
        }
        if (this->image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device.GetLogicalDeviceHandle(), this->image_, nullptr);
            this->image_ = VK_NULL_HANDLE;
        }
        if (this->deviceMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device.GetLogicalDeviceHandle(), this->deviceMemory_, nullptr);
            this->deviceMemory_ = VK_NULL_HANDLE;
        }
    }

    // Loads the image for this texture. Supports both glTF's web formats (jpg, png, embedded and external files) as well as external KTX2 files with basis universal texture compression
	void Texture::FromglTfImage(tinygltf::Image &gltfimage, std::string path, TextureSampler textureSampler)
	{
		auto& device = core::Device::Instance();

		// KTX2 files need to be handled explicitly
		bool isKtx2 = false;
		if (gltfimage.uri.find_last_of(".") != std::string::npos) {
			if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx2") {
				isKtx2 = true;
			}
		}

		VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

		if (isKtx2) {
			// Image is KTX2 using basis universal compression. Those images need to be loaded from disk and will be transcoded to a native GPU format

			basist::ktx2_transcoder ktxTranscoder;
			const std::string filename = path + "\\" + gltfimage.uri;
			std::ifstream ifs(filename, std::ios::binary | std::ios::in | std::ios::ate);
			if (!ifs.is_open()) {
				throw std::runtime_error("Could not load the requested image file " + filename);
			}

			uint32_t inputDataSize = static_cast<uint32_t>(ifs.tellg());
			char* inputData = new char[inputDataSize];

			ifs.seekg(0, std::ios::beg);
			ifs.read(inputData, inputDataSize);
			
			bool successed = ktxTranscoder.init(inputData, inputDataSize);
			if (!successed) {
				throw std::runtime_error("Could not initialize ktx2 transcoder for image file " + filename);
			}

			// Select target format based on device features (use uncompressed if none supported)
			auto targetFormat = basist::transcoder_texture_format::cTFRGBA32;

			auto formatSupported = [](VkFormat format) {
				auto& device = core::Device::Instance();
				VkFormatProperties formatProperties;
				vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDeviceHandle(), format, &formatProperties);
				return ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) && (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT));
			};

			if (device.GetPhysicalDeviceFeatures().textureCompressionBC) {
				// BC7 is the preferred block compression if available
				if (formatSupported(VK_FORMAT_BC7_UNORM_BLOCK)) {
					targetFormat = basist::transcoder_texture_format::cTFBC7_RGBA;
					format = VK_FORMAT_BC7_UNORM_BLOCK;
				} else {
					if (formatSupported(VK_FORMAT_BC3_SRGB_BLOCK)) {
						targetFormat = basist::transcoder_texture_format::cTFBC3_RGBA;
						format = VK_FORMAT_BC3_SRGB_BLOCK;
					}
				}
			}
			// Adaptive scalable texture compression
			if (device.GetPhysicalDeviceFeatures().textureCompressionASTC_LDR) {
				if (formatSupported(VK_FORMAT_ASTC_4x4_SRGB_BLOCK))
				{
					targetFormat = basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
					format = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
				}
			}
			// Ericsson texture compression
			if (device.GetPhysicalDeviceFeatures().textureCompressionETC2) {
				if (formatSupported(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK))
				{
					targetFormat = basist::transcoder_texture_format::cTFETC2_RGBA;
					format = VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
				}
			}

			// @todo PowerVR texture compression support needs to be checked via an extension (VK_IMG_FORMAT_PVRTC_EXTENSION_NAME)

			const bool targetFormatIsUncompressed = basist::basis_transcoder_format_is_uncompressed(targetFormat);

			std::vector<basist::ktx2_image_level_info> levelInfos(ktxTranscoder.get_levels());
			this->mipLevels_ = ktxTranscoder.get_levels();

			// Query image level information that we need later on for several calculations
			// We only support 2D images (no cube maps or layered images)
			for (uint32_t i = 0; i < this->mipLevels_; i++) {
				ktxTranscoder.get_image_level_info(levelInfos[i], i, 0, 0);
			}

			this->width_ = levelInfos[0].m_orig_width;
			this->height_ = levelInfos[0].m_orig_height;

			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements memReqs{};

			// Create one staging buffer large enough to hold all uncompressed image levels
			const uint32_t bytesPerBlockOrPixel = basist::basis_get_bytes_per_block_or_pixel(targetFormat);
			uint32_t numBlocksOrPixels = 0;
			VkDeviceSize totalBufferSize = 0;
			for (uint32_t i = 0; i < this->mipLevels_; i++) {
				// Size calculations differ for compressed/uncompressed formats
				numBlocksOrPixels = targetFormatIsUncompressed ? levelInfos[i].m_orig_width * levelInfos[i].m_orig_height : levelInfos[i].m_total_blocks;
				totalBufferSize += numBlocksOrPixels * bytesPerBlockOrPixel;
			}

			unsigned char* buffer = new unsigned char[totalBufferSize];
			unsigned char* bufferPtr = &buffer[0];

			successed = ktxTranscoder.start_transcoding();
			if (!successed) {
				throw std::runtime_error("Could not start transcoding for image file " + filename);
			}

			// Transcode all mip levels into the staging buffer
			for (uint32_t i = 0; i < this->mipLevels_; i++) {
				// Size calculations differ for compressed/uncompressed formats
				numBlocksOrPixels = targetFormatIsUncompressed ? levelInfos[i].m_orig_width * levelInfos[i].m_orig_height : levelInfos[i].m_total_blocks;
				uint32_t outputSize = numBlocksOrPixels * bytesPerBlockOrPixel;
				if (!ktxTranscoder.transcode_image_level(i, 0, 0, bufferPtr, numBlocksOrPixels, targetFormat, 0)) {
					throw std::runtime_error("Could not transcode the requested image file " + filename);
				}
				bufferPtr += outputSize;
			}

			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = this->mipLevels_;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { this->width_, this->height_, 1 };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

			SUCCESS_OR_LOG(
                vkCreateImage(device.GetLogicalDeviceHandle(), &imageCreateInfo, nullptr, &this->image_) == VK_SUCCESS, 
                "Texture: Failed to create image."
            );

			vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->image_, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); 

			SUCCESS_OR_LOG(
                vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->deviceMemory_), 
                "Texture: Failed to allocate memory."
            );

			SUCCESS_OR_LOG(
                vkBindImageMemory(device.GetLogicalDeviceHandle(), this->image_, this->deviceMemory_, 0), 
                "Texture: Failed to bind image memory"
            );

			// 异步上传：转码数据已全部在 buffer（含所有 mip），ring 切片 -> image
			std::vector<VkBufferImageCopy> bufferCopyRegions;
			VkDeviceSize bufferOffset = 0;
			for (uint32_t i = 0; i < this->mipLevels_; i++) {
				// Size calculations differ for compressed/uncompressed formats
				numBlocksOrPixels = targetFormatIsUncompressed ? levelInfos[i].m_orig_width * levelInfos[i].m_orig_height : levelInfos[i].m_total_blocks;
				uint32_t outputSize = numBlocksOrPixels * bytesPerBlockOrPixel;

				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = i;
				bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = levelInfos[i].m_orig_width;
				bufferCopyRegion.imageExtent.height = levelInfos[i].m_orig_height;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = bufferOffset;
				bufferCopyRegions.push_back(bufferCopyRegion);

				bufferOffset += outputSize;
			}

			// 终态 TRANSFER_SRC_OPTIMAL（KTX2 自带全部 mip，后续一般无 blit；descriptor 按此布局采样）
			this->readyAt_ = core::StagingRingAllocator::Instance().SubmitImageCopy(
				buffer, totalBufferSize, this->image_, bufferCopyRegions, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
			).readyAt;
			this->imageLayout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

			delete[] buffer;
			delete[] inputData;
		} else {
			// Image is a basic glTF format like png or jpg and can be loaded directly via tinyglTF
			unsigned char* buffer = nullptr;
			VkDeviceSize bufferSize = 0;
			bool deleteBuffer = false;

			if (gltfimage.component == 3) {
				// Most devices don't support RGB only on Vulkan so convert if necessary
				bufferSize = gltfimage.width * gltfimage.height * 4;
				buffer = new unsigned char[bufferSize];
				unsigned char* rgba = buffer;
				unsigned char* rgb = &gltfimage.image[0];
				for (int32_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
					for (int32_t j = 0; j < 3; ++j) {
						rgba[j] = rgb[j];
					}
					rgba += 4;
					rgb += 3;
				}
				deleteBuffer = true;
			}
			else {
				buffer = &gltfimage.image[0];
				bufferSize = gltfimage.image.size();
			}

			// PNG supports up to 64 bits
			if (gltfimage.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
				format = VK_FORMAT_R16G16B16A16_UNORM;
			}

			this->width_ = gltfimage.width;
			this->height_ = gltfimage.height;
			this->mipLevels_ = static_cast<uint32_t>(floor(log2(std::max(this->width_, this->height_))) + 1.0);

			VkFormatProperties formatProperties;
			vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDeviceHandle(), format, &formatProperties);
			assert(formatProperties.optimalTilingFeatures& VK_FORMAT_FEATURE_BLIT_SRC_BIT);
			assert(formatProperties.optimalTilingFeatures& VK_FORMAT_FEATURE_BLIT_DST_BIT);

			VkMemoryAllocateInfo memAllocInfo{};
			memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			VkMemoryRequirements memReqs{};

			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = this->mipLevels_;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { this->width_, this->height_, 1 };
			imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

			SUCCESS_OR_LOG(
                vkCreateImage(device.GetLogicalDeviceHandle(), &imageCreateInfo, nullptr, &this->image_) == VK_SUCCESS, 
                "Texture: Failed to create image."
            );

			vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->image_, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			SUCCESS_OR_LOG(
                vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->deviceMemory_) == VK_SUCCESS, 
                "Texture: Failed to allocate memory."
            );

			SUCCESS_OR_LOG(
                vkBindImageMemory(device.GetLogicalDeviceHandle(), this->image_, this->deviceMemory_, 0) == VK_SUCCESS, 
                "Texture: Failed to bind image memory."
            );

			// 异步上传 mip0（终态 TRANSFER_SRC_OPTIMAL，衔接下方同步 blit 链；
			// 同队列隐式提交序保证 blit 在上传完成后才执行）
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = this->width_;
			bufferCopyRegion.imageExtent.height = this->height_;
			bufferCopyRegion.imageExtent.depth = 1;

			std::vector<VkBufferImageCopy> mip0Regions{ bufferCopyRegion };
			this->readyAt_ = core::StagingRingAllocator::Instance().SubmitImageCopy(
				buffer, bufferSize, this->image_, mip0Regions, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
			).readyAt;
			if (deleteBuffer) {
				delete[] buffer;
			}

			// Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
			VkCommandBuffer blitCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			for (uint32_t i = 1; i < this->mipLevels_; i++) {
				VkImageBlit imageBlit{};

				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcOffsets[1].x = int32_t(this->width_ >> (i - 1));
				imageBlit.srcOffsets[1].y = int32_t(this->height_ >> (i - 1));
				imageBlit.srcOffsets[1].z = 1;

				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstOffsets[1].x = int32_t(this->width_ >> i);
				imageBlit.dstOffsets[1].y = int32_t(this->height_ >> i);
				imageBlit.dstOffsets[1].z = 1;

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = 0;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.image = this->image_;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}

				vkCmdBlitImage(blitCmd, this->image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this->image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.image = this->image_;
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}
			}

			this->imageLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			{
				VkImageSubresourceRange finalRange = {};
				finalRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				finalRange.levelCount = this->mipLevels_;
				finalRange.layerCount = 1;

				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = this->image_;
				imageMemoryBarrier.subresourceRange = finalRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

            device.FlushCommandBuffer(blitCmd, true);
		}

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = textureSampler.magFilter;
		samplerInfo.minFilter = textureSampler.minFilter;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = textureSampler.addressModeU;
		samplerInfo.addressModeV = textureSampler.addressModeV;
		samplerInfo.addressModeW = textureSampler.addressModeW;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.maxAnisotropy = 1.0;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxLod = (float)this->mipLevels_;
		samplerInfo.maxAnisotropy = 8.0f;
		samplerInfo.anisotropyEnable = VK_TRUE;

		SUCCESS_OR_LOG(
            vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerInfo, nullptr, &this->sampler_) == VK_SUCCESS, 
            "Failed to create sampler."
        );

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = this->image_;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.levelCount = this->mipLevels_;

        SUCCESS_OR_LOG(
            vkCreateImageView(device.GetLogicalDeviceHandle(), &viewInfo, nullptr, &this->view_) == VK_SUCCESS,
            "Texture2D: Failed to creatre image view."
        );
        
		this->descriptor_.sampler = this->sampler_;
		this->descriptor_.imageView = this->view_;
		this->descriptor_.imageLayout = this->imageLayout_;
	}

    void Texture2D::LoadFromFile(
        std::string filename, 
        VkFormat format,
        VkImageUsageFlags imageUsageFlags,
        VkImageLayout imageLayout,
        VkSamplerAddressMode addressMode,
        bool enableAnisotropy)
    {
        gli::texture2d tex2D(gli::load(filename.c_str()));	
        assert(!tex2D.empty());

        auto& device = core::Device::Instance();

        this->width_ = static_cast<uint32_t>(tex2D[0].extent().x);
        this->height_ = static_cast<uint32_t>(tex2D[0].extent().y);
        this->mipLevels_ = static_cast<uint32_t>(tex2D.levels());

        // Get device properites for the requested texture format
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDeviceHandle(), format, &formatProperties);

        VkMemoryAllocateInfo memAllocInfo{};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        // Setup buffer copy regions for each mip level
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        uint32_t offset = 0;

        for (uint32_t i = 0; i < this->mipLevels_; i++)
        {
            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = i;
            bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(tex2D[i].extent().x);
            bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(tex2D[i].extent().y);
            bufferCopyRegion.imageExtent.depth = 1;
            bufferCopyRegion.bufferOffset = offset;

            bufferCopyRegions.push_back(bufferCopyRegion);

            offset += static_cast<uint32_t>(tex2D[i].size());
        }

        // Create optimal tiled target image
        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.mipLevels = this->mipLevels_;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = { this->width_, this->height_, 1 };
        imageCreateInfo.usage = imageUsageFlags;
        // Ensure that the TRANSFER_DST bit is set for staging
        if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        SUCCESS_OR_LOG(
            vkCreateImage(device.GetLogicalDeviceHandle(), &imageCreateInfo, nullptr, &this->image_) == VK_SUCCESS,
            "Texture2D: Failed to create image."
        );

        vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->image_, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;

        memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        SUCCESS_OR_LOG(
            vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->deviceMemory_) == VK_SUCCESS,
            "Texture2D: Failed to allocate memory."
        );

        SUCCESS_OR_LOG(
            vkBindImageMemory(device.GetLogicalDeviceHandle(), this->image_, this->deviceMemory_, 0) == VK_SUCCESS,
            "Texture2D: Failed to bind image memory."
        );

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = this->mipLevels_;
        subresourceRange.layerCount = 1;

        // 异步上传：ring 切片 -> image，终态按调用方请求的布局
        this->imageLayout_ = imageLayout;
        this->readyAt_ = core::StagingRingAllocator::Instance().SubmitImageCopy(
            tex2D.data(), tex2D.size(), this->image_, bufferCopyRegions, imageLayout
        ).readyAt;

        VkSamplerCreateInfo samplerCreateInfo{};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.addressModeU = addressMode;
        samplerCreateInfo.addressModeV = addressMode;
        samplerCreateInfo.addressModeW = addressMode;
        samplerCreateInfo.mipLodBias = 0.0f;
        samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerCreateInfo.minLod = 0.0f;
        samplerCreateInfo.maxLod = (float)this->mipLevels_;
        samplerCreateInfo.anisotropyEnable = enableAnisotropy && device.GetEnableFeatures().samplerAnisotropy;
        samplerCreateInfo.maxAnisotropy = samplerCreateInfo.anisotropyEnable ? device.GetDeviceProperties().limits.maxSamplerAnisotropy : 1.0f;
        samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        SUCCESS_OR_LOG(
            vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerCreateInfo, nullptr, &this->sampler_) == VK_SUCCESS,
            "Texture2D: Failed to create sampler."
        );

        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = format;
        viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        viewCreateInfo.subresourceRange.levelCount = this->mipLevels_;
        viewCreateInfo.image = this->image_;
        SUCCESS_OR_LOG(
            vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCreateInfo, nullptr, &this->view_) == VK_SUCCESS,
            "Texture2D: Failed to create image view."
        );

        UpdateDescriptor();
    }

    void Texture2D::LoadFromBuffer(
        void* buffer,
        VkDeviceSize bufferSize,
        VkFormat format,
        uint32_t width,
        uint32_t height,
        VkFilter filter,
        VkImageUsageFlags imageUsageFlags,
        VkImageLayout imageLayout)
    {
        assert(buffer);

        auto& device = core::Device::Instance();

        width = width;
        height = height;
        this->mipLevels_ = 1;

        VkMemoryAllocateInfo memAllocInfo{};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        VkBufferImageCopy bufferCopyRegion = {};
        bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bufferCopyRegion.imageSubresource.mipLevel = 0;
        bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
        bufferCopyRegion.imageSubresource.layerCount = 1;
        bufferCopyRegion.imageExtent.width = width;
        bufferCopyRegion.imageExtent.height = height;
        bufferCopyRegion.imageExtent.depth = 1;
        bufferCopyRegion.bufferOffset = 0;

        // Create optimal tiled target image
        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.mipLevels = this->mipLevels_;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = { width, height, 1 };
        imageCreateInfo.usage = imageUsageFlags;
        // Ensure that the TRANSFER_DST bit is set for staging
        if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        SUCCESS_OR_LOG(
            vkCreateImage(device.GetLogicalDeviceHandle(), &imageCreateInfo, nullptr, &this->image_) == VK_SUCCESS,
            "Texture2D: Failed to create image."
        );

        vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->image_, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;

        memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        SUCCESS_OR_LOG(
            vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->deviceMemory_) == VK_SUCCESS,
            "Texture2D: Failed to allocate memory."
        );

        SUCCESS_OR_LOG(
            vkBindImageMemory(device.GetLogicalDeviceHandle(), this->image_, this->deviceMemory_, 0) == VK_SUCCESS,
            "Texture2D: Failed to bind image memory."
        );

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = this->mipLevels_;
        subresourceRange.layerCount = 1;

        // 异步上传：ring 切片 -> image，终态按调用方请求的布局
        this->imageLayout_ = imageLayout;
        std::vector<VkBufferImageCopy> regions{ bufferCopyRegion };
        this->readyAt_ = core::StagingRingAllocator::Instance().SubmitImageCopy(
            buffer, bufferSize, this->image_, regions, imageLayout
        ).readyAt;

        // Create sampler
        VkSamplerCreateInfo samplerCreateInfo = {};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = filter;
        samplerCreateInfo.minFilter = filter;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCreateInfo.mipLodBias = 0.0f;
        samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerCreateInfo.minLod = 0.0f;
        samplerCreateInfo.maxLod = 0.0f;
        samplerCreateInfo.maxAnisotropy = 1.0f;

        SUCCESS_OR_LOG(
            vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerCreateInfo, nullptr, &this->sampler_) == VK_SUCCESS,
            "Texture2D: Failed to create sampler."
        );

        // Create image view
        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.pNext = NULL;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = format;
        viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.image = this->image_;

        SUCCESS_OR_LOG(
            vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCreateInfo, nullptr, &this->view_) == VK_SUCCESS,
            "Texture2D: Failed to create image view."
        );

        // Update descriptor image info member that can be used for setting up descriptor sets
        UpdateDescriptor();
    }

    void TextureCubeMap::LoadFromFile(
        std::string filename,
        VkFormat format,
        VkImageUsageFlags imageUsageFlags,
        VkImageLayout imageLayout)
    {
#if defined(__ANDROID__)
        // Textures are stored inside the apk on Android (compressed)
        // So they need to be loaded via the asset manager
        AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            LOGE("Could not load texture %s", filename.c_str());
            exit(-1);
        }
        size_t size = AAsset_getLength(asset);
        assert(size > 0);

        void *textureData = malloc(size);
        AAsset_read(asset, textureData, size);
        AAsset_close(asset);

        gli::texture_cube texCube(gli::load((const char*)textureData, size));

        free(textureData);
#else
        gli::texture_cube texCube(gli::load(filename));
#endif	
        assert(!texCube.empty());

        auto& device = core::Device::Instance();

        this->width_ = static_cast<uint32_t>(texCube.extent().x);
        this->height_ = static_cast<uint32_t>(texCube.extent().y);
        this->mipLevels_ = static_cast<uint32_t>(texCube.levels());

        VkMemoryAllocateInfo memAllocInfo{};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        // Setup buffer copy regions for each face including all of it's miplevels
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        size_t offset = 0;

        for (uint32_t face = 0; face < 6; face++) {
            for (uint32_t level = 0; level < this->mipLevels_; level++) {
                VkBufferImageCopy bufferCopyRegion = {};
                bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bufferCopyRegion.imageSubresource.mipLevel = level;
                bufferCopyRegion.imageSubresource.baseArrayLayer = face;
                bufferCopyRegion.imageSubresource.layerCount = 1;
                bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(texCube[face][level].extent().x);
                bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(texCube[face][level].extent().y);
                bufferCopyRegion.imageExtent.depth = 1;
                bufferCopyRegion.bufferOffset = offset;

                bufferCopyRegions.push_back(bufferCopyRegion);

                // Increase offset into staging buffer for next level / face
                offset += texCube[face][level].size();
            }
        }

        // Create optimal tiled target image
        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.mipLevels = this->mipLevels_;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = { this->width_, this->height_, 1 };
        imageCreateInfo.usage = imageUsageFlags;
        // Ensure that the TRANSFER_DST bit is set for staging
        if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        // Cube faces count as array layers in Vulkan
        imageCreateInfo.arrayLayers = 6;
        // This flag is required for cube map images
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;


        SUCCESS_OR_LOG(
            vkCreateImage(device.GetLogicalDeviceHandle(), &imageCreateInfo, nullptr, &this->image_) == VK_SUCCESS,
            "Texture2D: Failed to create image."
        );

        vkGetImageMemoryRequirements(device.GetLogicalDeviceHandle(), this->image_, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = device.GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        SUCCESS_OR_LOG(
            vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &this->deviceMemory_) == VK_SUCCESS,
            "Texture2D: Failed to allocate memory."
        );

        SUCCESS_OR_LOG(
            vkBindImageMemory(device.GetLogicalDeviceHandle(), this->image_, this->deviceMemory_, 0) == VK_SUCCESS,
            "Texture2D: Failed to bind image memory."
        );

        // Image barrier for optimal image (target)
        // Set initial layout for all array layers (faces) of the optimal (target) tiled texture
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = this->mipLevels_;
        subresourceRange.layerCount = 6;

        // 异步上传：ring 切片 -> cube image（6 face × mips），终态按调用方请求的布局
        this->imageLayout_ = imageLayout;
        this->readyAt_ = core::StagingRingAllocator::Instance().SubmitImageCopy(
            texCube.data(), texCube.size(), this->image_, bufferCopyRegions, imageLayout
        ).readyAt;

        // Create sampler
        VkSamplerCreateInfo samplerCreateInfo{};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
        samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
        samplerCreateInfo.mipLodBias = 0.0f;
        samplerCreateInfo.maxAnisotropy = device.GetEnableFeatures().samplerAnisotropy ? device.GetDeviceProperties().limits.maxSamplerAnisotropy : 1.0f;
        samplerCreateInfo.anisotropyEnable = device.GetEnableFeatures().samplerAnisotropy;
        samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerCreateInfo.minLod = 0.0f;
        samplerCreateInfo.maxLod = (float)this->mipLevels_;
        samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        SUCCESS_OR_LOG(
            vkCreateSampler(device.GetLogicalDeviceHandle(), &samplerCreateInfo, nullptr, &this->sampler_) == VK_SUCCESS,
            "Texture2D: Failed to create sampler."
        );

        // Create image view
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewCreateInfo.format = format;
        viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        viewCreateInfo.subresourceRange.layerCount = 6;
        viewCreateInfo.subresourceRange.levelCount = this->mipLevels_;
        viewCreateInfo.image = this->image_;
        SUCCESS_OR_LOG(
            vkCreateImageView(device.GetLogicalDeviceHandle(), &viewCreateInfo, nullptr, &this->view_) == VK_SUCCESS,
            "Texture2D: Failed to create image view."
        );

        // Update descriptor image info member that can be used for setting up descriptor sets
        UpdateDescriptor();
    }
}
