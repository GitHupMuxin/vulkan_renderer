#include "app/ui/ui.hpp"
#include "engine/core/loader.h"
#include "engine/resource/resource_manager.h"

namespace app::ui
{
    UI::UI(VkRenderPass renderPass, VkPipelineCache pipelineCache, VkSampleCountFlagBits multiSampleCount) 
    {
        auto& device = engine::core::Device::Instance();

        ImGui::CreateContext();

        /*
            Font texture loading
        */
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* fontData;
        int texWidth, texHeight;

        io.Fonts->AddFontFromFileTTF((engine::resource::ResourceManager::assetPath_ + "Roboto-Medium.ttf").c_str(), 16.0f);
        io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
        fontTexture.LoadFromBuffer(fontData, texWidth * texHeight * 4 * sizeof(char), VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight);

        /*
            Setup
        */
        ImGuiStyle& style = ImGui::GetStyle();
        style.FrameBorderSize = 0.0f;
        style.WindowBorderSize = 0.0f;

        /*
            Descriptor pool
        */
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };
        VkDescriptorPoolCreateInfo descriptorPoolCI{};
        descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.poolSizeCount = 1;
        descriptorPoolCI.pPoolSizes = poolSizes.data();
        descriptorPoolCI.maxSets = 1;

        SUCCESS_OR_LOG(
            vkCreateDescriptorPool(device.GetLogicalDeviceHandle(), &descriptorPoolCI, nullptr, &descriptorPool) == VK_SUCCESS,
            "UI: Failed to create descriptor pool."
        );

        /*
            Descriptor set layout
        */
        VkDescriptorSetLayoutBinding setLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
        descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCI.pBindings = &setLayoutBinding;
        descriptorSetLayoutCI.bindingCount = 1;

        SUCCESS_OR_LOG(
            vkCreateDescriptorSetLayout(device.GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &descriptorSetLayout) == VK_SUCCESS,
            "UI: Failed to create descriptor set layout."
        );

        /*
            Descriptor set
        */
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSetAllocInfo.descriptorSetCount = 1;

