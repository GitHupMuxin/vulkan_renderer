#pragma once
#include <vulkan/vulkan.h>
#include <string>


namespace engine::core
{
    class Loader
    {
        public:
            static VkPipelineShaderStageCreateInfo LoadShader(VkDevice device, std::string fileName, VkShaderStageFlagBits stage);

    };



}

















