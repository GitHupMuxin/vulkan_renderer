#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/resource/aabb.h"

namespace engine::resource
{
    constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

    struct VertexData
    {
        glm::vec3 position{};
        glm::vec3 normal{};
        glm::vec2 uv0{};
        glm::vec4 color{1.0f};
    };

    struct PrimitiveData
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t materialIndex = InvalidIndex;
    };

    struct MeshData
    {
        std::vector<VertexData> vertices;
        std::vector<uint32_t> vertexIndices;
        std::vector<PrimitiveData> primitives;

        // 包围盒处于 Mesh 局部空间，由全部顶点位置计算；修改顶点后需重新计算。
        AABB bounds;
        void RecalculateBounds();
    };

    struct NodeData
    {
        uint32_t meshIndex = InvalidIndex;

        std::string name;

        glm::mat4 localTransform{1.0f};

        uint32_t parentIndex = InvalidIndex;
        std::vector<uint32_t> childIndices;
    };


    // 纹理在资产里的描述，不含 GPU 资源：像素由加载阶段按来源取回或直接使用。
    // 同一份纹理可以被多个材质引用（例如 glTF 把 roughness 和 metallic 打包在同一张图上）。
    struct TextureData
    {
        // 图片的两种存放方式：外部文件按 path 读取，内嵌图片的原始字节已经在这里。
        enum class Source { File, Embedded };
        enum class WrapMode { Repeat, Clamp, MirroredRepeat };
        enum class FilterMode { Nearest, Linear };

        Source              source = Source::File;

        // Source::File：可直接打开的完整路径（已拼上 glTF 文件所在目录）。
        std::string         path;

        // Source::Embedded：图片文件的原始字节（PNG/JPG 等编码形式，尚未解码）。
        std::vector<uint8_t> bytes;
        std::string         mimeType;

        // 是否按 sRGB 解码；由引用它的表面量决定（baseColor/emissive 为真，其余为假）。
        bool isSrgb = false;

        WrapMode    wrapU = WrapMode::Repeat;
        WrapMode    wrapV = WrapMode::Repeat;
        FilterMode  minFilter = FilterMode::Linear;
        FilterMode  magFilter = FilterMode::Linear;
        bool        generateMipmaps = true;
    };

    // 从纹理的哪些通道读取，按位组合。打包存储（如 glTF 的 roughness 在 G、metallic 在 B）
    // 靠它把同一张纹理的不同分量分给不同表面量。
    enum TextureChannelBits : uint8_t
    {
        // 未指定通道。默认取这个值而非某个"看起来合理"的组合，避免忘记设置时静默读错分量；
        // 引用纹理时该值非法，由 ValidateModelData 拒绝。
        TextureChannelNone = 0,

        TextureChannelR = 1u << 0,
        TextureChannelG = 1u << 1,
        TextureChannelB = 1u << 2,
        TextureChannelA = 1u << 3,

        TextureChannelRgb  = TextureChannelR | TextureChannelG | TextureChannelB,
        TextureChannelRgba = TextureChannelRgb | TextureChannelA,
    };

    // 表面量：常量因子 + 可选纹理采样。有纹理时两者相乘，无纹理时因子即最终值。
    struct SurfaceQuantity
    {
        glm::vec4   factor{1.0f};
        // 指向 ModelData::textures；InvalidIndex 表示无纹理，直接用 factor。
        uint32_t    textureIndex = InvalidIndex;
        // 从纹理的哪些通道读取；向量量用 TextureChannelRgb/Rgba，标量量必须只选一个通道。
        uint8_t     channels = TextureChannelNone;
    };

    // 材质的表面量语义：校验据此判断通道选择是否合法，也决定渲染时怎么取分量。
    enum class SurfaceQuantityKind
    {
        Vector,     // 读取 RGB/RGBA，factor 的分量按通道对应
        Scalar,     // 只能读取单个通道，factor 只用 R 分量
    };

    // 每个表面量在材质里的槽位；校验与渲染按它索引，避免位置语义散落在多处。
    enum class MaterialSlot
    {
        BaseColor = 0,
        Metallic,
        Roughness,
        Normal,
        Occlusion,
        Emissive,
        Count,
    };

    // 材质按 PBR 的物理量组织，与文件格式无关；格式差异由各自的 loader 转换消解。
    struct MaterialData
    {
        enum class AlphaMode { Opaque, Mask, Blend };

        std::string     name;

        // 六个表面量。occlusion 与 emissive 的 factor 分别只用 R 分量与 RGB 分量。
        SurfaceQuantity baseColor;      // RGB 为 sRGB 基础色，A 为不透明度
        SurfaceQuantity metallic;       // 只用 R 分量
        SurfaceQuantity roughness;      // 只用 R 分量
        SurfaceQuantity normal;         // RGB 为切线空间法线
        SurfaceQuantity occlusion;      // 只用 R 分量
        SurfaceQuantity emissive;       // RGB 为 sRGB 自发光，加到最终颜色上

        AlphaMode       alphaMode = AlphaMode::Opaque;
        float           alphaCutoff = 0.5f;     // 仅 Mask 模式使用
        bool            doubleSided = false;
    };

    struct ModelData
    {
        std::string name;

        std::vector<MeshData> meshes;
        std::vector<NodeData> nodes;
        std::vector<uint32_t> rootNodeIndices;
        std::vector<MaterialData> materials;
        std::vector<TextureData> textures;
    };

    // 只检查资产数据契约，不加载文件或创建 GPU 资源；失败时返回首个错误。
    bool ValidateModelData(const ModelData& model, std::string* error = nullptr);

    // 表面量的维度语义：校验通道选择、渲染取分量都按它判断。
    SurfaceQuantityKind GetSurfaceQuantityKind(MaterialSlot slot);

    // 按槽位取表面量，const 与非 const 两个版本；下标越界返回 nullptr。
    const SurfaceQuantity* GetSurfaceQuantity(const MaterialData& material, MaterialSlot slot);
    SurfaceQuantity* GetSurfaceQuantity(MaterialData& material, MaterialSlot slot);
}
