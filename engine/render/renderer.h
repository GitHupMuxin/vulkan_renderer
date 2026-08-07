#pragma once

#include <unordered_map>
#include "engine/core/device.h"
#include "engine/scene/scene.h"
#include "engine/render/render_pass.h"
#include "engine/render/fullscreen_pass.h"

namespace engine::render
{
    struct RendererControl
    {
        bool*       animate;

        float*      animationTimer;

        float*      frameTimer;
                    
        int32_t*    animationIndex;
    };

    class RendererDescription
    {
        public:
            bool fullscreen_ = false;
            bool vsync_ = false;
            // 默认 true，但实际值在 Renderer::Init() 中从 DeviceSetting 同步（Device 是唯一权威源）
            bool multiSampling_ = true;

            RendererDescription() = default;
            RendererDescription(const RendererDescription& description) : fullscreen_(description.fullscreen_), vsync_(description.vsync_), multiSampling_(description.multiSampling_) {}
    };

    class Attachment
    {
        public:
            VkImage         image_ = VK_NULL_HANDLE;
            VkImageView     imageView_ = VK_NULL_HANDLE;
            VkDeviceMemory  memory_ = VK_NULL_HANDLE;

            Attachment() = default;
            ~Attachment();
            Attachment(const Attachment&) = delete;
            Attachment& operator=(const Attachment&) = delete;
            Attachment(Attachment&& other) noexcept;
            Attachment& operator=(Attachment&& other) noexcept;

            void Destroy();
    };
    
    class MainRenderPassAttachmentList
    {
        public:
            Attachment colorAttachment_;
            Attachment depthAttachment_;
            Attachment multisampleColorAttachment_;
            Attachment multisampleDepthAttachment_;
    };

    struct FrameContext
    {
        VkCommandBuffer     commandBuffer_ = VK_NULL_HANDLE;
        VkFence             inFlightFence_ = VK_NULL_HANDLE;
        VkSemaphore         imageAvailableSemaphore_ = VK_NULL_HANDLE;
        VkQueryPool         queryPool_ = VK_NULL_HANDLE;
        bool                hasSubmittedFrame_ = false;
    };

    struct GpuTimings
    {
        float frameTotalMs = 0.0f;
        float skyboxMs = 0.0f;
        float pbrMs = 0.0f;
        bool valid = false;
    };

    class Renderer
    {
        private:
            engine::core::SwapChain                     swapChain_;
            VkCommandPool                               commandPool_ = VK_NULL_HANDLE;

            std::vector<std::unique_ptr<RenderPass>>    renderPasses_;

            VkPipelineCache                             pipelineCache_ = VK_NULL_HANDLE;
            uint32_t                                    frameCount_ = 0;

            std::vector<FrameContext>                   frameContexts_;
            std::vector<VkSemaphore>                    renderFinishedSemaphores_;

            RendererDescription                         rendererDescription_;
            RenderPassInitInfo                          renderPassInitInfo_;

            VkDescriptorPool                            descriptorPool_ = VK_NULL_HANDLE;
            
            VkRenderPass                                mainRenderPass_ = VK_NULL_HANDLE;
            std::vector<VkFramebuffer>                  frameBuffers_;
            std::vector<MainRenderPassAttachmentList>   mainAttachmentLists_;

            scene::Scene*                               scene_ = nullptr;
            uint32_t                                    frameIndex_ = 0;
            uint32_t                                    imageIndex_ = 0;

            VkCommandBuffer                             currentCB_ = VK_NULL_HANDLE;

    	    VkPipeline                                  boundPipeline_{ VK_NULL_HANDLE };

            bool                                        paused_ = false;
            bool                                        resizePending_ = false;
            bool                                        forceResize_ = false;
            uint32_t                                    pendingWidth_ = 0;
            uint32_t                                    pendingHeight_ = 0;

            float                                       timestampPeriod_ = 0.0f;
            bool                                        timestampQuerySupported_ = false;
            GpuTimings                                  lastGpuTimings_;            

            void                                        InitSwapChain(engine::platform::Window& window);
            void                                        InitCommandPool();
            void                                        CreatePipelineCache();
            void                                        CreateSyncObjects();
            void                                        CreateFrameContexts();

            void                                        CreateDescriptorPool();
            void                                        CreateMainAttachments(MainRenderPassAttachmentList& attachmentList);
            bool                                        RecreateSwapChain(uint32_t width, uint32_t height);

            void                                        DestroyFrameContexts();

        public:
            RendererControl controller;

            Renderer();
            Renderer(const RendererDescription& description);
            ~Renderer();

            void                                        Destroy();

            void                                        PrepareFrame();
            void                                        Init(engine::platform::Window& window);            
            void                                        AddRenderPass(std::unique_ptr<RenderPass> renderPass); 
            void                                        BindingScene(scene::Scene* scene);

            void                                        CreateMainRenderPass();
            void                                        CreatMainFrameBuffer();
            void                                        DestroyMainFrameBuffer();
            void                                        RecreateSyncObjects();

            bool                                        BeginFrame(uint32_t windowWidth, uint32_t windowHeight);
            void                                        Render();
            void                                        EndFrame();

            void                                        UpdateParams();
            void                                        UpdateUniformData();

            VkRenderPass                                GetRenderPass();
            VkPipelineCache                             GetPipelineCache();
            VkCommandBuffer                             GetCurrentCommandBuffer();
            GpuTimings                                  GetGpuTimings();

            void                                        RequestResize(uint32_t width, uint32_t height, bool force = false);
            void                                        RecordCommandBuffer();
    };


}


