#include <assert.h>
#include <iostream>
#include <fstream>
#include "engine/core/loader.h"

namespace engine::core
{
    VkPipelineShaderStageCreateInfo Loader::LoadShader(VkDevice device, std::string fileName, VkShaderStageFlagBits stage)
    {
        VkPipelineShaderStageCreateInfo shaderStage{};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = stage;
        shaderStage.pName = "main";
        std::ifstream is(fileName, std::ios::binary | std::ios::in | std::ios::ate);

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
            std::cerr << "Error: Could not open shader file \"" << fileName << "\"" << std::endl;
            shaderStage.module = VK_NULL_HANDLE;
        }

        assert(shaderStage.module != VK_NULL_HANDLE);
        return shaderStage;
    }


}




