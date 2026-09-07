#pragma once

#include <vulkan/vulkan.h>

namespace engine::render
{
    class PipelineCache final
    {
        private:
            VkPipelineCache                         handle_{ VK_NULL_HANDLE };

            PipelineCache() = default;
            void                                    Save() const;

        public:
            static PipelineCache&                   Instance() noexcept;

            PipelineCache(const PipelineCache&) = delete;
            PipelineCache& operator=(const PipelineCache&) = delete;

            void                                    Init();
            void                                    Destroy();
            VkPipelineCache                         GetHandle() const noexcept;
    };
}
