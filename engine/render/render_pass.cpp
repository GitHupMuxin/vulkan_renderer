#include "engine/render/render_pass.h"

#include <algorithm>
#include <vector>

#include "engine/core/device.h"
#include "engine/core/swapchain.h"
#include "engine/render/render_context.h"
#include "engine/utils/log.h"

namespace engine::render
{
    namespace
    {
        VkFormat ResolveAttachmentFormat(const PassOutputResource& output, core::SwapChain& swapChain)
        {
            const auto* resource = FindRenderResourceDescription(output.resource_);
            const auto& image = std::get<RenderImageDescription>(resource->description_);
            if (image.format_ != VK_FORMAT_UNDEFINED)
            {
                return image.format_;
            }

            return std::holds_alternative<PassColorAttachment>(output.attachment_)
                ? swapChain.GetColorFormat()
                : swapChain.GetDepthFormat();
        }

        VkAttachmentDescription BuildAttachmentDescription(const PassOutputResource& output, core::SwapChain& swapChain)
        {
            const auto* resource = FindRenderResourceDescription(output.resource_);
            const auto& image = std::get<RenderImageDescription>(resource->description_);

            VkAttachmentDescription attachment{};
            attachment.format = ResolveAttachmentFormat(output, swapChain);
            attachment.samples = image.samples_;

            if (const auto* color = std::get_if<PassColorAttachment>(&output.attachment_))
            {
                attachment.loadOp = color->loadOp_;
                attachment.storeOp = color->storeOp_;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = color->initialLayout_;
                attachment.finalLayout = color->finalLayout_;
            }
            else
            {
                const auto& depth = std::get<PassDepthAttachment>(output.attachment_);
                attachment.loadOp = depth.loadOp_;
                attachment.storeOp = depth.storeOp_;
                attachment.stencilLoadOp = depth.stencilLoadOp_;
                attachment.stencilStoreOp = depth.stencilStoreOp_;
                attachment.initialLayout = depth.initialLayout_;
                attachment.finalLayout = depth.finalLayout_;
            }

            return attachment;
        }

        VkClearValue BuildClearValue(const PassOutputResource& output)
        {
            VkClearValue clear{};
            if (const auto* color = std::get_if<PassColorAttachment>(&output.attachment_))
            {
                clear.color = color->clearValue_;
            }
            else
            {
                clear.depthStencil = std::get<PassDepthAttachment>(output.attachment_).clearValue_;
            }
            return clear;
        }
    }

    RenderPass::~RenderPass()
    {
        this->DestroyRenderTarget();
    }

    void RenderPass::Prepare(const CompiledPass& compiledPass, const RenderResourceRegistry& resources, core::SwapChain& swapChain, const RenderScene& renderScene)
    {
        this->renderResources_ = &resources;
        this->CreateRenderTarget(compiledPass, resources, swapChain);
        this->ExecutePreProcess(renderScene);
    }

