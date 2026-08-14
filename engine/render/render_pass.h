#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "engine/core/device.h"
#include "engine/core/swapchain.h"
#include "engine/core/buffer.h"
#include "engine/render/fullscreen_pass.h"
#include "engine/render/render_scene.h"
#include "engine/resource/texture.h"
#include "engine/resource/resource_manager.h"

namespace engine::render
{
    struct DescriptorSetCount
    {
        uint32_t uniformBufferCount = 0;
        uint32_t imageSamplerCount = 0;
        uint32_t storageBufferCount = 0;
        uint32_t maxSets = 0;

        DescriptorSetCount operator+(DescriptorSetCount other);
    };

    struct RenderPassInitInfo
    {
        public:
            bool                        multiSamplingEnabled_;
            engine::core::SwapChain*    swapChain_;
            const RenderScene*          renderScene_;
            VkPipelineCache*            pipelineCache_;
            VkRenderPass*               mainRenderPass_;
            VkDescriptorPool*           descriptorPool_;
            // 共享 UBO buffers（Renderer 持有，Pass 填 descriptor 时引用）
            std::vector<core::Buffer>*  matricesUBOBuffers_;
            std::vector<core::Buffer>*  paramsUBOBuffers_;
    };

    class RenderPass
    {
        protected:
            RenderPassInitInfo          initInfo_; 
            virtual void                PreProcess() = 0;
        public:
            RenderPass();
            virtual ~RenderPass();
            virtual void                UpdateUniformData(uint32_t frameIndex) = 0;
            virtual DescriptorSetCount  GetDescriptorSetCount() = 0;
            virtual void                Init(const RenderPassInitInfo& initInfo) = 0;
            virtual void                ExecutePreProcess() = 0;
            virtual void                Execute(VkCommandBuffer currentCB, uint32_t frameIndex) = 0;
            virtual void                Cleanup() = 0;
    };

    class SkyBoxRenderPass : public RenderPass
    {
        private:
            struct UBOMatricesUpload
            {
                glm::mat4 projection{ 1.0f };
                glm::mat4 model{ 1.0f };
            };

            VkDescriptorSetLayout                       skyboxLayout_{ VK_NULL_HANDLE };
            std::vector<VkDescriptorSet>                skyboxSets_;
            std::vector<core::Buffer>                   matricesUBOBuffer_;

            VkPipelineLayout                            pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline> pipelines_;

            void                                        PreProcess() override;

            void                                        SetUpDescriptorSetLayout();
            void                                        SetUpPipeline(const std::string vertexShader, const std::string fragmentShader);

        public:
            SkyBoxRenderPass();
            ~SkyBoxRenderPass();
            void                                        UpdateUniformData(uint32_t frameIndex) override;
            DescriptorSetCount                          GetDescriptorSetCount();
            void                                        Init(const RenderPassInitInfo& initInfo) override;
            void                                        ExecutePreProcess() override;
            void                                        Execute(VkCommandBuffer currentCB, uint32_t frameIndex) override;
            void                                        Cleanup() override;
    };

    class PBRRenderPass : public RenderPass
    {
        private:
            struct MeshPushConstantBlock 
            {
    		    int32_t meshIndex;
    		    int32_t materialIndex;
    	    };

            struct DescriptorSetLayouts
            {
                VkDescriptorSetLayout scene{ VK_NULL_HANDLE };
                VkDescriptorSetLayout material{ VK_NULL_HANDLE };
                VkDescriptorSetLayout materialBuffer{ VK_NULL_HANDLE };
                VkDescriptorSetLayout meshDataBuffer{ VK_NULL_HANDLE };
            }; 

            struct DescriptorSets
            {
                VkDescriptorSet scene = VK_NULL_HANDLE;
            };

            // PBR 预处理产出的离线贴图（BRDF LUT / 能量补偿 Eu / Eavg）
            struct PreProcessTextureList
            {
                resource::Texture2D lut_;
                resource::Texture2D eu_;
                resource::Texture2D eavg_;
            };

            std::vector<DescriptorSets>                 descriptorSets_;
            DescriptorSetLayouts                        descriptorSetLayouts_;

            VkPipelineLayout                            pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline> pipelines_;
            VkPipeline                                  boundPipeline_{ VK_NULL_HANDLE };

            PreProcessTextureList                       textureList_;

            resource::Texture2D                         PreComputeTexture(const FullScreenPassConfig& config);

            void                                        PreProcess() override;
            void                                        SetUpDescriptorSetLayout();
            void                                        SetUpPipeline(const std::string vertexShader, const std::string fragmentShader);
            void                                        DrawQueue(const std::vector<RenderItem>& items, VkCommandBuffer cb, uint32_t frameIndex);
            VkPipeline                                  SelectPipeline(PipelineVariant variant);
        public:        

            PBRRenderPass();
            ~PBRRenderPass();
            void                                        UpdateUniformData(uint32_t frameIndex) override;
            DescriptorSetCount                          GetDescriptorSetCount();
            void                                        Init(const RenderPassInitInfo& initInfo) override;
            void                                        ExecutePreProcess() override;
            void                                        Execute(VkCommandBuffer currentCB, uint32_t frameIndex) override;
            void                                        Cleanup() override;

    };
}



