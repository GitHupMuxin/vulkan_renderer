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
    // 在途上传的生命周期记账：GPU 完成（timeline 值达成）后需要归还的资源。
    // 录制所需的信息（拷贝目标/布局等）在提交前已被消费，不进入队列。
    struct PendingUpload
    {
        uint64_t                                submitValue = 0;
        VkCommandBuffer                         commandBuffer = VK_NULL_HANDLE;
        StagingBufferSlot                       slot;                                   // 常规路径：ring 切片
        VkBuffer                                ownedBuffer = VK_NULL_HANDLE;           // 超大回退：一次性 staging，回收时销毁
        VkDeviceMemory                          ownedMemory = VK_NULL_HANDLE;
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

            VkDeviceSize GetCapacity() const { return this->capacity_; }

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

            StagingRingBuffer                   stagingRingBuffer_;
            VkSemaphore                         timelineSemaphore_ = VK_NULL_HANDLE;
            uint64_t                            submittedValue_ = 0;   
            uint64_t                            completedValue_ = 0;   

            std::queue<PendingUpload>           waitingList_;

            static const uint32_t               commandSize_;
            std::queue<VkCommandBuffer>         freeCommandBufferList_;

            StagingRingAllocator();
            ~StagingRingAllocator();

            StagingRingAllocator(const StagingRingAllocator&) = delete;
            StagingRingAllocator& operator = (const StagingRingAllocator&) = delete;

            // 三个 Submit 的公共骨架
            StagingBufferSlot                   AcquireSlot(VkDeviceSize size);        // 回收 + 回绕 + 取 ring 切片
            VkCommandBuffer                     AcquireCommandBuffer();               // 池空则等待最老在途项再回收
            uint64_t                            SubmitRecorded(VkCommandBuffer cmd);  // end + timeline 提交，返回回执值
            UploadReceipt                       SubmitOversizedBufferCopy(const void* src, VkDeviceSize srcSize, VkBuffer dst, VkDeviceSize dstOffset);

            void                                FreePendingCopy(PendingUpload& pendingUpload);

            void                                Recycle();
            bool                                GetCommandBuffer(VkCommandBuffer& cmd);
        public:
            static StagingRingAllocator&        Instance();
            void                                Init();
            void                                Cleanup();

            void                                Tick();                                // 每帧调用：回收已完成项，空闲期也还账

            UploadReceipt                       SubmitBufferCopy(const void* src, VkDeviceSize srcSize, VkBuffer dst, VkDeviceSize dstOffset = 0);

            UploadReceipt                       SubmitImageCopy(
                                                    const void* src, VkDeviceSize srcSize,
                                                    VkImage dst,
                                                    const std::vector<VkBufferImageCopy>& regions,
                                                    VkImageLayout finalLayout,
                                                    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED);

            uint64_t                            GetCompletedValue();          
            bool                                IsCompleted(uint64_t at);     
            void                                WaitUntil(uint64_t at);      
            void                                WaitAll(); 

    };


}




