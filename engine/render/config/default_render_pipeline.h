#pragma once

#include <span>

#include "engine/core/config/schema.h"
#include "engine/render/render_pipeline_description.h"

namespace engine::render
{
    namespace
    {
        DescriptorBindingDescription MakeDescriptorBinding(RenderResourceReference resource,
            const VkDescriptorSetLayoutBinding& binding)
        {
            return {resource, binding.binding, binding.descriptorType, binding.stageFlags, binding.descriptorCount};
        }

        // 所有布局字段取自 Schema，配置只关联资源与 Schema 条目。
        DescriptorSetDescription MakeSharedDescriptorSet(uint32_t setIndex, RenderResourceId resource,
            std::span<const VkDescriptorSetLayoutBinding> schemaBindings)
        {
            DescriptorSetDescription set{};
            set.set_ = setIndex;
            set.bindings_.reserve(schemaBindings.size());
            for (const auto& binding : schemaBindings)
            {
                set.bindings_.push_back(MakeDescriptorBinding({resource}, binding));
            }
            return set;
        }

        GraphicsPipelineDescription MakeSkyboxPipeline()
        {
            GraphicsPipelineDescription pipeline{};
            pipeline.key_ = "skybox";
            pipeline.vertexShader_ = "shaders/skybox.vert.spv";
            pipeline.fragmentShader_ = "shaders/skybox.frag.spv";
            pipeline.vertexLayout_ = VertexLayout::Skybox;
            pipeline.cullMode_ = VK_CULL_MODE_NONE;
            return pipeline;
        }

        GraphicsPipelineDescription MakePbrPipeline(std::string_view key, VkCullModeFlags cullMode, bool alphaBlend)
        {
            GraphicsPipelineDescription pipeline{};
            pipeline.key_ = key;
            pipeline.vertexShader_ = "shaders/pbr.vert.spv";
            pipeline.fragmentShader_ = "shaders/material_pbr.frag.spv";
            pipeline.vertexLayout_ = VertexLayout::PbrModel;
            pipeline.cullMode_ = cullMode;
            pipeline.depthTestEnable_ = true;
            pipeline.blendMode_ = alphaBlend ? BlendMode::Alpha : BlendMode::Opaque;
            return pipeline;
        }

        GraphicsPipelineDescription MakeUnlitPipeline()
        {
            auto pipeline = MakePbrPipeline("unlit", VK_CULL_MODE_BACK_BIT, false);
            pipeline.fragmentShader_ = "shaders/material_unlit.frag.spv";
            return pipeline;
        }

        GraphicsPipelineDescription MakeToneMappingPipeline()
        {
            GraphicsPipelineDescription pipeline{};
            pipeline.key_ = "tone_mapping";
            pipeline.vertexShader_ = "shaders/tone_mapping.vert.spv";
            pipeline.fragmentShader_ = "shaders/tone_mapping.frag.spv";
            pipeline.cullMode_ = VK_CULL_MODE_NONE;
            return pipeline;
        }

