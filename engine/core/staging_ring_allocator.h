#pragma once
#include <vector>
#include <queue>
#include <cstddef>
#include "engine/core/device.h"


namespace engine::core
{

    struct StagingBufferSlot
    {
        // owner 指针 指向大 buffer
        VkBuffer                                StagingRingBuffer = VK_NULL_HANDLE;

        // 传给 VkBufferCopy::srcOffset 大 buffer 的起点
        VkDeviceSize                            offsetOfStagingRingBuffer = 0;

        // 用户真正请求的数据大小 
        VkDeviceSize                            size = 0;

        // 实际消耗：alignment padding + wrap padding + size
        VkDeviceSize                            reservedBytes = 0;

        // CPU memcpy 目标
        void*                                   stagingMemory = nullptr;
    };

    // 描述，但不管理
    struct PendingCopy
    {
        StagingBufferSlot                       stagingSlot;
        VkCommandBuffer                         commandBuffer = VK_NULL_HANDLE;
        uint64_t                                submitValue = 0;
        VkBuffer                                dst = VK_NULL_HANDLE;
        VkDeviceSize                            dstOffset = 0;
    };


    class StagingRingBuffer
    {
        private:
            VkBuffer                            buffer_ = VK_NULL_HANDLE;
            VkDeviceMemory                      memory_ = VK_NULL_HANDLE;
            std::byte*                          mapped_ = nullptr;

            VkDeviceSize                        capacity_ = 0;      // ring 总容量
            VkDeviceSize                        headOffset_ = 0;    // 水位线（下一个分配位置）
            VkDeviceSize                        usage_ = 0;         // 在途字节数（FIFO 记账）

        public:
            StagingRingBuffer();
            StagingRingBuffer(const StagingRingBuffer&) = delete;
            StagingRingBuffer& operator = (const StagingRingBuffer&) = delete;

            void Init();
            void Cleanup();

            bool CanFit(VkDeviceSize size) const;
            StagingBufferSlot Acquire(VkDeviceSize size);
            void Release(const StagingBufferSlot& slot);    // 还账：usage_ -= reservedBytes
            bool IsEmpty() const;
            void Reset();                                   // 仅 usage_==0 时允许回绕

    };
                
    struct UploadReceipt
    {
        uint64_t readyAt;   
    };



    class StagingRingAllocator
    {
        private:
            VkDevice                            device_ = VK_NULL_HANDLE;
            VkQueue                             queue_ = VK_NULL_HANDLE;
            VkCommandPool                       commandPool_ = VK_NULL_HANDLE;
            uint64_t                            timeLine_ = 0;

            StagingRingBuffer                   stagingRingBuffer_;
            VkSemaphore                         timelineSemaphore_ = VK_NULL_HANDLE;
            uint64_t                            submittedValue_ = 0;   
            uint64_t                            completedValue_ = 0;   

            std::queue<PendingCopy>             waitingList_;

            static const uint32_t               commandSize_;
            std::queue<VkCommandBuffer>         freeCommandBufferList_;

            StagingRingAllocator();
            ~StagingRingAllocator();

            StagingRingAllocator(const StagingRingAllocator&) = delete;
            StagingRingAllocator& operator = (const StagingRingAllocator&) = delete;

            bool ExecuteBufferCopy(PendingCopy& pendingCopy);
            void FreePendingCopy(PendingCopy& pendingCopy);

            void Recycle();
            bool GetCommandBuffer(VkCommandBuffer& cmd);
        public:
            static StagingRingAllocator&        Instance();
            void                                Init();
            void                                Cleanup();

            UploadReceipt                       SubmitBufferCopy(const void* src, VkDeviceSize srcSize, VkBuffer dst, VkDeviceSize dstOffset = 0);

            UploadReceipt                       SubmitImageCopy(
                                                    const void* src, VkDeviceSize srcSize,
                                                    VkImage dst,                                
                                                    const std::vector<VkBufferImageCopy>& regions,  
                                                    VkImageLayout finalLayout,                  
                                                    uint32_t baseMip = 0, uint32_t mipCount = 0,
                                                    uint32_t baseLayer = 0, uint32_t layerCount = 0);

            uint64_t                            GetCompletedValue();          
            bool                                IsCompleted(uint64_t at);     
            void                                WaitUntil(uint64_t at);      
            void                                WaitAll(); 

    };


}




