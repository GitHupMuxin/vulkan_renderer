#pragma once

#include <string>

#include "engine/resource/model_data.h"


namespace engine::resource
{
    // glTF / glb → ModelData。
    // 按 mesh 读取 accessor：每个 MeshData 的顶点与索引都是 mesh 局部的，
    // 不沿用 GLTFModel 的"全模型合并 VBO"形态，这样多节点才能共享同一份几何。
    class GltfAssetLoader
    {
        public:
            // 解析失败时返回 false 并写入首个错误原因；成功时完全覆盖 out。
            static bool Load(const std::string& path, ModelData* out, std::string* error = nullptr);
    };
}