        RenderPassDescription MakeSkyboxPass()
        {
            RenderPassDescription pass{};
            pass.name_ = "SkyBoxRenderPass";
            pass.inputs_ = {
                {{RenderResourceId::MainCamera}, {ResourceUsage::UniformBuffer}},
                {{RenderResourceId::Environment, EnvironmentTexture::Prefiltered}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}
            };
            pass.outputs_ = {{
                {RenderResourceId::SceneColorHdr},
                {ResourceUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                PassColorAttachment{
                    .loadOp_ = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp_ = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .clearValue_ = {{0.0f, 0.0f, 0.0f, 1.0f}}
                }
            }};
            pass.descriptorSets_ = {{
                schema::kSceneSetIndex,
                {
                    MakeDescriptorBinding({RenderResourceId::MainCamera}, schema::kSkyboxSet[0]),
                    MakeDescriptorBinding({RenderResourceId::Environment, EnvironmentTexture::Prefiltered}, schema::kSkyboxSet[1])
                }
            }};
            pass.pipelines_ = {MakeSkyboxPipeline()};
            pass.draw_ = {PassDrawType::SkyboxGeometry};
            return pass;
        }

        RenderPassDescription MakePbrPass()
        {
            RenderPassDescription pass{};
            pass.name_ = "PBRRenderPass";
            pass.inputs_ = {
                {{RenderResourceId::MainCamera}, {ResourceUsage::UniformBuffer}},
                {{RenderResourceId::SceneParam}, {ResourceUsage::UniformBuffer}},
                {{RenderResourceId::Environment, EnvironmentTexture::Irradiance}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
                {{RenderResourceId::Environment, EnvironmentTexture::Prefiltered}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
                {{RenderResourceId::BrdfLut}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
                {{RenderResourceId::EuLut}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
                {{RenderResourceId::EavgLut}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
                {{RenderResourceId::MaterialTextures}, {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
                {{RenderResourceId::MaterialBuffer}, {ResourceUsage::StorageBuffer}},
                {{RenderResourceId::MeshDataBuffer}, {ResourceUsage::StorageBuffer}}
            };
            pass.outputs_ = {
                {
                    {RenderResourceId::SceneColorHdr},
                    {ResourceUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                    PassColorAttachment{
                        .loadOp_ = VK_ATTACHMENT_LOAD_OP_LOAD,
                        .storeOp_ = VK_ATTACHMENT_STORE_OP_STORE,
                        .initialLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .finalLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                    }
                },
                {
                    {RenderResourceId::MainDepth},
                    {ResourceUsage::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                    PassDepthAttachment{
                        .loadOp_ = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp_ = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        .clearValue_ = {1.0f, 0}
                    }
                }
            };
            // Set 协议：0 由 Pass 逐帧持有，1 属于 Material，2/3 属于 Model；Skybox/Fullscreen 只用 set 0。
            pass.descriptorSets_ = {
                {schema::kSceneSetIndex, {
                    MakeDescriptorBinding({RenderResourceId::MainCamera}, schema::kSceneSet[0]),
                    MakeDescriptorBinding({RenderResourceId::SceneParam}, schema::kSceneSet[1]),
                    MakeDescriptorBinding({RenderResourceId::Environment, EnvironmentTexture::Irradiance}, schema::kSceneSet[2]),
                    MakeDescriptorBinding({RenderResourceId::Environment, EnvironmentTexture::Prefiltered}, schema::kSceneSet[3]),
                    MakeDescriptorBinding({RenderResourceId::BrdfLut}, schema::kSceneSet[4]),
                    MakeDescriptorBinding({RenderResourceId::EuLut}, schema::kSceneSet[5]),
                    MakeDescriptorBinding({RenderResourceId::EavgLut}, schema::kSceneSet[6])
                }},
                MakeSharedDescriptorSet(1, RenderResourceId::MaterialTextures, schema::kMaterialSet),
                MakeSharedDescriptorSet(2, RenderResourceId::MeshDataBuffer, schema::kMeshDataSSBO),
                MakeSharedDescriptorSet(3, RenderResourceId::MaterialBuffer, schema::kMaterialSSBO)
            };
            pass.pushConstants_ = {{
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int32_t) * 2
            }};
            pass.pipelines_ = {
                MakePbrPipeline("pbr", VK_CULL_MODE_BACK_BIT, false),
                MakePbrPipeline("pbr_double_sided", VK_CULL_MODE_NONE, false),
                MakePbrPipeline("pbr_alpha_blending", VK_CULL_MODE_NONE, true),
                MakeUnlitPipeline()
            };
            pass.draw_ = {PassDrawType::SceneGeometry};
            return pass;
        }

        RenderPassDescription MakeToneMappingPass()
        {
            RenderPassDescription pass{};
            pass.name_ = "ToneMappingRenderPass";
            pass.inputs_ = {{
                {RenderResourceId::SceneColorHdr},
                {ResourceUsage::SampledImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
            }};
            pass.outputs_ = {{
                {RenderResourceId::BackBuffer},
                {ResourceUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                PassColorAttachment{
                    .loadOp_ = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp_ = VK_ATTACHMENT_STORE_OP_STORE,
                    .initialLayout_ = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .clearValue_ = {{0.0f, 0.0f, 0.0f, 1.0f}}
                }
            }};
            pass.descriptorSets_ = {{
                schema::kSceneSetIndex,
                {MakeDescriptorBinding({RenderResourceId::SceneColorHdr}, schema::kToneMappingSet[0])}
            }};
            pass.pushConstants_ = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)}};
            pass.pipelines_ = {MakeToneMappingPipeline()};
            pass.draw_ = {PassDrawType::FullscreenTriangle};
            return pass;
        }
    }

    inline const RenderPipelineDescription defaultPipeline_ = {
        .passes_ = {
            MakeSkyboxPass(),
            MakePbrPass(),
            MakeToneMappingPass()
        },
        .dependencies_ = {
            {"SkyBoxRenderPass", "PBRRenderPass", {RenderResourceId::SceneColorHdr}},
            {"PBRRenderPass", "ToneMappingRenderPass", {RenderResourceId::SceneColorHdr}}
        },
        .outputPasses_ = {"ToneMappingRenderPass"}
    };
}
