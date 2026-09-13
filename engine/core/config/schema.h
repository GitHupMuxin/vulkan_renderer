#pragma once
#include <array>
#include <vulkan/vulkan.h>

namespace engine::schema
{
    inline constexpr uint32_t kSceneSetIndex = 0;
    inline constexpr uint32_t kMainCameraBinding = 0;
    inline constexpr uint32_t kSceneParamBinding = 1;

    // set=0: 相机矩阵 + 参数 UBO + 5 张环境贴图（pbr.vert / material_pbr.frag）
    inline constexpr std::array<VkDescriptorSetLayoutBinding, 7> kSceneSet = {{
        { kMainCameraBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { kSceneParamBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }}; 

    // set=0: skybox 专用（skybox.vert / skybox.frag）。注意与 kSceneSet 是不同 schema
    inline constexpr std::array<VkDescriptorSetLayoutBinding, 2> kSkyboxSet = {{
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};

    // set=0: HDR 场景颜色（tone_mapping.frag）
    inline constexpr std::array<VkDescriptorSetLayoutBinding, 1> kToneMappingSet = {{
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};

    // set=1: 材质纹理（material_pbr.frag）
    inline constexpr std::array<VkDescriptorSetLayoutBinding, 5> kMaterialSet = {{
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};

    // set=2: MeshData SSBO（pbr.vert）
    inline constexpr std::array<VkDescriptorSetLayoutBinding, 1> kMeshDataSSBO = {{
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
    }};

    // set=3: Material SSBO（material_pbr.frag）
    inline constexpr std::array<VkDescriptorSetLayoutBinding, 1> kMaterialSSBO = {{
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }};

}








