#pragma once

#include <unordered_map>
#include "engine/core/device.h"
#include "engine/core/swapchain.h"
#include "engine/render/render_pass.h"
#include "engine/render/fullscreen_pass.h"
#include "engine/render/render_scene.h"
#include "engine/render/framebuffer_attachment.h"
#include "engine/render/frame_graph.h"

namespace engine::render
{
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

            FrameGraph                                  frameGraph_;

            VkPipelineCache                             pipelineCache_ = VK_NULL_HANDLE;
            uint32_t                                    frameCount_ = 0;

            std::vector<FrameContext>                   frameContexts_;
            std::vector<VkSemaphore>                    renderFinishedSemaphores_;

            RendererDescription                         rendererDescription_;
            RenderPassInitInfo                          renderPassInitInfo_;
            
            VkRenderPass                                mainRenderPass_ = VK_NULL_HANDLE;
            std::vector<VkFramebuffer>                  frameBuffers_;
            std::vector<MainRenderPassAttachmentList>   mainAttachmentLists_;

            uint32_t                                    frameIndex_ = 0;
            uint32_t                                    imageIndex_ = 0;

            VkCommandBuffer                             currentCB_ = VK_NULL_HANDLE;

            bool                                        paused_ = false;
            bool                                        resizePending_ = false;
            bool                                        forceResize_ = false;
            uint32_t                                    pendingWidth_ = 0;
            uint32_t                                    pendingHeight_ = 0;

            float                                       timestampPeriod_ = 0.0f;
            bool                                        timestampQuerySupported_ = false;
            GpuTimings                                  lastGpuTimings_;            

            // RenderScene 双缓冲（CPU 侧，per frame-in-flight）
            std::vector<RenderScene>                    renderScenes_;
            // 共享 UBO buffers（GPU 侧，归 Renderer；Pass 填 descriptor 用）
            std::vector<core::Buffer>                   matricesUBOBuffers_;
            std::vector<core::Buffer>                   paramsUBOBuffers_;

            void                                        InitSwapChain(engine::platform::Window& window);
            void                                        InitCommandPool();
            void                                        CreatePipelineCache();
            void                                        SavePipelineCache();
            void                                        CreateSyncObjects();
            void                                        CreateFrameContexts();

            // void                                        CreateDescriptorPool();
            void                                        CreateMainAttachments(MainRenderPassAttachmentList& attachmentList);
            bool                                        RecreateSwapChain(uint32_t width, uint32_t height);

            void                                        DestroyFrameContexts();

            void                                        CreateUniformBuffers();
            void                                        DestroyUniformBuffers();
            bool                                        ValidatePassResourceDeclarations() const;

        public:
            Renderer();
            Renderer(const RendererDescription& description);
            ~Renderer();

            void                                        Destroy();

            void                                        PrepareFrame();
            void                                        Init(engine::platform::Window& window);            
            FrameGraphNodeId                            AddRenderPass(std::unique_ptr<RenderPass> renderPass);
            void                                        AddRenderPassDependency(FrameGraphNodeId sourceNodeId, FrameGraphNodeId destinationNodeId, RenderResourceId resourceId, ResourceHazard hazard);
            bool                                        RebuildFrameGraph();

            // 每帧由组合层（app）传入：把提取好的 RenderScene 拷入当前 frame slot
            void                                        SetRenderScene(const RenderScene& renderScene);

            // 供组合层查询当前 frame-in-flight 号（动画更新 mesh data buffer 用）
            uint32_t                                    GetFrameIndex() const;

            void                                        CreateMainRenderPass();
            void                                        CreatMainFrameBuffer();
            void                                        DestroyMainFrameBuffer();
            void                                        RecreateSyncObjects();

            bool                                        BeginFrame(uint32_t windowWidth, uint32_t windowHeight);
            void                                        Render();
            void                                        EndFrame();

            void                                        UploadFrameUniformData();

            VkRenderPass                                GetRenderPass();
            VkPipelineCache                             GetPipelineCache();
            VkCommandBuffer                             GetCurrentCommandBuffer();
            GpuTimings                                  GetGpuTimings();

            void                                        RequestResize(uint32_t width, uint32_t height, bool force = false);
    };


}


