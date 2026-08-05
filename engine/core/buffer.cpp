#include <assert.h>
#include "engine/utils/log.h"
#include "engine/core/buffer.h"

namespace engine::core
{
    std::string dataPath = VK_EXAMPLE_DATA_DIR;
    /*
        Vulkan buffer object
    */
    
    Buffer::~Buffer()
    {
        this->Destroy();
    }

    Buffer::Buffer(Buffer&& other) noexcept
        : device(other.device)
        , buffer(other.buffer)
        , memory(other.memory)
        , descriptor(other.descriptor)
        , count(other.count)
        , actualBufferSize(other.actualBufferSize)
        , mapped(other.mapped)
    {
        other.device = VK_NULL_HANDLE;
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.mapped = nullptr;
        other.descriptor = {};
    }

    Buffer& Buffer::operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            this->Destroy();
            this->device = other.device;
            this->buffer = other.buffer;
            this->memory = other.memory;
            this->descriptor = other.descriptor;
            this->count = other.count;
            this->actualBufferSize = other.actualBufferSize;
            this->mapped = other.mapped;
            other.device = VK_NULL_HANDLE;
            other.buffer = VK_NULL_HANDLE;
            other.memory = VK_NULL_HANDLE;
            other.mapped = nullptr;
            other.descriptor = {};
        }
        return *this;
    }

    void Buffer::Create(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool map) 
    {
        auto& device = core::Device::Instance();

        this->device = device.GetLogicalDeviceHandle();
        device.CreateBuffer(usageFlags, memoryPropertyFlags, size, &buffer, &memory, nullptr, &actualBufferSize);
        descriptor = { buffer, 0, size };
        if (map) 
        {
            SUCCESS_OR_LOG(
                vkMapMemory(device.GetLogicalDeviceHandle(), memory, 0, actualBufferSize, 0, &mapped) == VK_SUCCESS,
                "Buffer: Failed to map memory.");
        }
    }
    void Buffer::Destroy() 
    {
        if (mapped) 
        {
            Unmap();
        }
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }
    void Buffer::Map() 
    {
        SUCCESS_OR_LOG(
            vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS,
            "Buffer: Failed to map memory.");
    }
    void Buffer::Unmap() 
    {
        if (mapped) {
            vkUnmapMemory(device, memory);
            mapped = nullptr;
        }
    }
    void Buffer::Flush(VkDeviceSize size) 
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory;
        mappedRange.size = size;
        SUCCESS_OR_LOG(
            vkFlushMappedMemoryRanges(device, 1, &mappedRange) == VK_SUCCESS,
            "Buffer: Failed to Flush mapped memory ranges.");
    }

    VkPipelineShaderStageCreateInfo LoadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage)
    {
        VkPipelineShaderStageCreateInfo shaderStage{};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = stage;
        shaderStage.pName = "main";
    #if defined(VK_USE_PLATFORM_ANDROID_KHR)
        std::string assetpath = "shaders/" + filename;
        AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, assetpath.c_str(), AASSET_MODE_STREAMING);
        assert(asset);
        size_t size = AAsset_getLength(asset);
        assert(size > 0);
        char *shaderCode = new char[size];
        AAsset_read(asset, shaderCode, size);
        AAsset_close(asset);
        VkShaderModule shaderModule;
        VkShaderModuleCreateInfo moduleCreateInfo;
        moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleCreateInfo.pNext = NULL;
        moduleCreateInfo.codeSize = size;
        moduleCreateInfo.pCode = (uint32_t*)shaderCode;
        moduleCreateInfo.flags = 0;
        VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStage.module));
        delete[] shaderCode;
    #else
        std::ifstream is(dataPath + "shaders/" + filename, std::ios::binary | std::ios::in | std::ios::ate);

        if (is.is_open()) {
            size_t size = is.tellg();
            is.seekg(0, std::ios::beg);
            char* shaderCode = new char[size];
            is.read(shaderCode, size);
            is.close();
            assert(size > 0);
            VkShaderModuleCreateInfo moduleCreateInfo{};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = size;
            moduleCreateInfo.pCode = (uint32_t*)shaderCode;
            vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderStage.module);
            delete[] shaderCode;
        }
        else {
            std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
            shaderStage.module = VK_NULL_HANDLE;
        }

    #endif
        assert(shaderStage.module != VK_NULL_HANDLE);
        return shaderStage;
    }

    void ReadDirectory(const std::string& directory, const std::string &extension, std::map<std::string, std::string> &filelist, bool recursive)
    {
    #if defined(VK_USE_PLATFORM_ANDROID_KHR)
        AAssetDir* assetDir = AAssetManager_openDir(androidApp->activity->assetManager, directory.c_str());
        AAssetDir_rewind(assetDir);
        const char* assetName;
        while ((assetName = AAssetDir_getNextFileName(assetDir)) != 0) {
            std::string filename(assetName);
            if (filename.find(extension) == std::string::npos) {
                continue;
            }
            filename.erase(filename.find_last_of("."), std::string::npos);
            filelist[filename] = directory + "/" + assetName;
        }
        AAssetDir_close(assetDir);
    #elif defined(VK_USE_PLATFORM_WIN32_KHR)
        std::string searchpattern(directory + "/*" + extension);
        WIN32_FIND_DATA data;
        HANDLE hFind;
        if ((hFind = FindFirstFile(searchpattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
            do {
                std::string filename(data.cFileName);
                filename.erase(filename.find_last_of("."), std::string::npos);
                filelist[filename] = directory + "/" + data.cFileName;
            } while (FindNextFile(hFind, &data) != 0);
            FindClose(hFind);
        }
        if (recursive) {
            std::string dirpattern = directory + "/*";
            if ((hFind = FindFirstFile(dirpattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
                do {
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        char subdir[MAX_PATH];
                        strcpy(subdir, directory.c_str());
                        strcat(subdir, "/");
                        strcat(subdir, data.cFileName);
                        if ((strcmp(data.cFileName, ".") != 0) && (strcmp(data.cFileName, "..") != 0)) {
                            ReadDirectory(subdir, extension, filelist, recursive);
                        }
                    }
                } while (FindNextFile(hFind, &data) != 0);
                FindClose(hFind);
            }
        }
    #elif defined(__linux__)
        struct dirent *entry;
        DIR *dir = opendir(directory.c_str());
        if (dir == NULL) {
            return;
        }
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                std::string filename(entry->d_name);
                if (filename.find(extension) != std::string::npos) {
                    filename.erase(filename.find_last_of("."), std::string::npos);
                    filelist[filename] = directory + "/" + entry->d_name;
                }
            }
            if (recursive && (entry->d_type == DT_DIR)) {
                std::string subdir = directory + "/" + entry->d_name;
                if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
                    readDirectory(subdir, extension, filelist, recursive);
                }
            }
        }
        closedir(dir);
    #endif
    }
}