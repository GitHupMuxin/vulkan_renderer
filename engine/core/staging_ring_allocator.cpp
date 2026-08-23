#include <cstring>
#include <cstdint>
#include <algorithm>
#include <assert.h>
#include "engine/core/staging_ring_allocator.h"
#include "engine/utils/log.h"

namespace engine::core
{
    const uint32_t StagingRingAllocator::commandSize_ = 4;


    StagingRingBuffer::StagingRingBuffer()
    {
        
    }

    void StagingRingBuffer::Init()
    {
        auto& device = Device::Instance();
        constexpr VkDeviceSize kCapacity = 64 * 1024 * 1024;   // 64MB

        // 一块大 buffer：HOST_VISIBLE + HOST_COHERENT = CPU/GPU 交接窗口
        device.CreateBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            kCapacity, &this->buffer_, &this->memory_
        );

        SUCCESS_OR_LOG(
            vkMapMemory(device.GetLogicalDeviceHandle(), this->memory_, 0, kCapacity, 0, (void**)&this->mapped_) == VK_SUCCESS,
            "StagingRingBuffer: Failed to map memory."
        );

        this->capacity_ = kCapacity;
        this->headOffset_ = 0;
        this->usage_ = 0;
    }

    void StagingRingBuffer::Cleanup()
    {
        auto& device = Device::Instance();
        if (this->mapped_ != nullptr)
        {
            vkUnmapMemory(device.GetLogicalDeviceHandle(), this->memory_);
            this->mapped_ = nullptr;
        }
        if (this->memory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device.GetLogicalDeviceHandle(), this->memory_, nullptr);
            this->memory_ = VK_NULL_HANDLE;
        }
        if (this->buffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device.GetLogicalDeviceHandle(), this->buffer_, nullptr);
            this->buffer_ = VK_NULL_HANDLE;
        }
        this->capacity_ = 0;
        this->headOffset_ = 0;
        this->usage_ = 0;
    }

    bool StagingRingBuffer::CanFit(VkDeviceSize size) const
    {
        return this->headOffset_ + size <= this->capacity_;
    }

    StagingBufferSlot StagingRingBuffer::Acquire(VkDeviceSize size)
    {
        assert(this->headOffset_ + size <= this->capacity_);   // 调用方必须先 CanFit

        // 对齐到 16 字节；尾部不足则截断到剩余空间（wrap padding 计入 reservedBytes）
        VkDeviceSize aligned = (size + 15) & ~15;
        if (aligned > this->capacity_ - this->headOffset_)
            aligned = this->capacity_ - this->headOffset_;

        StagingBufferSlot slot{};
        slot.StagingRingBuffer = this->buffer_;
        slot.offsetOfStagingRingBuffer = this->headOffset_;
        slot.size = size;                                    // 用户数据大小
        slot.reservedBytes = aligned;                        // 实际占用（对齐+截断损耗）
        slot.stagingMemory = static_cast<void*>(this->mapped_ + this->headOffset_);

        this->headOffset_ += aligned;
        this->usage_ += aligned;

        return slot;
    }

    void StagingRingBuffer::Release(const StagingBufferSlot& slot)
    {
        this->usage_ -= slot.reservedBytes;                  // 还账：必须用 reservedBytes
    }

    bool StagingRingBuffer::IsEmpty() const
    {
        return this->usage_ == 0;
    }

    void StagingRingBuffer::Reset()
    {
        assert(this->IsEmpty());                             // 仅当全部在途完成才允许回绕
        this->headOffset_ = 0;
    }

    StagingRingAllocator::StagingRingAllocator() { }

    StagingRingAllocator::~StagingRingAllocator() 
    {
        this->Cleanup();
    }



    StagingBufferSlot StagingRingAllocator::AcquireSlot(VkDeviceSize size)
    {
        VkDeviceSize aligned = (size + 15) & ~15;

        this->Recycle();

        if (!this->stagingRingBuffer_.CanFit(aligned))
        {
            if (!this->waitingList_.empty())
            {
                LOG_WARN("StagingRingAllocator: wrap with in-flight uploads");
                this->WaitUntil(this->waitingList_.front().submitValue);
                this->Recycle();
            }
            this->stagingRingBuffer_.Reset();
        }

        return this->stagingRingBuffer_.Acquire(size);
    }

    VkCommandBuffer StagingRingAllocator::AcquireCommandBuffer()
    {
        VkCommandBuffer cmd;
        if (!this->GetCommandBuffer(cmd))
        {
            LOG_WARN("StagingRingAllocator: command buffer pool exhausted, waiting for oldest in-flight upload.");
            this->WaitUntil(this->waitingList_.front().submitValue);
            this->Recycle();
            this->GetCommandBuffer(cmd);
        }

        return cmd;
    }

    uint64_t StagingRingAllocator::SubmitRecorded(VkCommandBuffer cmd)
    {
        SUCCESS_OR_LOG(
            vkEndCommandBuffer(cmd) == VK_SUCCESS,
            "StagingRingAllocator: Failed to end command buffer."
        );

        // ===== 取号 + 提交：号必须在提交前取，提交后号就是回执 =====
        uint64_t submitValue = ++this->submittedValue_;

        uint64_t signalValue = submitValue;   // 局部副本，保证 vkQueueSubmit 调用期间稳定

        VkTimelineSemaphoreSubmitInfo timelineSubmit{};
        timelineSubmit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineSubmit.signalSemaphoreValueCount = 1;
        timelineSubmit.pSignalSemaphoreValues = &signalValue;   // "执行完把计数器推进到号"

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &timelineSubmit;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;                       // ← 必须和 valueCount 配对！
        submitInfo.pSignalSemaphores = &this->timelineSemaphore_;  // ← signal 哪个 semaphore

        SUCCESS_OR_LOG(
            vkQueueSubmit(this->queue_, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
            "StagingRingAllocator: Failed to submit queue."
        );

        return submitValue;
    }

    void StagingRingAllocator::FreePendingCopy(PendingUpload& pendingUpload)
    {
        // CB 还池；staging 归位：常规路径还 ring 切片，超大回退路径销毁一次性 buffer
        this->freeCommandBufferList_.push(pendingUpload.commandBuffer);

        if (pendingUpload.ownedBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(this->device_, pendingUpload.ownedBuffer, nullptr);
            vkFreeMemory(this->device_, pendingUpload.ownedMemory, nullptr);
            pendingUpload.ownedBuffer = VK_NULL_HANDLE;
            pendingUpload.ownedMemory = VK_NULL_HANDLE;
        }
        else
        {
            this->stagingRingBuffer_.Release(pendingUpload.slot);
        }
    }

    void StagingRingAllocator::Recycle()
    {
        while (!this->waitingList_.empty())
        {
            PendingUpload& tempUpload = this->waitingList_.front();
            if (tempUpload.submitValue <= this->GetCompletedValue())
            {
                this->FreePendingCopy(tempUpload);
                this->waitingList_.pop();
            }
            else
                break;
        }
    }

    bool StagingRingAllocator::GetCommandBuffer(VkCommandBuffer& cmd)
    {
        if (!this->freeCommandBufferList_.empty())
        {
            cmd = this->freeCommandBufferList_.front();
            this->freeCommandBufferList_.pop();
            return true;
        }

        return false;
    }

    StagingRingAllocator& StagingRingAllocator::Instance()
    {
        static StagingRingAllocator instance;
        return instance;
    }

    void StagingRingAllocator::Init()
    {
        auto& device = Device::Instance();
        this->device_ = device.GetLogicalDeviceHandle();
        this->queue_ = device.GetGraphicsQueue();
        LOG_INFO("StagingRingAllocator: Init start, device=" << (void*)this->device_);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device.GetGraphicsQueueFamilyIndices();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        SUCCESS_OR_LOG(
            vkCreateCommandPool(this->device_, &poolInfo, nullptr, &this->commandPool_) == VK_SUCCESS,
            "StagingRingAllocator: Failed to create command pool."
        );
        LOG_INFO("StagingRingAllocator: command pool created");

        for (int i = 0; i < StagingRingAllocator::commandSize_; i++)
        {
            auto& device = Device::Instance();
            VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
            cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufAllocateInfo.commandPool = this->commandPool_;
            cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufAllocateInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuffer;
            SUCCESS_OR_LOG(
                vkAllocateCommandBuffers(this->device_, &cmdBufAllocateInfo, &cmdBuffer) == VK_SUCCESS,
                "Device: Failed to create command buffer."
            );

            this->freeCommandBufferList_.push(cmdBuffer);
        }
        LOG_INFO("StagingRingAllocator: command buffers created, count=" << this->freeCommandBufferList_.size());

        VkSemaphoreTypeCreateInfo typeInfo{};
        typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0;

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semInfo.pNext = &typeInfo;

        SUCCESS_OR_LOG(
            vkCreateSemaphore(this->device_, &semInfo, nullptr, &this->timelineSemaphore_) == VK_SUCCESS,
            "StagingRingAllocator: Failed to create timeline semaphore."
        );
        LOG_INFO("StagingRingAllocator: timeline semaphore created");

        // ring 大 buffer 创建 + 映射
        LOG_INFO("StagingRingAllocator: start to init staging ring buffer");
        this->stagingRingBuffer_.Init();
        LOG_INFO("StagingRingAllocator: staging ring buffer ready");
    }

    void StagingRingAllocator::Cleanup()
    {
        auto& device = Device::Instance();
        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());

        // clear pendingUpload（此时全部已完成，直接出队，资源归位）
        while (!this->waitingList_.empty())
        {
            PendingUpload& tempUpload = this->waitingList_.front();
            this->FreePendingCopy(tempUpload);
            this->waitingList_.pop();
        }

        // clear VkCommandBuffer（池里的 CB 全部 free）
        while (!this->freeCommandBufferList_.empty())
        {
            VkCommandBuffer cmd = this->freeCommandBufferList_.front();
            this->freeCommandBufferList_.pop();
            vkFreeCommandBuffers(device.GetLogicalDeviceHandle(), this->commandPool_, 1, &cmd);
        }

        // clear timeline semaphore
        if (this->timelineSemaphore_ != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device.GetLogicalDeviceHandle(), this->timelineSemaphore_, nullptr);
            this->timelineSemaphore_ = VK_NULL_HANDLE;
        }

        // clear StagingRingBuffer
        this->stagingRingBuffer_.Cleanup();

        // clear VkCommandPool
        if (this->commandPool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device.GetLogicalDeviceHandle(), this->commandPool_, nullptr);
            this->commandPool_ = VK_NULL_HANDLE;
        }
    }

    UploadReceipt StagingRingAllocator::SubmitBufferCopy(const void* src, VkDeviceSize srcSize, VkBuffer dst, VkDeviceSize dstOffset)
    {
        assert(srcSize > 0 && dst != VK_NULL_HANDLE);

        VkDeviceSize aligned = (srcSize + 15) & ~15;
        if (aligned > this->stagingRingBuffer_.GetCapacity())
        {
            return this->SubmitOversizedBufferCopy(src, srcSize, dst, dstOffset);
        }

        StagingBufferSlot slot = this->AcquireSlot(srcSize);
        std::memcpy(slot.stagingMemory, src, srcSize);

        VkCommandBuffer cmd = this->AcquireCommandBuffer();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;    // 录一次提一次，驱动可优化

        SUCCESS_OR_LOG(
            vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS,
            "StagingRingAllocator: Failed to begin command buffer."
        );

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = slot.offsetOfStagingRingBuffer;   // ring 切片起点
        copyRegion.dstOffset = dstOffset;                        // 目标 buffer 内落点
        copyRegion.size      = srcSize;                          // 用户数据大小
        vkCmdCopyBuffer(cmd, slot.StagingRingBuffer, dst, 1, &copyRegion);

        uint64_t submitValue = this->SubmitRecorded(cmd);

        PendingUpload pending{};
        pending.submitValue = submitValue;
        pending.commandBuffer = cmd;
        pending.slot = slot;
        this->waitingList_.push(pending);

        return { submitValue };
    }

    UploadReceipt StagingRingAllocator::SubmitOversizedBufferCopy(const void* src, VkDeviceSize srcSize, VkBuffer dst, VkDeviceSize dstOffset)
    {
        LOG_WARN(
            "StagingRingAllocator: upload of " << srcSize << " bytes exceeds ring capacity " <<
            this->stagingRingBuffer_.GetCapacity() << ", falling back to dedicated staging buffer."
        );

        auto& device = Device::Instance();

        // 一次性专用 staging（CreateBuffer 的 data 参数内部完成分配 + 上传），不占 ring
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        device.CreateBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            srcSize, &stagingBuffer, &stagingMemory, const_cast<void*>(src)
        );

        VkCommandBuffer cmd = this->AcquireCommandBuffer();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        SUCCESS_OR_LOG(
            vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS,
            "StagingRingAllocator: Failed to begin command buffer."
        );

        VkBufferCopy copyRegion{};
        copyRegion.dstOffset = dstOffset;
        copyRegion.size      = srcSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, dst, 1, &copyRegion);

        uint64_t submitValue = this->SubmitRecorded(cmd);

        // 临时 staging 挂在 timeline 上延迟销毁：GPU 完成前不可释放
        PendingUpload pending{};
        pending.submitValue = submitValue;
        pending.commandBuffer = cmd;
        pending.ownedBuffer = stagingBuffer;
        pending.ownedMemory = stagingMemory;
        this->waitingList_.push(pending);

        return { submitValue };
    }

    UploadReceipt StagingRingAllocator::SubmitImageCopy(
        const void* src, VkDeviceSize srcSize,
        VkImage dst,
        const std::vector<VkBufferImageCopy>& regions,
        VkImageLayout finalLayout,
        VkImageLayout oldLayout)
    {
        assert(srcSize > 0 && dst != VK_NULL_HANDLE && !regions.empty());

        StagingBufferSlot slot = this->AcquireSlot(srcSize);
        std::memcpy(slot.stagingMemory, src, srcSize);

        // region 偏移修正：调用方的 bufferOffset 相对数据块开头，录制时统一加上 ring 切片起点
        std::vector<VkBufferImageCopy> fixedRegions = regions;
        for (auto& region : fixedRegions)
        {
            region.bufferOffset += slot.offsetOfStagingRingBuffer;
        }

        // barrier 范围从 regions 推导：恰好覆盖被写入的 mip/layer，不碰未写的子资源
        uint32_t baseMipLevel = UINT32_MAX;
        uint32_t maxMipLevel = 0;
        uint32_t baseArrayLayer = UINT32_MAX;
        uint32_t maxArrayLayer = 0;
        for (const auto& region : regions)
        {
            baseMipLevel = std::min(baseMipLevel, region.imageSubresource.mipLevel);
            maxMipLevel = std::max(maxMipLevel, region.imageSubresource.mipLevel);
            baseArrayLayer = std::min(baseArrayLayer, region.imageSubresource.baseArrayLayer);
            maxArrayLayer = std::max(maxArrayLayer, region.imageSubresource.baseArrayLayer);
        }

        VkImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = baseMipLevel;
        subresourceRange.levelCount = maxMipLevel - baseMipLevel + 1;
        subresourceRange.baseArrayLayer = baseArrayLayer;
        subresourceRange.layerCount = maxArrayLayer - baseArrayLayer + 1;

        VkCommandBuffer cmd = this->AcquireCommandBuffer();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        SUCCESS_OR_LOG(
            vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS,
            "StagingRingAllocator: Failed to begin command buffer."
        );

        // 进入屏障：oldLayout -> TRANSFER_DST
        // UNDEFINED / PREINITIALIZED 起点无先行访问需要同步（srcAccessMask = 0）
        VkImageMemoryBarrier preBarrier{};
        preBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarrier.oldLayout = oldLayout;
        preBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        preBarrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED || oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED) ? 0 : VK_ACCESS_MEMORY_READ_BIT;
        preBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        preBarrier.image = dst;
        preBarrier.subresourceRange = subresourceRange;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &preBarrier);

        // N 条拷贝夹在一对屏障中间，源是 ring 大 buffer（偏移已在 region 内修正）
        vkCmdCopyBufferToImage(
            cmd,
            slot.StagingRingBuffer,
            dst,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(fixedRegions.size()),
            fixedRegions.data()
        );

        // 退出屏障：TRANSFER_DST -> finalLayout
        VkImageMemoryBarrier postBarrier{};
        postBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        postBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        postBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        postBarrier.newLayout = finalLayout;
        postBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        postBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        postBarrier.image = dst;
        postBarrier.subresourceRange = subresourceRange;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &postBarrier);

        uint64_t submitValue = this->SubmitRecorded(cmd);

        PendingUpload pending{};
        pending.submitValue = submitValue;
        pending.commandBuffer = cmd;
        pending.slot = slot;
        this->waitingList_.push(pending);

        return { submitValue };
    }

    void StagingRingAllocator::Tick()
    {
        this->Recycle();
    }

    uint64_t StagingRingAllocator::GetCompletedValue()
    {
        uint64_t value = 0;
        vkGetSemaphoreCounterValue(this->device_, this->timelineSemaphore_, &value);
        this->completedValue_ = value;
        return this->completedValue_;
    }

    bool StagingRingAllocator::IsCompleted(uint64_t at)
    {
        return at <= this->GetCompletedValue();
    }

    void StagingRingAllocator::WaitUntil(uint64_t at)
    {
        if (at <= this->GetCompletedValue()) return;

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &timelineSemaphore_;
        waitInfo.pValues = &at;
        waitInfo.flags = 0;

        vkWaitSemaphores(this->device_, &waitInfo, UINT64_MAX);
    }

    void StagingRingAllocator::WaitAll()
    {
        this->WaitUntil(this->submittedValue_);
    }

}



