#pragma once
#include <string>
#include <vector>

namespace engine::scene
{
    // 场景描述：纯数据，声明"这个场景需要哪些资源"
    // Scene 按描述加载资源，而不是默认获取 ResourceManager 里的全部资源
    struct SceneDescription
    {
        // 场景模型（相对 data/ 的路径），每个路径加载为一个 SceneObject
        std::vector<std::string> modelPaths;

        // 环境贴图（.ktx），为空则场景不使用 IBL
        std::string environmentPath;
    };
}
