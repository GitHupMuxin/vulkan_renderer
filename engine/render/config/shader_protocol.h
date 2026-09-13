#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "engine/core/config/schema.h"

namespace engine::render::shader_protocol
{
    inline constexpr uint32_t kCameraSet = schema::kSceneSetIndex;
    inline constexpr uint32_t kCameraBinding = schema::kMainCameraBinding;

    // set=0, binding=0 的 Camera 公共协议；字段顺序必须与 camera.glsl 一致。
    struct CameraUniformData
    {
        glm::mat4 projection{1.0f};
        glm::mat4 model{1.0f};
        glm::mat4 view{1.0f};
        glm::vec3 camPos{0.0f};
    };

    static_assert(offsetof(CameraUniformData, projection) == 0);
    static_assert(offsetof(CameraUniformData, model) == 64);
    static_assert(offsetof(CameraUniformData, view) == 128);
    static_assert(offsetof(CameraUniformData, camPos) == 192);

    inline constexpr uint32_t kSceneParamSet = schema::kSceneSetIndex;
    inline constexpr uint32_t kSceneParamBinding = schema::kSceneParamBinding;

    // 字段布局必须与 scene_params.glsl 一致；用户参数来自 RenderScene，贴图层数来自 Registry 中的环境资源。
    struct SceneParamUniformData
    {
        glm::vec4 lightDir{};
        float exposure{};
        float gamma{};
        float prefilteredCubeMipLevels{};
        float scaleIBLAmbient{};
        float debugViewInputs{};
        float debugViewEquation{};
        float debugBSDFType{};
    };

    static_assert(offsetof(SceneParamUniformData, lightDir) == 0);
    static_assert(offsetof(SceneParamUniformData, exposure) == 16);
    static_assert(offsetof(SceneParamUniformData, gamma) == 20);
    static_assert(offsetof(SceneParamUniformData, prefilteredCubeMipLevels) == 24);
    static_assert(offsetof(SceneParamUniformData, scaleIBLAmbient) == 28);
    static_assert(offsetof(SceneParamUniformData, debugViewInputs) == 32);
    static_assert(offsetof(SceneParamUniformData, debugViewEquation) == 36);
    static_assert(offsetof(SceneParamUniformData, debugBSDFType) == 40);

}
