#include <algorithm>
#include <engine/core/device.h>
#include "engine/core/descriptor_layout_registry.h"
#include "engine/utils/log.h"

namespace engine::core
{
    void DescriptorLayoutRegistry::HashCombine(std::size_t& seed, std::size_t value)
    {
        seed ^= value
            + 0x9e3779b9
            + (seed << 6)
            + (seed >> 2); 
    }

    bool DescriptorLayoutRegistry::DescriptorBindingKey::operator==(const DescriptorBindingKey& other) const
    {
        return this->binding == other.binding &&
                this->type == other.type &&
                this->count == other.count &&
                this->stages == other.stages;
    }

    size_t DescriptorLayoutRegistry::DescriptorLayoutKeyHash::operator()(const DescriptorLayoutKey& key) const
    {
        size_t seed = 0;

        for (const auto& binding : key.bindings)
        {
            DescriptorLayoutRegistry::HashCombine(seed, std::hash<uint32_t>{}(binding.binding));
            DescriptorLayoutRegistry::HashCombine(seed, std::hash<uint32_t>{}(binding.type));
            DescriptorLayoutRegistry::HashCombine(seed, std::hash<uint32_t>{}(binding.count));
            DescriptorLayoutRegistry::HashCombine(seed, std::hash<uint32_t>{}(binding.stages));
        }

        return seed;
    }

    DescriptorLayoutRegistry::~DescriptorLayoutRegistry()
    {
        
    }

    DescriptorLayoutRegistry::DescriptorLayoutKey DescriptorLayoutRegistry::MakeKey(std::span<const VkDescriptorSetLayoutBinding> inputBinding)
    {
        DescriptorLayoutKey key;
        key.bindings.reserve(inputBinding.size());

        for (const auto& binding : inputBinding)
        {
            DescriptorBindingKey bindingKey = {
                .binding = binding.binding,
                .type = binding.descriptorType,
                .count = binding.descriptorCount,
                .stages = binding.stageFlags
            };
            key.bindings.push_back(bindingKey);
        }

        std::sort(key.bindings.begin(), key.bindings.end(), 
            [](const DescriptorBindingKey& a, const DescriptorBindingKey& b){
                return a.binding < b.binding;
            }
        );

        return key;
    }

    void DescriptorLayoutRegistry::Cleanup()
    {
        auto& device = core::Device::Instance();
        for (auto& it : this->cache_)
        {
            if (it.second != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device.GetLogicalDeviceHandle(), it.second, nullptr);
            }
        }

        this->cache_.clear();
    }

    DescriptorLayoutRegistry& DescriptorLayoutRegistry::Instance()
    {
        static DescriptorLayoutRegistry instance;
        return instance;
    }

    void DescriptorLayoutRegistry::Init()
    {
        this->cache_.clear();       
    }

    VkDescriptorSetLayout DescriptorLayoutRegistry::GetOrCreate(std::span<const VkDescriptorSetLayoutBinding> binding)
    {
        DescriptorLayoutKey key = this->MakeKey(binding);

        const auto& it = this->cache_.find(key);

        if (it != this->cache_.end())
            return it->second;
        else 
        {
            VkDescriptorSetLayout layout;
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
            descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCI.pBindings = binding.data();
            descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(binding.size());

            SUCCESS_OR_LOG(
                vkCreateDescriptorSetLayout(core::Device::Instance().GetLogicalDeviceHandle(), &descriptorSetLayoutCI, nullptr, &layout) == VK_SUCCESS,
                "Renderer: Failed to create descriptor set layout."
            );

            this->cache_.emplace(std::make_pair(key, layout));

            return layout;
        }
    }
}






