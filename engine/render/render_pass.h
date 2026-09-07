#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/core/buffer.h"
#include "engine/render/pass_resource.h"
#include "engine/render/render_image.h"
#include "engine/render/render_resource_registry.h"
#include "engine/render/render_scene.h"
#include "engine/resource/texture.h"

namespace engine::core
{
    class SwapChain;
}

namespace engine::render
{
    struct CompiledPass;
    struct FullScreenPassConfig;
    class RenderPass
    {
        protected:
            VkRenderPass                                    renderPass_{ VK_NULL_HANDLE };
            std::vector<VkFramebuffer>                      framebuffers_;
            std::vector<VkClearValue>                       clearValues_;
            const RenderResourceRegistry*                   renderResources_{ nullptr };

            void                                            CreateRenderTarget(const CompiledPass& compiledPass, const RenderResourceRegistry& resources, core::SwapChain& swapChain);
            void                                            CreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain);
        public:
            virtual ~RenderPass();
            void                                            Prepare(const CompiledPass& compiledPass, const RenderResourceRegistry& resources, core::SwapChain& swapChain, const RenderScene& renderScene);
            virtual void                                    RecreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain);
            void                                            DestroyFramebuffers();
            void                                            DestroyRenderTarget();
            VkRenderPass                                    GetHandle() const noexcept;
            VkFramebuffer                                   GetFramebuffer(uint32_t imageIndex) const;
            std::span<const VkClearValue>                   GetClearValues() const noexcept;
            virtual std::string_view                        GetName() const noexcept = 0;
            virtual std::span<const PassInputResource>      GetInputResources() const noexcept = 0;
            virtual std::span<const PassOutputResource>     GetOutputResources() const noexcept = 0;
            virtual PassResourceUsage                       GetResourceUsage(RenderResourceId resourceId) const noexcept = 0;
            virtual void                                    ExecutePreProcess(const RenderScene& renderScene) = 0;
            virtual void                                    Execute(VkCommandBuffer currentCB, uint32_t frameIndex, uint32_t imageIndex, const RenderScene& renderScene) = 0;
    };

    class SkyBoxRenderPass : public RenderPass
    {
        private:
            std::vector<VkDescriptorSet>                    skyboxSets_;

            VkPipelineLayout                                pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline>     pipelines_;

            void                                            SetUpDescriptorSetLayout(const RenderScene& renderScene);
            void                                            SetUpPipeline(const std::string vertexShader, const std::string fragmentShader);
            void                                            Cleanup();

        public:
            SkyBoxRenderPass();
            ~SkyBoxRenderPass() override;
            std::string_view                                GetName() const noexcept override;
            std::span<const PassInputResource>              GetInputResources() const noexcept override;
            std::span<const PassOutputResource>             GetOutputResources() const noexcept override;
            PassResourceUsage                               GetResourceUsage(RenderResourceId resourceId) const noexcept override;
            void                                            ExecutePreProcess(const RenderScene& renderScene) override;
            void                                            Execute(VkCommandBuffer currentCB, uint32_t frameIndex, uint32_t imageIndex, const RenderScene& renderScene) override;
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
            std::span<const PassInputResource>          GetInputResources() const noexcept override;
            std::span<const PassOutputResource>         GetOutputResources() const noexcept override;
            PassResourceUsage                           GetResourceUsage(RenderResourceId resourceId) const noexcept override;
            void                                        ExecutePreProcess(const RenderScene& renderScene) override;
            void                                        Execute(VkCommandBuffer currentCB, uint32_t frameIndex, uint32_t imageIndex, const RenderScene& renderScene) override;

    };

    class ToneMappingRenderPass : public RenderPass
    {
        private:
            struct ToneMappingPushConstant
            {
                float exposure_;
            };

            VkSampler                                   sampler_{ VK_NULL_HANDLE };
            std::vector<VkDescriptorSet>                descriptorSets_;
            VkPipelineLayout                            pipelineLayout_{ VK_NULL_HANDLE };
            VkPipeline                                  pipeline_{ VK_NULL_HANDLE };

            void                                        CreateSampler();
            void                                        SetUpDescriptorSets();
            void                                        SetUpPipeline(const std::string& vertexShader, const std::string& fragmentShader);
            void                                        Cleanup();

        public:
            ToneMappingRenderPass();
            ~ToneMappingRenderPass() override;
            ToneMappingRenderPass(const ToneMappingRenderPass&) = delete;
            ToneMappingRenderPass& operator=(const ToneMappingRenderPass&) = delete;
            std::string_view                            GetName() const noexcept override;
            std::span<const PassInputResource>          GetInputResources() const noexcept override;
            std::span<const PassOutputResource>         GetOutputResources() const noexcept override;
            PassResourceUsage                           GetResourceUsage(RenderResourceId resourceId) const noexcept override;
            void                                        RecreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain) override;
            void                                        ExecutePreProcess(const RenderScene& renderScene) override;
            void                                        Execute(VkCommandBuffer currentCB, uint32_t frameIndex, uint32_t imageIndex, const RenderScene& renderScene) override;
    };
}
