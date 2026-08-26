#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace engine::render
{
    inline constexpr size_t kMaxPipelineCachePayloadSize = 64u * 1024u * 1024u;

    // 读取并校验应用文件头、CRC 和 Vulkan 设备兼容信息；无可用缓存时返回空数组。
    std::vector<uint8_t> LoadPipelineCacheFile(const VkPhysicalDeviceProperties& properties);

    // 将 Vulkan 原始 cache payload 写入临时文件，成功后原子替换正式文件。
    bool SavePipelineCacheFile(const std::vector<uint8_t>& payload);
}
