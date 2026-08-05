#pragma once
#include "engine/core/device.h"
#include "engine/resource/texture.h"

namespace engine::render
{
    struct  FullScreenPassConfig
    {
        VkFormat                outputFormat;
        uint32_t                width = 512;
        uint32_t                height = 512;

        std::string             vertShader;
        std::string             fragShader;

        resource::Texture2D*    inputTexture = nullptr;
    };


    class FullScreenPass 
    {
        public:
            resource::Texture2D Execute(VkPipelineCache cache, const FullScreenPassConfig& config);
    };

}






