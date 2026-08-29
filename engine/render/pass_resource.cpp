#include "engine/render/pass_resource.h"

#include <array>

namespace engine::render
{
    namespace
    {
        constexpr std::array kResourceDescriptions = {
            RenderResourceDescription{
                RenderResourceId::MainColor,
                "MainColor",
                RenderResourceType::Image,
                RenderResourceLifetime::External
            },
            RenderResourceDescription{
                RenderResourceId::MainDepth,
                "MainDepth",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::CameraMatrices,
                "CameraMatrices",
                RenderResourceType::Buffer,
                RenderResourceLifetime::Frame
            },
            RenderResourceDescription{
                RenderResourceId::RenderParams,
                "RenderParams",
                RenderResourceType::Buffer,
                RenderResourceLifetime::Frame
            },
            RenderResourceDescription{
                RenderResourceId::EnvironmentCube,
                "EnvironmentCube",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::IrradianceMap,
                "IrradianceMap",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::PrefilteredMap,
                "PrefilteredMap",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::BrdfLut,
                "BrdfLut",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::EuLut,
                "EuLut",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::EavgLut,
                "EavgLut",
                RenderResourceType::Image,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::MaterialTextures,
                "MaterialTextures",
                RenderResourceType::ImageCollection,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::MaterialBuffer,
                "MaterialBuffer",
                RenderResourceType::BufferCollection,
                RenderResourceLifetime::Persistent
            },
            RenderResourceDescription{
                RenderResourceId::MeshDataBuffer,
                "MeshDataBuffer",
                RenderResourceType::BufferCollection,
                RenderResourceLifetime::Frame
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
