#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/core/buffer.h"
#include "engine/render/pass_resource.h"
#include "engine/render/render_scene.h"
#include "engine/resource/texture.h"

namespace engine::render
{
    struct FullScreenPassConfig;

    struct RenderPassInitInfo
    {
        VkPipelineCache*                            pipelineCache_{ nullptr };
        VkRenderPass*                               mainRenderPass_{ nullptr };
        // 共享 UBO buffers（Renderer 持有，Pass 填 descriptor 时引用）
        std::vector<core::Buffer>*                  matricesUBOBuffers_{ nullptr };
        std::vector<core::Buffer>*                  paramsUBOBuffers_{ nullptr };
    };

    class RenderPass
    {
        protected:
            RenderPassInitInfo                          initInfo_;
        public:
            virtual ~RenderPass() = default;
            void                                        Init(const RenderPassInitInfo& initInfo) { initInfo_ = initInfo; }
            virtual std::string_view                    GetName() const noexcept = 0;
            virtual std::span<const PassResourceUsage>  GetResourceUsages() const noexcept = 0;
            virtual void                                ExecutePreProcess(const RenderScene& renderScene) = 0;
            virtual void                                Execute(VkCommandBuffer currentCB, uint32_t frameIndex, const RenderScene& renderScene) = 0;
    };

    class SkyBoxRenderPass : public RenderPass
    {
        private:
            std::vector<VkDescriptorSet>                skyboxSets_;

            VkPipelineLayout                            pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline> pipelines_;

            void                                        SetUpDescriptorSetLayout(const RenderScene& renderScene);
            void                                        SetUpPipeline(const std::string vertexShader, const std::string fragmentShader);
            void                                        Cleanup();

        public:
            SkyBoxRenderPass();
            ~SkyBoxRenderPass() override;
            std::string_view                            GetName() const noexcept override;
            std::span<const PassResourceUsage>          GetResourceUsages() const noexcept override;
            void                                        ExecutePreProcess(const RenderScene& renderScene) override;
            void                                        Execute(VkCommandBuffer currentCB, uint32_t frameIndex, const RenderScene& renderScene) override;
    };

    class PBRRenderPass : public RenderPass
    {
        private:
            struct MeshPushConstantBlock 
            {
    		    int32_t meshIndex;
    		    int32_t materialIndex;
    	    };

            struct PreProcessTextureList
            {
                resource::Texture2D lut_;
                resource::Texture2D eu_;
                resource::Texture2D eavg_;
            };

            std::vector<VkDescriptorSet>                sceneSets_;

            VkPipelineLayout                            pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline> pipelines_;

            PreProcessTextureList                       textureList_;

            resource::Texture2D                         PreComputeTexture(const FullScreenPassConfig& config);

            void                                        SetUpDescriptorSetLayout(const RenderScene& renderScene);
            void                                        SetUpPipeline(const std::string vertexShader, const std::string fragmentShader);
            void                                        DrawQueue(const std::vector<RenderItem>& items, VkCommandBuffer cb, uint32_t frameIndex);
            VkPipeline                                  SelectPipeline(PipelineVariant variant);
            void                                        Cleanup();
        public:

            PBRRenderPass();
            ~PBRRenderPass() override;
            std::string_view                            GetName() const noexcept override;
            std::span<const PassResourceUsage>          GetResourceUsages() const noexcept override;
            void                                        ExecutePreProcess(const RenderScene& renderScene) override;
            void                                        Execute(VkCommandBuffer currentCB, uint32_t frameIndex, const RenderScene& renderScene) override;

    };
}



