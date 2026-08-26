#include "engine/render/pipeline_cache_file.h"
#include "engine/utils/log.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace engine::render
{
    namespace
    {
        constexpr uint32_t kPipelineCacheFileMagic = 0x31435056; // "VPC1"（little-endian）
        constexpr uint32_t kPipelineCacheFileVersion = 1;
        constexpr uint32_t kPipelineCacheFileHeaderSize = 5u * sizeof(uint32_t);

        struct PipelineCacheFileHeader
        {
            uint32_t magic_ = kPipelineCacheFileMagic;
            uint32_t version_ = kPipelineCacheFileVersion;
            uint32_t headerSize_ = kPipelineCacheFileHeaderSize;
            uint32_t payloadSize_ = 0;
            uint32_t payloadCrc32_ = 0;
        };

        static_assert(sizeof(PipelineCacheFileHeader) == kPipelineCacheFileHeaderSize);

        const std::filesystem::path& GetPipelineCachePath()
        {
            static const std::filesystem::path path =
                std::filesystem::path(VK_PIPELINE_CACHE_DIR) / "vulkan_pbr.pipeline_cache";
            return path;
        }

        uint32_t ComputeCrc32(const uint8_t* data, size_t size)
        {
            uint32_t crc = 0xFFFFFFFFu;
            for (size_t i = 0; i < size; ++i)
            {
                crc ^= data[i];
                for (uint32_t bit = 0; bit < 8; ++bit)
                {
                    const uint32_t mask = 0u - (crc & 1u);
                    crc = (crc >> 1u) ^ (0xEDB88320u & mask);
                }
            }
            return ~crc;
        }

        bool IsCompatiblePipelineCache(
            const std::vector<uint8_t>& payload,
            const VkPhysicalDeviceProperties& properties,
            std::string& reason)
        {
            if (payload.size() < sizeof(VkPipelineCacheHeaderVersionOne))
            {
                reason = "payload is smaller than VkPipelineCacheHeaderVersionOne";
                return false;
            }

            VkPipelineCacheHeaderVersionOne header{};
            std::memcpy(&header, payload.data(), sizeof(header));

            if (header.headerSize != sizeof(VkPipelineCacheHeaderVersionOne) || header.headerSize > payload.size())
            {
                reason = "invalid Vulkan cache header size";
                return false;
            }
            if (header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
            {
                reason = "unsupported Vulkan cache header version";
                return false;
            }
            if (header.vendorID != properties.vendorID || header.deviceID != properties.deviceID)
            {
                reason = "cache belongs to a different physical device";
                return false;
            }
            if (std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
            {
                reason = "pipeline cache UUID does not match the current driver";
                return false;
            }

            return true;
        }

        bool ReplacePipelineCacheFile(
            const std::filesystem::path& temporaryPath,
            const std::filesystem::path& targetPath,
            std::string& reason)
        {
#if defined(_WIN32)
            if (MoveFileExW(
                temporaryPath.c_str(),
                targetPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            ) != FALSE)
            {
                return true;
            }

            reason = "Win32 error " + std::to_string(GetLastError());
            return false;
#else
            std::error_code error;
            std::filesystem::rename(temporaryPath, targetPath, error);
            if (!error)
            {
                return true;
            }

            reason = error.message();
            return false;
#endif
        }
    }

    std::vector<uint8_t> LoadPipelineCacheFile(const VkPhysicalDeviceProperties& properties)
    {
        const auto& path = GetPipelineCachePath();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LOG_INFO("Pipeline cache: no readable cache at " << path.string() << ", creating empty cache");
            return {};
        }

        const std::streamoff fileSize = file.tellg();
        const size_t maximumFileSize = sizeof(PipelineCacheFileHeader) + kMaxPipelineCachePayloadSize;
        if (fileSize < static_cast<std::streamoff>(sizeof(PipelineCacheFileHeader)) ||
            fileSize > static_cast<std::streamoff>(maximumFileSize))
        {
            LOG_WARN("Pipeline cache: ignored file with invalid size " << fileSize);
            return {};
        }

        std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
        file.seekg(0, std::ios::beg);
        file.read(
            reinterpret_cast<char*>(fileData.data()),
            static_cast<std::streamsize>(fileData.size())
        );
        if (!file)
        {
            LOG_WARN("Pipeline cache: failed to read complete cache file");
            return {};
        }

        PipelineCacheFileHeader fileHeader{};
        std::memcpy(&fileHeader, fileData.data(), sizeof(fileHeader));
        if (fileHeader.magic_ != kPipelineCacheFileMagic ||
            fileHeader.version_ != kPipelineCacheFileVersion ||
            fileHeader.headerSize_ != sizeof(PipelineCacheFileHeader))
        {
            LOG_WARN("Pipeline cache: ignored file with unknown magic, version, or header size");
            return {};
        }

        const size_t expectedFileSize = sizeof(PipelineCacheFileHeader) + fileHeader.payloadSize_;
        if (fileHeader.payloadSize_ == 0 ||
            fileHeader.payloadSize_ > kMaxPipelineCachePayloadSize ||
            expectedFileSize != fileData.size())
        {
            LOG_WARN("Pipeline cache: ignored truncated or malformed cache file");
            return {};
        }

        std::vector<uint8_t> payload(fileHeader.payloadSize_);
        std::memcpy(
            payload.data(),
            fileData.data() + sizeof(PipelineCacheFileHeader),
            payload.size()
        );
        if (ComputeCrc32(payload.data(), payload.size()) != fileHeader.payloadCrc32_)
        {
            LOG_WARN("Pipeline cache: ignored cache file with checksum mismatch");
            return {};
        }

        std::string reason;
        if (!IsCompatiblePipelineCache(payload, properties, reason))
        {
            LOG_WARN("Pipeline cache: ignored incompatible cache: " << reason);
            return {};
        }

        LOG_INFO("Pipeline cache: loaded " << payload.size() << " compatible bytes from " << path.string());
        return payload;
    }

    bool SavePipelineCacheFile(const std::vector<uint8_t>& payload)
    {
        if (payload.empty() ||
            payload.size() > kMaxPipelineCachePayloadSize ||
            payload.size() > std::numeric_limits<uint32_t>::max())
        {
            LOG_WARN("Pipeline cache: refusing to save invalid payload size " << payload.size());
            return false;
        }

        const auto& path = GetPipelineCachePath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            LOG_WARN("Pipeline cache: failed to create cache directory: " << error.message());
            return false;
        }

        PipelineCacheFileHeader header{};
        header.payloadSize_ = static_cast<uint32_t>(payload.size());
        header.payloadCrc32_ = ComputeCrc32(payload.data(), payload.size());

        auto temporaryPath = path;
        temporaryPath += ".tmp";
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            LOG_WARN("Pipeline cache: failed to open temporary cache file for writing");
            return false;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(
            reinterpret_cast<const char*>(payload.data()),
            static_cast<std::streamsize>(payload.size())
        );
        file.flush();
        if (!file)
        {
            LOG_WARN("Pipeline cache: failed while writing temporary cache file");
            file.close();
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        file.close();
        if (!file)
        {
            LOG_WARN("Pipeline cache: failed while closing temporary cache file");
            std::filesystem::remove(temporaryPath, error);
            return false;
        }

        std::string replaceFailureReason;
        if (!ReplacePipelineCacheFile(temporaryPath, path, replaceFailureReason))
        {
            LOG_WARN("Pipeline cache: failed to replace cache file: " << replaceFailureReason);
            std::filesystem::remove(temporaryPath, error);
            return false;
        }

        LOG_INFO("Pipeline cache: saved " << payload.size() << " bytes to " << path.string());
        return true;
    }
}
