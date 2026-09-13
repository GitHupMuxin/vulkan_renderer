#include "engine/scene/scene_extractor.h"


namespace engine::scene
{
    void SceneExtractor::ExtracCamera(const Scene& scene, render::CameraRenderData& cameraData)
    {
        const auto* camera = scene.GetCamera();
        if (camera == nullptr) return;

        cameraData.view = camera->matrices_.view_;
        cameraData.projection = camera->matrices_.perspective_;
        glm::mat4 cv = glm::inverse(camera->matrices_.view_);
        cameraData.position = glm::vec3(cv[3]);
    }

    void SceneExtractor::ExtracEnvironment(const Scene& scene, render::EnvironmentRenderData& environmentData)
    {
        environmentData.exposure = scene.params_.exposure;
        environmentData.gamma = scene.params_.gamma;
        environmentData.scaleIBLAmbient = scene.params_.scaleIBLAmbient;
    }

    void SceneExtractor::ExtracSettings(const Scene& scene, render::RenderSettings& settings)
    {
        settings.debugViewInputs = scene.params_.debugViewInputs;
        settings.debugViewEquation = scene.params_.debugViewEquation;
        settings.debugBsdfType = scene.params_.debugBsdfType;
    }

    void SceneExtractor::ExtracLights(const Scene& scene, render::LightRenderData& lightData)
    {
        lightData.type = render::LightType::Directional;
        lightData.color = scene.lightSource_.color;
        lightData.direction = glm::vec3(scene.params_.lightDir);
    }

    void SceneExtractor::ExtracModel(const Scene& scene, render::RenderScene& renderScene)
    {
        auto& rm = resource::ResourceManager::Instance();

        for (size_t i = 0; i < scene.GetModelCount(); ++i)
        {
            resource::ModelHandle h = scene.sceneObjects_[i].modelHandle;
            // PeekModel：资源存在即可参与提取（上传在途也算），只保证 descriptor/几何可用；
            resource::Model* model = rm.PeekModel(h);
            if (model == nullptr) continue;

            // 显式注册：descriptor 分配、就绪轮询等"资源存在性"遍历用
            renderScene.modelHandles.push_back(h);

            const glm::mat4& world = scene.sceneObjects_[i].transform;

            FlattenNodes(model, h, model->GetNodes(), world, renderScene);
        }
    }


    void SceneExtractor::FlattenNodes(resource::Model* model, resource::ModelHandle handle, const std::vector<resource::Node*>& nodes, const glm::mat4& parentWorld, render::RenderScene& renderScene)
    {
        for (auto* node : nodes)
        {
        // 节点自身变换叠加（glTF 局部 → 世界）
            glm::mat4 nodeWorld = parentWorld * node->GetMatrix();

            if (node->mesh)
            {
                for (const auto* primitive : node->mesh->primitives)
                {
                    render::RenderItem item;
                    item.modelHandle = handle;
                    item.meshIndex = node->mesh->index;
                    item.materialIndex = primitive->material.index;
                    item.firstIndex = primitive->firstIndex;
                    item.indexCount = primitive->indexCount;
                    item.vertexCount = primitive->vertexCount;
                    item.hasIndices = primitive->hasIndices;
                    item.worldTransform = nodeWorld;
                    item.materialDescriptorSet = primitive->material.descriptorSet;

                    // 分类 → 队列 + 管线变体
                    switch (primitive->material.alphaMode)
                    {
                        case resource::Material::ALPHAMODE_OPAQUE:
                            item.renderQueue = render::RenderQueue::Opaque;
                            break;
                        case resource::Material::ALPHAMODE_MASK:
                            item.renderQueue = render::RenderQueue::Masked;
                            break;
                        case resource::Material::ALPHAMODE_BLEND:
                            item.renderQueue = render::RenderQueue::Transparent;
                            break;
                    }
                    // 当前只有一条透明管线，BLEND 优先使用它。
                    if (item.renderQueue == render::RenderQueue::Transparent)
                    {
                        item.pipeline = render::PipelineVariant::AlphaBlending;
                    }
                    else
                    {
                        item.pipeline = primitive->material.unlit
                            ? render::PipelineVariant::Unlit
                            : (primitive->material.doubleSided
                                ? render::PipelineVariant::DoubleSided
                                : render::PipelineVariant::Pbr);
                    }

                    // 分装到对应队列
                    switch (item.renderQueue)
                    {
                        case render::RenderQueue::Opaque:      renderScene.opaqueItems.push_back(item); break;
                        case render::RenderQueue::Masked:      renderScene.maskedItems.push_back(item); break;
                        case render::RenderQueue::Transparent: renderScene.transparentItems.push_back(item); break;
                    }
                }
            }
            // 递归子节点
            FlattenNodes(model, handle, node->children, nodeWorld, renderScene);
        }
    }

    render::RenderScene SceneExtractor::ExtractScene(const Scene& scene)
    {
        render::RenderScene renderScene;

        ExtracCamera(scene, renderScene.camera);

        ExtracLights(scene, renderScene.light);
        
        ExtracEnvironment(scene, renderScene.environment);

        ExtracSettings(scene, renderScene.settings);
        
        ExtracModel(scene, renderScene);

        // 场景主物体的世界变换 → shader UBO model 矩阵
        if (!scene.sceneObjects_.empty())
        {
            renderScene.modelMatrix = scene.sceneObjects_[0].transform;
        }

        return renderScene;
    }

}



