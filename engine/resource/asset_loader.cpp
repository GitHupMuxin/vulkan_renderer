#include "engine/resource/asset_loader.h"

#include <algorithm>
#include <cctype>

#include "engine/resource/gltf_asset_loader.h"


namespace engine::resource
{
    namespace
    {
        bool Fail(std::string* error, const std::string& message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return false;
        }

        // 取小写扩展名（不含点）；无扩展名时返回空串。
        std::string GetExtension(const std::string& path)
        {
            const size_t dot = path.rfind('.');
            if (dot == std::string::npos)
            {
                return std::string();
            }
            std::string extension = path.substr(dot + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }
    }

    bool AssetLoader::LoadModelData(const std::string& path, ModelData* out, std::string* error)
    {
        const std::string extension = GetExtension(path);

        if (extension == "gltf" || extension == "glb")
        {
            return GltfAssetLoader::Load(path, out, error);
        }
        if (extension == "fbx")
        {
            // ufbx 尚未接入，分派位置留在这里，接入时只补这一支。
            return Fail(error, "AssetLoader: FBX import is not implemented yet: " + path);
        }

        return Fail(error, "AssetLoader: unsupported model format '" + extension + "': " + path);
    }
}
