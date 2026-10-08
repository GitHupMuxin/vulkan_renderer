#include "engine/resource/gltf_asset_loader.h"

#include <functional>
#include <map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
// 与 texture.h 中 tinygltf 的编译选项一致，本项目不链接图片写出实现。
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include "engine/utils/log.h"


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

        // glTF 的四元数按 (x, y, z, w) 存储，glm::quat 构造按 (w, x, y, z)。
        glm::mat4 BuildLocalTransform(const tinygltf::Node& node)
        {
            // 规范：matrix 存在时忽略 TRS；否则按 T × R × S 组合。
            if (node.matrix.size() == 16)
            {
                glm::mat4 result(1.0f);
                const double* m = node.matrix.data();
                for (uint32_t column = 0; column < 4; ++column)
                {
                    for (uint32_t row = 0; row < 4; ++row)
                    {
                        result[column][row] = static_cast<float>(m[column * 4 + row]);
                    }
                }
                return result;
            }

            const glm::vec3 translation = node.translation.size() == 3 ? glm::vec3(node.translation[0], node.translation[1], node.translation[2]) : glm::vec3(0.0f);

            const glm::quat rotation = node.rotation.size() == 4
                ? glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                            static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]))
                : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            const glm::vec3 scale = node.scale.size() == 3 ? glm::vec3(node.scale[0], node.scale[1], node.scale[2]) : glm::vec3(1.0f);

            return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
        }

        // 一个 float 属性在内存中的视图。glTF 把每个属性放在 buffer 的独立区段，
        // 元素之间由 bufferView.byteStride 决定间距（0 表示紧密排列）。
        struct FloatAttributeView
        {
            const float* data = nullptr;    // 第一个元素的第一个分量
            size_t count = 0;               // 元素个数
            int strideInFloats = 0;         // 元素间距（以 float 为单位）
            int componentCount = 0;         // VEC2/VEC3/VEC4 的分量数
        };

        // 属性缺失或分量类型不是 float 时返回 false；两种情况的处理方法不同，
        // 调用方需要自己判断该属性是否必需。
        bool ReadFloatAttribute(const tinygltf::Model& gltf, const tinygltf::Primitive& primitive, const char* name, FloatAttributeView* out)
        {
            const auto found = primitive.attributes.find(name);
            if (found == primitive.attributes.end())
            {
                return false;
            }

            const tinygltf::Accessor& accessor = gltf.accessors[found->second];
            if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
            {
                return false;
            }

            const tinygltf::BufferView& bufferView = gltf.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = gltf.buffers[bufferView.buffer];

            // 元素地址 = buffer 起点 + bufferView 偏移 + accessor 偏移 + i × stride
            const unsigned char* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

            const int componentCount = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
            if (componentCount <= 0)
            {
                return false;
            }
            const int elementBytes = static_cast<int>(sizeof(float)) * componentCount;
            const int stride = bufferView.byteStride != 0 ? static_cast<int>(bufferView.byteStride) : elementBytes;

            out->data = reinterpret_cast<const float*>(base);
            out->count = accessor.count;
            out->strideInFloats = stride / static_cast<int>(sizeof(float));
            out->componentCount = componentCount;
            return true;
        }

        // 读出 primitive 的索引并统一成 uint32；无索引时按顶点顺序生成。
        // 返回 false 表示索引分量类型不支持。
        bool ReadIndices(const tinygltf::Model& gltf, const tinygltf::Primitive& primitive, size_t vertexCount, std::vector<uint32_t>* out)
        {
            out->clear();

            // 没有索引的 primitive 等价于顺序索引，补齐后绘制路径可以统一。
            if (primitive.indices < 0)
            {
                out->resize(vertexCount);
                for (uint32_t i = 0; i < static_cast<uint32_t>(vertexCount); ++i)
                {
                    (*out)[i] = i;
                }
                return true;
            }

            const tinygltf::Accessor& accessor = gltf.accessors[primitive.indices];
            const tinygltf::BufferView& bufferView = gltf.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = gltf.buffers[bufferView.buffer];
            const unsigned char* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

            // 规范禁止索引 bufferView 带 byteStride，元素紧密排列。
            out->reserve(accessor.count);
            switch (accessor.componentType)
            {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                {
                    const unsigned char* values = reinterpret_cast<const unsigned char*>(base);
                    for (size_t i = 0; i < accessor.count; ++i)
                    {
                        out->push_back(values[i]);
                    }
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    const uint16_t* values = reinterpret_cast<const uint16_t*>(base);
                    for (size_t i = 0; i < accessor.count; ++i)
                    {
                        out->push_back(values[i]);
                    }
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                {
                    const uint32_t* values = reinterpret_cast<const uint32_t*>(base);
                    for (size_t i = 0; i < accessor.count; ++i)
                    {
                        out->push_back(values[i]);
                    }
                    break;
                }
                default:
                    return false;
            }
            return true;
        }

        // 把一个 glTF mesh 的全部 primitive 物化进一份 MeshData：顶点与索引都落成
        // mesh 局部数组，primitive 只记录自己在这两个数组里的区间。
        bool ConvertMesh(const tinygltf::Model& gltf, uint32_t gltfMeshIndex, MeshData* mesh, std::string* error)
        {
            const tinygltf::Mesh& source = gltf.meshes[gltfMeshIndex];

            for (size_t primitiveIndex = 0; primitiveIndex < source.primitives.size(); ++primitiveIndex)
            {
                const tinygltf::Primitive& primitive = source.primitives[primitiveIndex];
                const std::string where = "mesh " + std::to_string(gltfMeshIndex) + " primitive " + std::to_string(primitiveIndex);

                FloatAttributeView position;
                if (!ReadFloatAttribute(gltf, primitive, "POSITION", &position))
                {
                    return Fail(error, "GltfAssetLoader: " + where + " has no usable POSITION attribute");
                }

                FloatAttributeView normal;
                FloatAttributeView uv0;
                FloatAttributeView color;
                const bool hasNormal = ReadFloatAttribute(gltf, primitive, "NORMAL", &normal);
                const bool hasUv0 = ReadFloatAttribute(gltf, primitive, "TEXCOORD_0", &uv0);
                const bool hasColor = ReadFloatAttribute(gltf, primitive, "COLOR_0", &color);

                // 拼接基准：本 primitive 的顶点与索引从这里往后追加。
                const uint32_t vertexBase = static_cast<uint32_t>(mesh->vertices.size());
                const uint32_t indexBase = static_cast<uint32_t>(mesh->vertexIndices.size());

                mesh->vertices.reserve(mesh->vertices.size() + position.count);
                for (size_t i = 0; i < position.count; ++i)
                {
                    VertexData vertex;

                    const float* p = position.data + i * static_cast<size_t>(position.strideInFloats);
                    vertex.position = glm::vec3(p[0], p[1], p[2]);

                    // 缺失的属性沿用 VertexData 的默认值：法线为零向量、UV 为零、颜色为白。
                    if (hasNormal)
                    {
                        const float* n = normal.data + i * static_cast<size_t>(normal.strideInFloats);
                        vertex.normal = glm::vec3(n[0], n[1], n[2]);
                    }
                    if (hasUv0)
                    {
                        const float* uv = uv0.data + i * static_cast<size_t>(uv0.strideInFloats);
                        vertex.uv0 = glm::vec2(uv[0], uv[1]);
                    }
                    if (hasColor)
                    {
                        const float* c = color.data + i * static_cast<size_t>(color.strideInFloats);
                        vertex.color = color.componentCount >= 4 ? glm::vec4(c[0], c[1], c[2], c[3]) : glm::vec4(c[0], c[1], c[2], 1.0f);
                    }

                    mesh->vertices.push_back(vertex);
                }

                std::vector<uint32_t> primitiveIndices;
                if (!ReadIndices(gltf, primitive, position.count, &primitiveIndices))
                {
                    return Fail(error, "GltfAssetLoader: " + where + " uses an unsupported index component type");
                }

                // 索引加上本 primitive 的顶点基准，从全局偏移改为 mesh 局部下标。
                mesh->vertexIndices.reserve(mesh->vertexIndices.size() + primitiveIndices.size());
                for (uint32_t index : primitiveIndices)
                {
                    mesh->vertexIndices.push_back(vertexBase + index);
                }

                PrimitiveData data;
                data.firstIndex = indexBase;
                data.indexCount = static_cast<uint32_t>(primitiveIndices.size());
                data.materialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material) : InvalidIndex;
                mesh->primitives.push_back(data);
            }

            mesh->RecalculateBounds();
            return true;
        }

        // glTF 的图片有三种来源，其中两种把字节直接交给回调（data URI、bufferView），
        // 回调返回后字节就没了；外部文件则把路径留在 image->uri 上。这里只捕获内嵌数据的字节。
        struct CapturedImages
        {
            std::vector<std::vector<uint8_t>> bytes;        // 按 image 下标
            std::vector<std::string>          mimeTypes;
        };

        bool CaptureImageBytes(tinygltf::Image* image, const int imageIndex, std::string*, std::string*,
                               int, int, const unsigned char* bytes, int size, void* userData)
        {
            auto* captured = static_cast<CapturedImages*>(userData);

            // image->uri 为空表示这张图来自 data URI 或 bufferView，需要自己保留字节。
            if (image->uri.empty() && bytes != nullptr && size > 0)
            {
                const size_t index = static_cast<size_t>(imageIndex);
                if (captured->bytes.size() <= index)
                {
                    captured->bytes.resize(index + 1);
                    captured->mimeTypes.resize(index + 1);
                }
                captured->bytes[index].assign(bytes, bytes + size);
                captured->mimeTypes[index] = image->mimeType;
            }
            return true;
        }

        TextureData::WrapMode ConvertWrapMode(int32_t wrapMode)
        {
            switch (wrapMode)
            {
                case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:    return TextureData::WrapMode::Clamp;
                case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:  return TextureData::WrapMode::MirroredRepeat;
                default:                                     return TextureData::WrapMode::Repeat;
            }
        }

        TextureData::FilterMode ConvertFilterMode(int32_t filterMode)
        {
            return filterMode == TINYGLTF_TEXTURE_FILTER_NEAREST
                ? TextureData::FilterMode::Nearest
                : TextureData::FilterMode::Linear;
        }

        // sRGB 由引用这张图的表面量决定，所以同一张 glTF 纹理可能对应多条 TextureData；
        // 去重键因此同时包含纹理下标与色彩空间。
        struct TextureKey
        {
            uint32_t gltfTextureIndex = 0;
            bool     isSrgb = false;

            bool operator<(const TextureKey& other) const
            {
                return this->gltfTextureIndex < other.gltfTextureIndex ||
                    (this->gltfTextureIndex == other.gltfTextureIndex && this->isSrgb < other.isSrgb);
            }
        };

        // 纹理转换期间不变的上下文：数据源、图片字节、输出容器与去重缓存。
        // 打包成一个结构，避免每个辅助函数都要重复接收同一组参数。
        struct MaterialContext
        {
            const tinygltf::Model*          gltf = nullptr;
            const CapturedImages*           captured = nullptr;
            std::string                     assetDir;       // 外部图片的目录前缀
            ModelData*                      model = nullptr;
            std::map<TextureKey, uint32_t>* textureCache = nullptr;
        };

        // 按需建立 TextureData 并缓存；返回 ModelData::textures 的下标。
        // 纹理缺少 source（例如依赖未支持扩展提供图片）时返回 InvalidIndex。
        // 失败结果不写入缓存：同一个坏纹理被多处引用时会重复报错，但实现更简单。
        uint32_t GetOrCreateTexture(const MaterialContext& context, uint32_t gltfTextureIndex, bool isSrgb)
        {
            const TextureKey key{gltfTextureIndex, isSrgb};
            const auto found = context.textureCache->find(key);
            if (found != context.textureCache->end())
            {
                return found->second;
            }

            const tinygltf::Texture& source = context.gltf->textures[gltfTextureIndex];

            if (source.source < 0 || static_cast<size_t>(source.source) >= context.gltf->images.size())
            {
                // 纹理引用不到图片：材质会退化成只用因子，表面外观与作者意图不符。
                LOG_ERROR("GltfAssetLoader: texture " << gltfTextureIndex << " has no usable image source");
                return InvalidIndex;
            }

            TextureData texture;
            texture.isSrgb = isSrgb;

            if (source.sampler >= 0 && static_cast<size_t>(source.sampler) < context.gltf->samplers.size())
            {
                const tinygltf::Sampler& sampler = context.gltf->samplers[source.sampler];
                texture.wrapU = ConvertWrapMode(sampler.wrapS);
                texture.wrapV = ConvertWrapMode(sampler.wrapT);
                texture.minFilter = ConvertFilterMode(sampler.minFilter);
                texture.magFilter = ConvertFilterMode(sampler.magFilter);
                // 只有带 mipmap 的 minFilter 才需要生成 mip 链。
                texture.generateMipmaps = sampler.minFilter >= TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST;
            }

            const tinygltf::Image& image = context.gltf->images[source.source];
            if (!image.uri.empty())
            {
                texture.source = TextureData::Source::File;
                // 记成可直接打开的路径，调用方不必再知道 glTF 文件的位置。
                texture.path = context.assetDir + image.uri;
            }
            else
            {
                if (static_cast<size_t>(source.source) >= context.captured->bytes.size())
                {
                    // 内嵌图片未被捕获：通常是回调未执行或 image 下标与捕获数组不对齐。
                    LOG_ERROR("GltfAssetLoader: embedded image " << source.source << " was not captured");
                    return InvalidIndex;
                }
                texture.source = TextureData::Source::Embedded;
                texture.bytes = context.captured->bytes[source.source];
                texture.mimeType = context.captured->mimeTypes[source.source];
            }

            const uint32_t index = static_cast<uint32_t>(context.model->textures.size());
            context.model->textures.push_back(std::move(texture));
            context.textureCache->emplace(key, index);
            return index;
        }

        // 把一个表面量的纹理引用填好；无纹理时保持 textureIndex 为 InvalidIndex。
        void AssignTexture(const MaterialContext& context, int gltfTextureIndex, uint32_t channels, bool isSrgb, SurfaceQuantity* quantity)
        {
            if (gltfTextureIndex < 0 || static_cast<size_t>(gltfTextureIndex) >= context.gltf->textures.size())
            {
                quantity->textureIndex = InvalidIndex;
                quantity->channels = TextureChannelNone;
                return;
            }

            quantity->textureIndex = GetOrCreateTexture(context, static_cast<uint32_t>(gltfTextureIndex), isSrgb);
            // 拿不到纹理时同步清掉通道，保持数据自洽（校验要求指定纹理就必须选通道）。
            quantity->channels = quantity->textureIndex == InvalidIndex ? TextureChannelNone : channels;
        }

        bool ConvertMaterial(const MaterialContext& context, uint32_t gltfMaterialIndex, MaterialData* material)
        {
            const tinygltf::Material& source = context.gltf->materials[gltfMaterialIndex];
            const tinygltf::PbrMetallicRoughness& pbr = source.pbrMetallicRoughness;

            material->name = source.name;

            // ── baseColor：RGB 为 sRGB 基础色，A 为不透明度
            if (pbr.baseColorFactor.size() == 4)
            {
                material->baseColor.factor = glm::vec4(
                    static_cast<float>(pbr.baseColorFactor[0]), static_cast<float>(pbr.baseColorFactor[1]),
                    static_cast<float>(pbr.baseColorFactor[2]), static_cast<float>(pbr.baseColorFactor[3]));
            }
            AssignTexture(context, pbr.baseColorTexture.index, TextureChannelRgba, true, &material->baseColor);

            // ── metallic / roughness：glTF 允许打包在同一张图（G=roughness，B=metallic）。
            //    两者引用同一个 glTF 纹理，第二次调用由缓存命中，因此共享同一个 ModelData 纹理下标。
            material->metallic.factor = glm::vec4(static_cast<float>(pbr.metallicFactor), 0.0f, 0.0f, 0.0f);
            material->roughness.factor = glm::vec4(static_cast<float>(pbr.roughnessFactor), 0.0f, 0.0f, 0.0f);
            AssignTexture(context, pbr.metallicRoughnessTexture.index, TextureChannelB, false, &material->metallic);
            AssignTexture(context, pbr.metallicRoughnessTexture.index, TextureChannelG, false, &material->roughness);

            // ── normal：切线空间法线，线性空间
            AssignTexture(context, source.normalTexture.index, TextureChannelRgb, false, &material->normal);

            // ── occlusion：只读 R 通道，线性空间
            AssignTexture(context, source.occlusionTexture.index, TextureChannelR, false, &material->occlusion);

            // ── emissive：默认必须显式清零，否则会沿用 SurfaceQuantity 的默认白色而整体自发光
            material->emissive.factor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            if (source.emissiveFactor.size() == 3)
            {
                material->emissive.factor = glm::vec4(
                    static_cast<float>(source.emissiveFactor[0]), static_cast<float>(source.emissiveFactor[1]),
                    static_cast<float>(source.emissiveFactor[2]), 1.0f);
            }
            AssignTexture(context, source.emissiveTexture.index, TextureChannelRgb, true, &material->emissive);

            // ── 状态：alphaMode 与 doubleSided 决定流水线，不进入表面量
            if (source.alphaMode == "MASK")
            {
                material->alphaMode = MaterialData::AlphaMode::Mask;
            }
            else if (source.alphaMode == "BLEND")
            {
                material->alphaMode = MaterialData::AlphaMode::Blend;
            }
            else
            {
                material->alphaMode = MaterialData::AlphaMode::Opaque;
            }
            material->alphaCutoff = static_cast<float>(source.alphaCutoff);
            material->doubleSided = source.doubleSided;

            return true;
        }
    }

    bool GltfAssetLoader::Load(const std::string& path, ModelData* out, std::string* error)
    {
        if (out == nullptr)
        {
            return Fail(error, "GltfAssetLoader: output ModelData is null");
        }
        if (error != nullptr)
        {
            error->clear();
        }

        // ① 读文件：.glb 走二进制，其余按 ASCII glTF。
        tinygltf::Model gltf;
        tinygltf::TinyGLTF context;
        std::string loadError;
        std::string loadWarning;

        // 本转换不解码图片：只保留纹理描述，像素由加载 GPU 资源时按描述取回。
        // 但内嵌图片（data URI / bufferView）的字节只在回调期间存在，必须在这里捕获。
        CapturedImages capturedImages;
        context.SetImageLoader(CaptureImageBytes, &capturedImages);

        const size_t extensionPos = path.rfind('.');
        const bool binary = extensionPos != std::string::npos && path.compare(extensionPos + 1, std::string::npos, "glb") == 0;
        const bool loaded = binary ? context.LoadBinaryFromFile(&gltf, &loadError, &loadWarning, path.c_str()) : context.LoadASCIIFromFile(&gltf, &loadError, &loadWarning, path.c_str());

        if (!loaded)
        {
            return Fail(error, "GltfAssetLoader: failed to load '" + path + "': " + loadError);
        }
        if (!loadWarning.empty())
        {
            LOG_WARN("GltfAssetLoader: " << loadWarning);
        }

        // glTF 里外部图片的 uri 相对文件所在目录；先取出目录，后面拼成可直接打开的路径。
        const size_t slashPos = path.find_last_of("/\\");
        const std::string assetDir = slashPos != std::string::npos ? path.substr(0, slashPos + 1) : std::string();

        // ② 选默认场景：缺失 defaultScene 时退回第一个场景。
        if (gltf.scenes.empty())
        {
            return Fail(error, "GltfAssetLoader: '" + path + "' contains no scene");
        }
        const int sceneIndex = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        if (static_cast<size_t>(sceneIndex) >= gltf.scenes.size())
        {
            return Fail(error, "GltfAssetLoader: default scene index is out of range");
        }

        const tinygltf::Scene& scene = gltf.scenes[sceneIndex];

        // 组装 ModelData。节点是 1:1 直接构造，下标由 push 位置决定；
        // mesh 是 N:1（多节点可引用同一 mesh），需要一张表来去重。
        ModelData result;
        result.name = !scene.name.empty() ? scene.name : path;

        // glTF mesh 下标 → ModelData mesh 下标。只包含被选中场景引用的 mesh，
        // 其余保持 InvalidIndex；加载期用它做两件事：判断是否已建过（去重）、定位写入目标。
        std::vector<uint32_t> gltfMeshToModelMesh(gltf.meshes.size(), InvalidIndex);

        // 深度优先遍历场景树；返回新节点的 ModelData 下标。
        std::function<uint32_t(uint32_t, uint32_t)> visitNode = [&](uint32_t gltfNodeIndex, uint32_t parentIndex) -> uint32_t
        {
            const tinygltf::Node& source = gltf.nodes[gltfNodeIndex];

            const uint32_t nodeIndex = static_cast<uint32_t>(result.nodes.size());

            NodeData node;
            node.name = source.name;
            node.localTransform = BuildLocalTransform(source);
            node.parentIndex = parentIndex;

            // mesh 去重：首次遇到才建 MeshData，后续节点复用同一份几何。
            if (source.mesh >= 0)
            {
                const uint32_t gltfMeshIndex = static_cast<uint32_t>(source.mesh);
                if (gltfMeshToModelMesh[gltfMeshIndex] == InvalidIndex)
                {
                    gltfMeshToModelMesh[gltfMeshIndex] = static_cast<uint32_t>(result.meshes.size());
                    result.meshes.emplace_back();   // 几何数据在第 ⑤ 步填入
                }
                node.meshIndex = gltfMeshToModelMesh[gltfMeshIndex];
            }

            result.nodes.push_back(std::move(node));

            for (int child : source.children)
            {
                if (child < 0 || static_cast<size_t>(child) >= gltf.nodes.size())
                {
                    continue;
                }
                const uint32_t childIndex = visitNode(static_cast<uint32_t>(child), nodeIndex);
                result.nodes[nodeIndex].childIndices.push_back(childIndex);
            }

            return nodeIndex;
        };

        // 遍历节点。
        result.rootNodeIndices.reserve(scene.nodes.size());
        for (int rootNode : scene.nodes)
        {
            if (rootNode < 0 || static_cast<size_t>(rootNode) >= gltf.nodes.size())
            {
                return Fail(error, "GltfAssetLoader: scene references an out-of-range node");
            }
            result.rootNodeIndices.push_back(visitNode(static_cast<uint32_t>(rootNode), InvalidIndex));
        }

        // ⑤ 转换几何：沿 gltfMeshToModelMesh 遍历，只处理被场景引用到的 mesh，
        //    键是 glTF mesh 下标（数据来源）、值是 ModelData mesh 下标（写入目标）。
        for (size_t gltfMeshIndex = 0; gltfMeshIndex < gltfMeshToModelMesh.size(); ++gltfMeshIndex)
        {
            const uint32_t modelMeshIndex = gltfMeshToModelMesh[gltfMeshIndex];
            if (modelMeshIndex == InvalidIndex)
            {
                continue;
            }
            if (!ConvertMesh(gltf, static_cast<uint32_t>(gltfMeshIndex), &result.meshes[modelMeshIndex], error))
            {
                return false;
            }
        }

        // ⑥ 转换材质与纹理。primitive 里的 materialIndex 直接沿用 glTF 下标，
        //    因此材质必须按原下标顺序一一建出，中间不能跳过。
        std::map<TextureKey, uint32_t> textureCache;
        MaterialContext materialContext;
        materialContext.gltf = &gltf;
        materialContext.captured = &capturedImages;
        materialContext.assetDir = assetDir;
        materialContext.model = &result;
        materialContext.textureCache = &textureCache;

        result.materials.resize(gltf.materials.size());
        for (size_t materialIndex = 0; materialIndex < gltf.materials.size(); ++materialIndex)
        {
            ConvertMaterial(materialContext, static_cast<uint32_t>(materialIndex), &result.materials[materialIndex]);
        }

        *out = std::move(result);
        return true;
    }
}