    void RenderPass::CreateRenderTarget(const CompiledPass& compiledPass, const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        const auto outputs = this->GetOutputResources();
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> colorReferences;
        attachments.reserve(outputs.size());
        colorReferences.reserve(outputs.size());
        this->clearValues_.clear();
        this->clearValues_.reserve(outputs.size());

        VkAttachmentReference depthReference{};
        bool hasDepthAttachment = false;
        for (uint32_t index = 0; index < outputs.size(); ++index)
        {
            const auto& output = outputs[index];
            attachments.push_back(BuildAttachmentDescription(output, swapChain));
            this->clearValues_.push_back(BuildClearValue(output));

            if (std::holds_alternative<PassColorAttachment>(output.attachment_))
            {
                colorReferences.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            }
            else
            {
                depthReference = {index, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                hasDepthAttachment = true;
            }
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
        subpass.pColorAttachments = colorReferences.data();
        subpass.pDepthStencilAttachment = hasDepthAttachment ? &depthReference : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        for (const auto& compiledDependency : compiledPass.attachmentDependencies_)
        {
            const auto output = std::find_if(outputs.begin(), outputs.end(), [&compiledDependency](const auto& candidate) {
                return candidate.resource_ == compiledDependency.resourceId_;
            });
            if (output == outputs.end()) continue;

            PassResourceUsage srcUsage =  compiledDependency.srcUsage_;
            PassResourceUsage dstUsage =  compiledDependency.dstUsage_;

            // src 侧：上一个 Pass 怎么写这个资源（按生产者的实际用法）
            switch (srcUsage.type_)
            {
                case ResourceUsage::ColorAttachment:
                    dependency.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    dependency.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    break;
                case ResourceUsage::DepthAttachment:
                    dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    dependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;
                default:
                    LOG_ERROR(this->GetName() << ": attachment dependency has non-attachment source usage.");
                    break;
            }

            // dst 侧：本 Pass 怎么用这个资源（按消费者的实际用法）
            switch (dstUsage.type_)
            {
                case ResourceUsage::ColorAttachment:
                    dependency.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    break;
                case ResourceUsage::DepthAttachment:
                    dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;
                default:
                    LOG_ERROR(this->GetName() << ": attachment dependency has non-attachment destination usage.");
                    break;
            }
        }

        const bool hasAttachmentDependency = dependency.srcStageMask != 0;
        VkRenderPassCreateInfo renderPassCI{};
        renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCI.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassCI.pAttachments = attachments.data();
        renderPassCI.subpassCount = 1;
        renderPassCI.pSubpasses = &subpass;
        renderPassCI.dependencyCount = hasAttachmentDependency ? 1u : 0u;
        renderPassCI.pDependencies = hasAttachmentDependency ? &dependency : nullptr;

        SUCCESS_OR_LOG(
            vkCreateRenderPass(core::Device::Instance().GetLogicalDeviceHandle(), &renderPassCI, nullptr, &this->renderPass_) == VK_SUCCESS,
            this->GetName() << ": failed to create render pass."
        );

        this->CreateFramebuffers(resources, swapChain);
    }

    void RenderPass::CreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        const auto outputs = this->GetOutputResources();
        const VkExtent2D extent = swapChain.GetExtent();
        this->framebuffers_.resize(swapChain.GetImageCount(), VK_NULL_HANDLE);

        for (uint32_t imageIndex = 0; imageIndex < this->framebuffers_.size(); ++imageIndex)
        {
            std::vector<VkImageView> views;
            views.reserve(outputs.size());
            for (const auto& output : outputs)
            {
                if (resources.HasImages(output.resource_))
                {
                    const auto& images = resources.GetImages(output.resource_);
                    views.push_back(images[images.size() == 1 ? 0 : imageIndex].imageView_);
                }
                else
                {
                    views.push_back(swapChain.GetSwapChainBuffer(imageIndex).view);
                }
            }

            VkFramebufferCreateInfo framebufferCI{};
            framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCI.renderPass = this->renderPass_;
            framebufferCI.attachmentCount = static_cast<uint32_t>(views.size());
            framebufferCI.pAttachments = views.data();
            framebufferCI.width = extent.width;
            framebufferCI.height = extent.height;
            framebufferCI.layers = 1;

            SUCCESS_OR_LOG(
                vkCreateFramebuffer(core::Device::Instance().GetLogicalDeviceHandle(), &framebufferCI, nullptr, &this->framebuffers_[imageIndex]) == VK_SUCCESS,
                this->GetName() << ": failed to create framebuffer."
            );
        }
    }

    void RenderPass::RecreateFramebuffers(const RenderResourceRegistry& resources, core::SwapChain& swapChain)
    {
        this->DestroyFramebuffers();
        this->CreateFramebuffers(resources, swapChain);
    }

    void RenderPass::DestroyFramebuffers()
    {
        const VkDevice device = core::Device::Instance().GetLogicalDeviceHandle();
        for (VkFramebuffer framebuffer : this->framebuffers_)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        this->framebuffers_.clear();
    }

    void RenderPass::DestroyRenderTarget()
    {
        this->DestroyFramebuffers();
        if (this->renderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(core::Device::Instance().GetLogicalDeviceHandle(), this->renderPass_, nullptr);
            this->renderPass_ = VK_NULL_HANDLE;
        }
    }

    VkRenderPass RenderPass::GetHandle() const noexcept
    {
        return this->renderPass_;
    }

    VkFramebuffer RenderPass::GetFramebuffer(uint32_t imageIndex) const
    {
        return this->framebuffers_.at(imageIndex);
    }

    std::span<const VkClearValue> RenderPass::GetClearValues() const noexcept
    {
        return this->clearValues_;
    }
}
