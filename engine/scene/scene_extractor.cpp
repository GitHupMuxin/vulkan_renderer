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
            // 提取只读取 CPU 数据；上传回执由绘制端 GetModel 检查。
            resource::Model* model = rm.PeekModel(h);
            if (model == nullptr) continue;

            // 显式注册：descriptor 分配、就绪轮询等"资源存在性"遍历用
            renderScene.modelHandles.push_back(h);

            const glm::mat4& world = scene.sceneObjects_[i].transform;

            FlattenNodes(*model, h, world, renderScene);
        }
    }


    void SceneExtractor::FlattenNodes(const resource::Model& model, resource::ModelHandle handle, const glm::mat4& objectWorld, render::RenderScene& renderScene)
    {
        // Model 已计算完整层级变换，遍历扁平节点时不能再次累乘父节点。
        for (const resource::ModelNode& node : model.GetNodes())
        {
            if (node.meshIndex == resource::InvalidIndex)
            {
                continue;
            }
            const resource::MeshResource* mesh = model.GetMesh(node.meshIndex);
            if (mesh == nullptr)
            {
                // 不完整资源不能生成指向无效几何的 Item。
                continue;
            }

            for (const resource::PrimitiveData& primitive : mesh->GetPrimitives())
            {
                if (primitive.indexCount == 0)
                {
                    // 空 Primitive 没有绘制工作，也不需要绑定材质。
                    continue;
                }
                const resource::MaterialData* material = model.GetMaterial(primitive.materialIndex);
                if (material == nullptr)
                {
                    // 没有材质时不能访问材质 SSBO；导入器应先补齐默认材质。
                    LOG_WARN("SceneExtractor: primitive has no renderable material, skipped.");
                    continue;
                }

                render::RenderItem item;
                item.modelHandle = handle;
                item.meshIndex = node.meshIndex;
                item.instanceSlot = node.instanceSlot;
                item.materialIndex = primitive.materialIndex;
                item.firstIndex = primitive.firstIndex;
                item.indexCount = primitive.indexCount;
                item.hasIndices = true;
                item.worldTransform = objectWorld * node.worldTransform;
                item.materialDescriptorSet = model.GetMaterialTextureSet(primitive.materialIndex);

                switch (material->alphaMode)
                {
                    case resource::MaterialData::AlphaMode::Opaque:
                        item.renderQueue = render::RenderQueue::Opaque;
                        break;
                    case resource::MaterialData::AlphaMode::Mask:
                        item.renderQueue = render::RenderQueue::Masked;
                        break;
                    case resource::MaterialData::AlphaMode::Blend:
                        item.renderQueue = render::RenderQueue::Transparent;
                        break;
                }
                item.pipeline = item.renderQueue == render::RenderQueue::Transparent
                    ? render::PipelineVariant::AlphaBlending
                    : (material->doubleSided ? render::PipelineVariant::DoubleSided : render::PipelineVariant::Pbr);

                switch (item.renderQueue)
                {
                    case render::RenderQueue::Opaque:      renderScene.opaqueItems.push_back(item); break;
                    case render::RenderQueue::Masked:      renderScene.maskedItems.push_back(item); break;
                    case render::RenderQueue::Transparent: renderScene.transparentItems.push_back(item); break;
                }
            }
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



