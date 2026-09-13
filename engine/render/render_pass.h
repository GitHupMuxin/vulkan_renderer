#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/core/buffer.h"
#include "engine/render/config/pass_resource.h"
#include "engine/render/render_image.h"
#include "engine/render/render_pass_description.h"
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
    class RenderPass
    {
        protected:
            RenderPassDescription                           description_;
            VkRenderPass                                    renderPass_{ VK_NULL_HANDLE };
            std::vector<VkFramebuffer>                      framebuffers_;
            std::vector<VkClearValue>                       clearValues_;
            const RenderResourceRegistry*                   renderResources_{ nullptr };

            // 当前协议：Pass 持有 set 0 的逐帧实例，下标是 frameIndex。
            std::vector<VkDescriptorSet>                    descriptorSets_;
            // 下标就是 set 编号；布局由 DescriptorLayoutRegistry 持有。
            std::vector<VkDescriptorSetLayout>              descriptorSetLayouts_;
            VkPipelineLayout                                pipelineLayout_{ VK_NULL_HANDLE };
            std::unordered_map<std::string, VkPipeline>     pipelines_;

            void                                            PrepareDescriptorResources(RenderResourceRegistry& resources);
            void                                            SetUpDescriptorSetLayouts();
            void                                            AllocateDescriptorSets();
            void                                            SetUpPipelineLayout();
            void                                            SetUpPipelines();
            void                                            DestroyPipelines();
            void                                            CreateRenderTarget(const CompiledPass& compiledPass, const RenderResourceRegistry& resources, core::SwapChain& swapChain);
            void                                            CreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain);

            void                                            DrawSceneGeometry(VkCommandBuffer cb, uint32_t frameIndex, const RenderScene& renderScene);
            void                                            DrawSkyboxGeometry(VkCommandBuffer cb, uint32_t frameIndex);
            void                                            DrawFullscreenTriangle(VkCommandBuffer cb, uint32_t frameIndex, const RenderScene& renderScene);
            void                                            DrawQueue(std::span<const RenderItem> items, VkCommandBuffer cb, uint32_t frameIndex);
            // 第一条描述为默认管线；Unlit 使用 "unlit"，其他变体按现有混合/剔除配置匹配。
            VkPipeline                                      SelectScenePipeline(PipelineVariant variant) const;
        public:
            explicit RenderPass(const RenderPassDescription& description);
            virtual ~RenderPass();
            RenderPass(const RenderPass&) = delete;
            RenderPass& operator=(const RenderPass&) = delete;
            void                                            Prepare(const CompiledPass& compiledPass, RenderResourceRegistry& resources, core::SwapChain& swapChain);
            // 当前帧 fence 已完成，Registry 已选定本次图像后，填写该帧的 set 0。
            void                                            UpdateDescriptorSets(uint32_t frameIndex);
            virtual void                                    RecreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain);
            void                                            DestroyFramebuffers();
            void                                            DestroyRenderTarget();
            VkRenderPass                                    GetHandle() const noexcept;
            VkFramebuffer                                   GetFramebuffer(uint32_t imageIndex) const;
            std::span<const VkClearValue>                   GetClearValues() const noexcept;
            std::string_view                                GetName() const noexcept;
            std::span<const PassInputResource>              GetInputResources() const noexcept;
            std::span<const PassOutputResource>             GetOutputResources() const noexcept;
            // Input/Output 按 ID + 成员匹配；仅传 ID 时查询 Source。
            PassResourceUsage                               GetResourceUsage(RenderResourceReference reference) const noexcept;
            PassResourceUsage                               GetResourceUsage(RenderResourceId resourceId) const noexcept;
            // Renderer 已开始 RenderPass 并设置 viewport/scissor；这里只录制配置指定的绘制命令。
            void                                            Execute(VkCommandBuffer currentCB, uint32_t frameIndex, const RenderScene& renderScene);
    };
}
