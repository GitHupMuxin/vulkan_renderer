#include "engine/core/device.h"
#include "engine/core/descriptor_allocator.h"
#include "engine/utils/log.h"

namespace engine::core
{

    const uint32_t DescriptorAllocator::initialSets = 256;

    DescriptorAllocator::DescriptorAllocator() { }

    DescriptorAllocator::~DescriptorAllocator() 
    {
     
    }

    VkDescriptorPool DescriptorAllocator::CreatDescriptroPool()
    {
        auto& device = Device::Instance();

        uint32_t initialSets = this->nextInitialSets_;
        nextInitialSets_ *= 2;

		std::vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, initialSets },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, initialSets },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, initialSets }
		};

		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolCI.maxSets = initialSets;
		descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		descriptorPoolCI.pPoolSizes = poolSizes.data();

        VkDescriptorPool pool;
		SUCCESS_OR_LOG(
            vkCreateDescriptorPool(device.GetLogicalDeviceHandle(), &descriptorPoolCI, nullptr, &pool) == VK_SUCCESS,
            "Renderer: Failed to set up descriptors."
        );

        this->persistentPools_.emplace_back(pool);  
        
        return pool;
    }

    DescriptorAllocator& DescriptorAllocator::Instance()
    {
        static DescriptorAllocator instance;
        return instance;
    }

    void DescriptorAllocator::Init()
    {
        LOG_INFO("DescriptorAllocator: initialzation...");
		auto& device = Device::Instance();

        this->nextInitialSets_ = DescriptorAllocator::initialSets;
        this->CreatDescriptroPool(); 
    }

    VkDescriptorSet DescriptorAllocator::AllocatePersistent(VkDescriptorSetLayout layout)
    {
        VkDescriptorSet outSet;
        for (auto& pool : this->persistentPools_)
        {
            if (this->TryAllocate(pool, layout, outSet))
            {
                this->setToPool_[outSet] = pool;
                return outSet;
            }
        }

        LOG_WARN("DescriptorAllocator: all pools exhausted, growing to" << this->nextInitialSets_ << " sets.");

        VkDescriptorPool newPool = this->CreatDescriptroPool();

        if (!this->TryAllocate(newPool, layout, outSet))
        {
            // 新建的池按全类型配额，单个 set 必然放得下；到这里说明设备级上限或驱动异常
            LOG_FATAL("DescriptorAllocator: allocation failed even after growing.");
        }
        this->setToPool_[outSet] = newPool;
        return outSet;
    }

    bool DescriptorAllocator::TryAllocate(VkDescriptorPool pool, VkDescriptorSetLayout layout, VkDescriptorSet& outSet)
    {
        auto& device = Device::Instance();
        VkDescriptorSetAllocateInfo CI{};
        CI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        CI.descriptorPool = pool;
        CI.pSetLayouts = &layout;
        CI.descriptorSetCount = 1;

        return vkAllocateDescriptorSets(device.GetLogicalDeviceHandle(), &CI, &outSet) == VK_SUCCESS;
    }

    void DescriptorAllocator::FreePersistent(VkDescriptorSet set)
    {
        auto it = this->setToPool_.find(set);
        if (it == this->setToPool_.end())
        {
            LOG_WARN("DescriptorAllocator: FreePersistent called unknown set.");
            return;
        }

        VkDescriptorPool pool = it->second;
        vkFreeDescriptorSets(Device::Instance().GetLogicalDeviceHandle(), pool, 1, &set);
        this->setToPool_.erase(it);
    }

    void DescriptorAllocator::Cleanup()
    {
        auto& device = Device::Instance();

        for (auto& pool : this->persistentPools_)
        {
            vkDestroyDescriptorPool(device.GetLogicalDeviceHandle(), pool, nullptr);
        }

        this->persistentPools_.clear();
        this->setToPool_.clear();
    }


}





