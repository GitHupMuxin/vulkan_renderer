#pragma once
#include "engine/render/render_scene.h"
#include "engine/scene/scene.h"

namespace engine::scene
{

    class SceneExtractor
    {
        private:
            static void ExtracCamera(const Scene& scene, render::CameraRenderData& cameraData);
            static void ExtracEnvironment(const Scene& scene, render::EnvironmentRenderData& EnvironmentData);
            static void ExtracLights(const Scene& scene, render::LightRenderData& renderScene);
            static void ExtracSettings(const Scene& scene, render::RenderSettings& settings);
            static void ExtracModel(const Scene& scene, render::RenderScene& renderScene);
            static void FlattenNodes(resource::Model* model, resource::ModelHandle handle, const std::vector<resource::Node*>& nodes, const glm::mat4& parentWorld, render::RenderScene& renderScene);
        public:
            static render::RenderScene ExtractScene(const Scene& scene);
    };

}




