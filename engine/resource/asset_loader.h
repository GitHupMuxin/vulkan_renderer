#pragma once

#include <string>

#include "engine/resource/model_data.h"


namespace engine::resource
{
    // 资产加载入口：把文件解析成与文件格式、Vulkan 都无关的 CPU 数据。
    // 只产出 ModelData，不创建 GPU 资源；GPU 化由 Model 负责。
    // 按扩展名分派到具体解析器，调用方不需要知道文件格式。
    class AssetLoader
    {
        public:
            // 按扩展名选择解析器（.gltf/.glb 走 glTF；.fbx 待接入）。
            // 成功时完全覆盖 out；失败时返回 false 并写入首个错误原因。
            static bool LoadModelData(const std::string& path, ModelData* out, std::string* error = nullptr);
    };
}
