#pragma once

#include "engine/core/device.h"
#include "engine/core/swapchain.h"
#include "engine/render/render_pass.h"
#include "engine/render/fullscreen_pass.h"
#include "engine/render/render_scene.h"
#include "engine/render/render_image.h"
#include "engine/render/render_resource_registry.h"


namespace engine::render
{
    class RenderContext;

    class RendererDescription
    {
        public:
            bool fullscreen_ = false;
            bool vsync_ = false;
            // 实际值在 Renderer::Init() 中从 DeviceSetting 同步（Device 是唯一权威源）
            bool multiSampling_ = false;

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
            // 对接 RenderContext：借用决策结果，不拥有 Pass / FrameGraph。
            const RenderContext*                        renderContext_ = nullptr;

            // 对接资源声明与 Pass：Registry 拥有内部 GPU 资源；外部 Swapchain / Model 资源不放入拥有型表。
            // 当前 Image 实例按 imageIndex_ 取，逐帧 Buffer 实例按 frameIndex_ 取，不可混用。
            RenderResourceRegistry                      renderResources_;

            // 对接 RenderScene：保存 Application 提交的逐帧 CPU 快照，用于上传和绘制。
            std::vector<RenderScene>                    renderScenes_;

            // 对接 Window / SwapChain：管理呈现目标和窗口重建请求。
            engine::core::SwapChain                     swapChain_;
            RendererDescription                         rendererDescription_;
            bool                                        paused_ = false;
            bool                                        resizePending_ = false;
            bool                                        forceResize_ = false;
            uint32_t                                    pendingWidth_ = 0;
            uint32_t                                    pendingHeight_ = 0;
            uint32_t                                    imageIndex_ = 0;

            // 对接 app::ui::UI：Renderer 拥有独立的 UI RenderPass / framebuffer；UI 只借用 RenderPass 创建 Pipeline，并借用当前 CommandBuffer 录制绘制。
            VkRenderPass                                uiRenderPass_ = VK_NULL_HANDLE;
            std::vector<VkFramebuffer>                  uiFramebuffers_;

            // Renderer 内部执行状态：命令录制、frame-in-flight 与 GPU 同步。
            VkCommandPool                               commandPool_ = VK_NULL_HANDLE;
            VkCommandBuffer                             currentCB_ = VK_NULL_HANDLE;
            uint32_t                                    frameCount_ = 0;
            uint32_t                                    frameIndex_ = 0;
            std::vector<FrameContext>                   frameContexts_;
            std::vector<VkSemaphore>                    renderFinishedSemaphores_;

            // 对接 UI 的 GPU 计时结果；query pool 属于 FrameContext。
            float                                       timestampPeriod_ = 0.0f;
            bool                                        timestampQuerySupported_ = false;
            GpuTimings                                  lastGpuTimings_;

            // Context / 资源：校验声明，创建和释放 Renderer 持有的 GPU 资源。
            bool                                        ValidatePassResourceDeclarations() const;
            void                                        CreateResources();
            // 只负责创建；调用方提供参数已确定的内部资源描述及合并后的 usage。
            void                                        CreateBuffer(const RenderResourceDescription& resource, VkBufferUsageFlags usage);
            void                                        CreateImage(const RenderResourceDescription& resource, VkImageUsageFlags usage);

            // UI 输出：复用 ToneMapping 写过的 Swapchain image，LOAD 原颜色，结束时转换到 PRESENT。
            void                                        PrepareUI();
            void                                        CreateUIFramebuffers();
            void                                        DestroyUIFramebuffers();

            // Window / SwapChain：内部初始化和重建。
            void                                        InitSwapChain(engine::platform::Window& window);
            bool                                        RecreateSwapChain(uint32_t width, uint32_t height);

            // 内部执行：命令池、帧上下文和同步对象。
            void                                        InitCommandPool();
            void                                        CreateSyncObjects();
            void                                        CreateFrameContexts();
            void                                        DestroyFrameContexts();

        public:

            // Application：管理 Renderer 生命周期。
            Renderer();
            Renderer(const RendererDescription& description);
            ~Renderer();
            void                                        Destroy();

            // RenderContext：接收场景渲染决策，创建资源并准备三个场景 Pass 与独立 UI 输出；Context 必须存活至 Renderer 销毁。
            void                                        PrepareFrame(const RenderContext& renderContext);

            // RenderScene：接收当前帧 CPU 数据并上传共享 UBO。
            void                                        SetRenderScene(const RenderScene& renderScene);
            void                                        UploadFrameUniformData();

            // Window / SwapChain：窗口接入和呈现资源重建。
            void                                        Init(engine::platform::Window& window);
            void                                        RequestResize(uint32_t width, uint32_t height, bool force = false);
            void                                        RecreateSyncObjects();

            // RenderPass / UI：共享 Pipeline Cache，不转移所有权。
            VkPipelineCache                             GetPipelineCache();

            // UI：Application 在 Render() 之后、EndFrame() 之前成对调用 Begin / End，并在两者之间调用 UI::Draw()。
            VkRenderPass                                GetUIRenderPass() const;
            void                                        BeginUIRenderPass();
            void                                        EndUIRenderPass();

            // frameIndex 用于逐帧数据，不是 Swapchain imageIndex。
            bool                                        BeginFrame(uint32_t windowWidth, uint32_t windowHeight);
            void                                        Render();
            void                                        EndFrame();
            uint32_t                                    GetFrameIndex() const;
            VkCommandBuffer                             GetCurrentCommandBuffer();

            // 可观测性：返回已经完成的上一帧 GPU 计时，供 UI 展示。
            GpuTimings                                  GetGpuTimings();
    };


}