        SUCCESS_OR_LOG(
            vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &descriptorSetAllocInfo, &descriptorSet) == VK_SUCCESS,
            "UI: Failed to allocate descriptor sets."
        );

        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pImageInfo = &fontTexture.descriptor_;

        vkUpdateDescriptorSets(device.GetLogicalDeviceHandle(), 1, &writeDescriptorSet, 0, nullptr);

        /*
            Pipeline layout
        */
        VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock) };

        VkPipelineLayoutCreateInfo pipelineLayoutCI{};
        pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        pipelineLayoutCI.setLayoutCount = 1;
        pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

        SUCCESS_OR_LOG(
            vkCreatePipelineLayout(device.GetLogicalDeviceHandle(), &pipelineLayoutCI, nullptr, &pipelineLayout) == VK_SUCCESS,
            "UI: Failed to create pipeline layout."
        );

        /*
            Pipeline
        */
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
        inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
        rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationStateCI.lineWidth = 1.0f;

        VkPipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
        colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendStateCI.attachmentCount = 1;
        colorBlendStateCI.pAttachments = &blendAttachmentState;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
        depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilStateCI.depthTestEnable = VK_FALSE;
        depthStencilStateCI.depthWriteEnable = VK_FALSE;
        depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilStateCI.front = depthStencilStateCI.back;
        depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineViewportStateCreateInfo viewportStateCI{};
        viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCI.viewportCount = 1;
        viewportStateCI.scissorCount = 1;

        VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
        multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

		// 与 Device 的 MSAA 开关保持一致：开 → 传入的 sampleCount，关 → 1x
		multisampleStateCI.rasterizationSamples = engine::core::Device::Instance().GetSetting().multiSampling_ ? multiSampleCount : VK_SAMPLE_COUNT_1_BIT;

        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicStateCI{};
        dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
        dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

        VkVertexInputBindingDescription vertexInputBinding = { 0, 20, VK_VERTEX_INPUT_RATE_VERTEX };
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 },
            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 },
        };
        VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
        vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputStateCI.vertexBindingDescriptionCount = 1;
        vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCI{};
        pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCI.layout = pipelineLayout;
        pipelineCI.renderPass = renderPass;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pVertexInputState = &vertexInputStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();

        pipelineCI.layout = pipelineLayout;

        std::string assetPath = engine::resource::ResourceManager::assetPath_;

        shaderStages = {
            engine::core::Loader::LoadShader(device.GetLogicalDeviceHandle(), assetPath + "shaders/ui.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            engine::core::Loader::LoadShader(device.GetLogicalDeviceHandle(), assetPath + "shaders/ui.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        SUCCESS_OR_LOG(
            vkCreateGraphicsPipelines(device.GetLogicalDeviceHandle(), pipelineCache, 1, &pipelineCI, nullptr, &pipeline) == VK_SUCCESS,
            "UI: Failed to create graphics pipeline."
        );

        for (auto shaderStage : shaderStages) 
        {
            vkDestroyShaderModule(device.GetLogicalDeviceHandle(), shaderStage.module, nullptr);
        }
    }
    
    UI::~UI() 
    {
        auto& device = engine::core::Device::Instance();
        ImGui::DestroyContext();
        vertexBuffer.Destroy();
        indexBuffer.Destroy();
        vkDestroyPipeline(device.GetLogicalDeviceHandle(), pipeline, nullptr);
        vkDestroyPipelineLayout(device.GetLogicalDeviceHandle(), pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device.GetLogicalDeviceHandle(), descriptorSetLayout, nullptr);
        vkDestroyDescriptorPool(device.GetLogicalDeviceHandle(), descriptorPool, nullptr);
    }

    void UI::Draw(VkCommandBuffer cmdBuffer) 
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        const VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UI::PushConstBlock), &pushConstBlock);

        ImDrawData* imDrawData = ImGui::GetDrawData();
        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;
        for (int32_t j = 0; j < imDrawData->CmdListsCount; j++) {
            const ImDrawList* cmd_list = imDrawData->CmdLists[j];
            for (int32_t k = 0; k < cmd_list->CmdBuffer.Size; k++) {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[k];
                VkRect2D scissorRect;
                scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
                scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
                scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
                scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
                vkCmdSetScissor(cmdBuffer, 0, 1, &scissorRect);
                vkCmdDrawIndexed(cmdBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
                indexOffset += pcmd->ElemCount;
            }
            vertexOffset += cmd_list->VtxBuffer.Size;
        }
    }

    bool UI::Header(const char *caption) 
    {
        return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen);
    }
    
    bool UI::Slider(const char* caption, float* value, float min, float max) 
    {
        return ImGui::SliderFloat(caption, value, min, max);
    }

    bool UI::Combo(const char *caption, int32_t *itemindex, std::vector<std::string> items) 
    {
        if (items.empty()) 
            return false;
        
        std::vector<const char*> charitems;
        charitems.reserve(items.size());

        for (size_t i = 0; i < items.size(); i++) 
            charitems.push_back(items[i].c_str());

        uint32_t itemCount = static_cast<uint32_t>(charitems.size());
        return ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
    }

    bool UI::Combo(const char *caption, std::string &selectedkey, std::map<std::string, std::string> items) 
    {
        bool selectionChanged = false;
        if (ImGui::BeginCombo(caption, selectedkey.c_str())) 
        {
            for (auto it = items.begin(); it != items.end(); ++it) 
            {
                const bool isSelected = it->first == selectedkey;
                if (ImGui::Selectable(it->first.c_str(), isSelected)) 
                {
                    selectionChanged = it->first != selectedkey;
                    selectedkey = it->first;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return selectionChanged;
    }

    bool UI::Button(const char *caption) 
    {
        return ImGui::Button(caption);
    }

    void UI::Text(const char *formatstr, ...) 
    {
        va_list args;
        va_start(args, formatstr);
        ImGui::TextV(formatstr, args);
        va_end(args);
    } 
}