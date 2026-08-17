#pragma once
#include <vector>
#include "engine/core/device.h"


namespace engine::core
{

    struct UploadResult
    {
        VkBuffer                        buffer = VK_NULL_HANDLE;
        VkDeviceMemory                  memory = VK_NULL_HANDLE;
        VkFence                         fence = VK_NULL_HANDLE;
    };

    class UploadContext
    {
        private:
            VkDevice                    device_ = VK_NULL_HANDLE;
            VkQueue                     queue_ = VK_NULL_HANDLE;
            VkCommandPool               commandPool_ = VK_NULL_HANDLE;
            // VkCommandBuffer             commandBuffer = VK_NULL_HANDLEL;
            
            struct PendingUpload
            {
                VkBuffer                stagingBuffer = VK_NULL_HANDLE;
                VkDeviceMemory          stagingMemory = VK_NULL_HANDLE;
                VkCommandBuffer         commandBuffer = VK_NULL_HANDLE;
                VkFence                 fence = VK_NULL_HANDLE;
            };

            std::vector<PendingUpload>  pending_;

            UploadContext() = default;
            ~UploadContext();

            UploadContext(const UploadContext&) = delete;
            UploadContext& operator = (const UploadContext&) = delete;

            VkCommandBuffer             CreateCommandBuffer();

        public:
            static UploadContext&       Instance();
            void                        Init();
            void                        Cleanup();

            VkCommandBuffer             BeginSingleTimeCommand(VkCommandBufferLevel level);
            void                        EndSingleTimeCommand(VkCommandBuffer commandBuffer);

            VkCommandBuffer             CreateCommandBuffer(VkCommandBufferLevel level);

            UploadResult                UploadData(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

            void                        RecycleCompleted();

            // 等所有已提交上传完成并回收 staging（初始化加载末尾调用）
            void                        WaitAll();


    };


}




