#include "engine/render/pass_resource.h"

#include "engine/render/render_scene.h"

#include <array>

namespace engine::render
{
    namespace
    {
        constexpr std::array kResourceDescriptions = {
            RenderResourceDescription{
                RenderResourceId::MainColor,
                "MainColor",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::PerSwapchainImage
                }
            },
            RenderResourceDescription{
                RenderResourceId::MainDepth,
                "MainDepth",
                RenderResourceLifetime::Persistent,
                RenderImageDescription{
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::PerSwapchainImage
                }
            },
            RenderResourceDescription{
                RenderResourceId::MainCamera,
                "MainCamera",
                RenderResourceLifetime::Frame,
                RenderBufferDescription{
                    .size_ = sizeof(UBOMatricesUpload)
                }
            },
            RenderResourceDescription{
                RenderResourceId::SceneParam,
                "SceneParam",
                RenderResourceLifetime::Frame,
                RenderBufferDescription{
                    .size_ = sizeof(ParamsUpload)
                }
            },
            RenderResourceDescription{
                RenderResourceId::EnvironmentCube,
                "EnvironmentCube",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .arrayLayers_ = 6,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_CUBE,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::IrradianceMap,
                "IrradianceMap",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R32G32B32A32_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .arrayLayers_ = 6,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_CUBE,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::PrefilteredMap,
                "PrefilteredMap",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .arrayLayers_ = 6,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_CUBE,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::BrdfLut,
                "BrdfLut",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R16G16_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::EuLut,
                "EuLut",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R16G16_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::EavgLut,
                "EavgLut",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R16G16_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::MaterialTextures,
                "MaterialTextures",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::Single
                }
            },
            RenderResourceDescription{
                RenderResourceId::MaterialBuffer,
                "MaterialBuffer",
                RenderResourceLifetime::External,
                RenderBufferDescription{}
            },
            RenderResourceDescription{
                RenderResourceId::MeshDataBuffer,
                "MeshDataBuffer",
                RenderResourceLifetime::External,
                RenderBufferDescription{}
            },
            RenderResourceDescription{
                RenderResourceId::SceneColorHdr,
                "SceneColorHdr",
                RenderResourceLifetime::Persistent,
                RenderImageDescription{
                    .format_ = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::PerSwapchainImage
                }
            },
            RenderResourceDescription{
                RenderResourceId::BackBuffer,
                "BackBuffer",
                RenderResourceLifetime::External,
                RenderImageDescription{
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::PerSwapchainImage
                }
            },
            RenderResourceDescription{
                RenderResourceId::MainColorMsaa,
                "MainColorMsaa",
                RenderResourceLifetime::Persistent,
                RenderImageDescription{
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::PerSwapchainImage
                }
            },
            RenderResourceDescription{
                RenderResourceId::MainDepthSingleSample,
                "MainDepthSingleSample",
                RenderResourceLifetime::Persistent,
                RenderImageDescription{
                    .samples_ = VK_SAMPLE_COUNT_1_BIT,
                    .mipLevels_ = 1,
                    .arrayLayers_ = 1,
                    .viewType_ = VK_IMAGE_VIEW_TYPE_2D,
                    .instancePolicy_ = RenderImageInstancePolicy::PerSwapchainImage
                }
            }
        };
    }

    std::span<const RenderResourceDescription> GetRenderResourceDescriptions() noexcept
    {
        return kResourceDescriptions;
    }

    const RenderResourceDescription* FindRenderResourceDescription(RenderResourceId id) noexcept
    {
        for (const auto& description : kResourceDescriptions)
        {
            if (description.id_ == id)
            {
                return &description;
            }
        }

        return nullptr;
    }
}
