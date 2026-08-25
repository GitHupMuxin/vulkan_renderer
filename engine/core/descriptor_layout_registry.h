#pragma once
#include <span>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace engine::core
{    
    class DescriptorLayoutRegistry
    {
        private:
            static void                             HashCombine(std::size_t& seed, std::size_t value);

            struct DescriptorBindingKey
            {
                uint32_t                            binding;
                VkDescriptorType                    type;
                uint32_t                            count;
                VkShaderStageFlags                  stages;

                bool                                operator==(const DescriptorBindingKey& other) const;
            };

            struct DescriptorLayoutKey
            {
                std::vector<DescriptorBindingKey>   bindings;

                bool                                operator==(const DescriptorLayoutKey& other) const = default;
            };

            struct DescriptorLayoutKeyHash
            {
                public:
                    size_t                          operator()(const DescriptorLayoutKey& key) const;
            };

            std::unordered_map<DescriptorLayoutKey, VkDescriptorSetLayout, DescriptorLayoutKeyHash> cache_;

            DescriptorLayoutRegistry() = default;
            ~DescriptorLayoutRegistry();

            DescriptorLayoutKey                     MakeKey(std::span<const VkDescriptorSetLayoutBinding> inputBinding);

        public:
            static DescriptorLayoutRegistry&        Instance();

            void                                    Init();
            void                                    Cleanup();

            VkDescriptorSetLayout                   GetOrCreate(std::span<const VkDescriptorSetLayoutBinding> binding);   

    };

}






