#include <vulkan/vulkan.h>
#include <vector>
#include "engine/core/device.h"
#include "engine/core/swapchain.h"
#include "engine/core/buffer.h"
#include "engine/render/fullscreen_pass.h"
#include "engine/resource/texture.h"
#include "engine/resource/resource_manager.h"
#include "engine/scene/scene.h"

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
            scene::Scene*               scene_;
            VkPipelineCache*            pipelineCache_;
            VkRenderPass*               mainRenderPass_;
            VkDescriptorPool*           descriptorPool_;
    };

    class RenderPass
    {
        private:

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

    struct PreProcessTextureList
    {
        public:
            resource::Texture2D LUT_;
            resource::Texture2D Eu_;
            resource::Texture2D Eavg_; 
    };

    class SkyBoxRenderPass : public RenderPass
    {
        private:
            struct UBOMatrices
            {
                glm::mat4 projection{ 1.0f };
		        glm::mat4 model{ 1.0f };
    		    glm::mat4 view{ 1.0f };
		        glm::vec3 camPos{ 0.0f };
            };

            struct DescriptorSetLayouts
            {
                VkDescriptorSetLayout skybox{ VK_NULL_HANDLE };
            }; 

            struct DescriptorSets
            {
                VkDescriptorSet skybox = VK_NULL_HANDLE;
            };

            DescriptorSetLayouts                        descriptorSetLayouts_;
            std::vector<DescriptorSets>                 descriptorSets_;

            VkPipelineLayout                            pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline> pipelines_;

            void                                        PreProcess() override;

            void                                        SetUpDescriptorSetLayout();
            void                                        SetUpPipeline(const std::string vertexShader, const std::string fragmentShader);

        public:
            UBOMatrices                                 matrices_;
            std::vector<core::Buffer>                   matricesUBOBuffer_;
            
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
	        void                                        RenderNode(resource::Model* model, resource::Node *node, VkCommandBuffer currentCB, uint32_t cbIndex, resource::Material::AlphaMode alphaMode);
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



