#pragma once
#include "engine/resource/texture.h"

namespace engine::resource
{


    class EnvironmentCubeMap
    {
        private:
            uint32_t        prefilteredCubeMipLevels_;
        public:
            TextureCubeMap  environmentCube_;
            TextureCubeMap  irradianceCube_;
            TextureCubeMap  prefilteredCube_;           
            std::string     name_;
            void            Init(std::string name = "");
            void            Destroy();
            void            LoadFromFile(std::string fileName, VkFormat format, VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT, VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            void            GenerateCubemaps(); 
            uint32_t        GetPrefilteredCubeMipLevels();
    };


}



