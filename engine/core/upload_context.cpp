#include "engine/core/upload_context.h"
#include "engine/utils/log.h"

namespace engine::core
{

    VkCommandBuffer UploadContext::CreateCommandBuffer()
    {
        auto& device = Device::Instance();

        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = this->commandPool_;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        SUCCESS_OR_LOG(
        vkAllocateCommandBuffers(device.GetLogicalDeviceHandle(), &cmdBufAllocateInfo, &cmdBuffer) == VK_SUCCESS,
            "Device: Failed to create command buffer."
        );

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        SUCCESS_OR_LOG(
            vkBeginCommandBuffer(cmdBuffer, &commandBufferBI) == VK_SUCCESS,
            "Device: Failed to begin command buffer."
        );

        return cmdBuffer;      
    }


    VkCommandBuffer UploadContext::BeginSingleTimeCommand(VkCommandBufferLevel level)
    {
        auto& device = Device::Instance();
        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = this->commandPool_;
        cmdBufAllocateInfo.level = level;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        SUCCESS_OR_LOG(
            vkAllocateCommandBuffers(this->device_, &cmdBufAllocateInfo, &cmdBuffer) == VK_SUCCESS,
            "Device: Failed to create command buffer."
        );

        
        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        SUCCESS_OR_LOG(
            vkBeginCommandBuffer(cmdBuffer, &commandBufferBI) == VK_SUCCESS,
            "Device: Failed to begin command buffer."
        );

        return cmdBuffer;
    }

    void UploadContext::EndSingleTimeCommand(VkCommandBuffer commandBuffer)
    {
        SUCCESS_OR_LOG(
            vkEndCommandBuffer(commandBuffer) == VK_SUCCESS,
            "Device: Failed to end command buffer."
        );

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;

        SUCCESS_OR_LOG(
            vkCreateFence(this->device_, &fenceInfo, nullptr, &fence) == VK_SUCCESS,
            "Device: Failed to create fence."
        );

        // Submit to the queue
        SUCCESS_OR_LOG(
            vkQueueSubmit(this->queue_, 1, &submitInfo, fence) == VK_SUCCESS,
            "Device: Failed to submit queue."
        );
        // Wait for the fence to signal that command buffer has finished executing
        SUCCESS_OR_LOG(
            vkWaitForFences(this->device_, 1, &fence, VK_TRUE, 100000000000) == VK_SUCCESS,
            "Device: Failed to wait for fences."
        );

        vkDestroyFence(this->device_, fence, nullptr);

        vkFreeCommandBuffers(this->device_, this->commandPool_, 1, &commandBuffer);
    }


    VkCommandBuffer UploadContext::CreateCommandBuffer(VkCommandBufferLevel level)
    {
        auto& device = Device::Instance();
        VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = this->commandPool_;
        cmdBufAllocateInfo.level = level;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer;
        SUCCESS_OR_LOG(
            vkAllocateCommandBuffers(this->device_, &cmdBufAllocateInfo, &cmdBuffer) == VK_SUCCESS,
            "Device: Failed to create command buffer."
        );

        return cmdBuffer;
    }


    UploadContext& UploadContext::Instance()
    {
        static UploadContext instance;
        return instance;
    }

    UploadContext::~UploadContext()
    {
        this->Cleanup();
    }

    void UploadContext::Init()
    {
        auto& device = Device::Instance();
        this->device_ = device.GetLogicalDeviceHandle();
        this->queue_ = device.GetGraphicsQueue();

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device.GetGraphicsQueueFamilyIndices();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        SUCCESS_OR_LOG(
            vkCreateCommandPool(this->device_, &poolInfo, nullptr, &this->commandPool_) == VK_SUCCESS,
            "UploadContext: Failed to create command pool."
        );

    }

    void UploadContext::Cleanup()
    {
        auto& device = Device::Instance();
        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());

        for (auto& pending : this->pending_)
        {
            if (pending.commandBuffer != VK_NULL_HANDLE)
                vkFreeCommandBuffers(device.GetLogicalDeviceHandle(), this->commandPool_, 1, &pending.commandBuffer);
            if (pending.stagingBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(device.GetLogicalDeviceHandle(), pending.stagingBuffer, nullptr);
            if (pending.stagingMemory != VK_NULL_HANDLE)
                vkFreeMemory(device.GetLogicalDeviceHandle(), pending.stagingMemory, nullptr);
        }
        this->pending_.clear();

        if (this->commandPool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device.GetLogicalDeviceHandle(), this->commandPool_, nullptr);
            this->commandPool_ = VK_NULL_HANDLE;
        }
    }

    UploadResult UploadContext::UploadData(const void* data, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        auto& device = Device::Instance();

        // ① 创建目标 buffer（DEVICE_LOCAL，供 GPU 使用）
        VkBuffer target;
        VkDeviceMemory targetMemory;
        device.CreateBuffer(
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            size, &target, &targetMemory
        );

        // ② 创建 staging buffer 并填充数据（CPU 可写，同步 memcpy）
        VkBuffer staging;
        VkDeviceMemory stagingMemory;
        device.CreateBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            size, &staging, &stagingMemory, const_cast<void*>(data)
        );

        // ③ 记录 copy 命令（staging → target）
        VkCommandBuffer cmd = this->CreateCommandBuffer();
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, staging, target, 1, &copyRegion);
        vkEndCommandBuffer(cmd);

        // ④ 提交（不等待！）——fence 记录"何时 GPU 用完"
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
        VkFence fence;
        vkCreateFence(this->device_, &fenceInfo, nullptr, &fence);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(this->queue_, 1, &submitInfo, fence);

        // ⑤ CB + staging 进 pending_ 等回收（CB 处于 pending 状态，必须等 fence 完成后才能 free）
        this->pending_.push_back({ staging, stagingMemory, cmd, fence });

        return { target, targetMemory, fence };   // 调用方持有目标 buffer
    }

    void UploadContext::RecycleCompleted()
    {
        for (auto it = this->pending_.begin(); it != this->pending_.end(); )
        {
            if (vkGetFenceStatus(this->device_, it->fence) == VK_SUCCESS)
            {
                // GPU 已用完 CB + staging，安全销毁（fence 归调用方，不在此销毁）
                vkFreeCommandBuffers(this->device_, this->commandPool_, 1, &it->commandBuffer);
                vkDestroyBuffer(this->device_, it->stagingBuffer, nullptr);
                vkFreeMemory(this->device_, it->stagingMemory, nullptr);
                it = this->pending_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void UploadContext::WaitAll()
    {
        // 等所有已提交的上传命令完成（只等图形队列，比 vkDeviceWaitIdle 轻）
        vkQueueWaitIdle(this->queue_);
        // 此时所有 CB + staging 都安全，直接清空（fence 归调用方，不销毁）
        for (auto& pending : this->pending_)
        {
            vkFreeCommandBuffers(this->device_, this->commandPool_, 1, &pending.commandBuffer);
            vkDestroyBuffer(this->device_, pending.stagingBuffer, nullptr);
            vkFreeMemory(this->device_, pending.stagingMemory, nullptr);
        }
        this->pending_.clear();
    }

}



