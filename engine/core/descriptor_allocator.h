#pragma once
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace engine::core
{
    class DescriptorAllocator
    {
        private:
            static const uint32_t initialSets;
            uint32_t nextInitialSets_;

            std::vector<VkDescriptorPool> persistentPools_;
            std::unordered_map<VkDescriptorSet, VkDescriptorPool> setToPool_;

            DescriptorAllocator();
            ~DescriptorAllocator();

            DescriptorAllocator(const DescriptorAllocator&) = delete;
            DescriptorAllocator& operator = (const DescriptorAllocator&) = delete;

            VkDescriptorPool                CreatDescriptroPool();
            bool                            TryAllocate(VkDescriptorPool pool, VkDescriptorSetLayout layout, VkDescriptorSet& outSet);
        public:
            static DescriptorAllocator&     Instance();
            void                            Init();
            VkDescriptorSet                 AllocatePersistent(VkDescriptorSetLayout layout);            
            void                            FreePersistent(VkDescriptorSet set);

            void                            Cleanup();

    };

}




