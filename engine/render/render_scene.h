#pragma once
#include <vector>

#include "engine/resource/resource_manager.h"


namespace engine::render
{

    enum class RenderQueue { Opaque, Masked, Transparent};

    enum class PipelineVariant { Pbr, Unlit, DoubleSided, AlphaBlending };    

    enum class LightType { Directional, Point, Spot };

    struct LightRenderData
    {
        LightType                           type = LightType::Directional;
        glm::vec3                           color{ 1.0f };
        glm::vec3                           direction{ 0.0f, -1.0f, 0.0f };  
        glm::vec3                           position{ 0.0f };                
        float                               range = 0.0f;                        
        float                               innerConeAngle = 0.0f;
        float                               outerConeAngle = 0.0f;
    };

    struct CameraRenderData
    {
        glm::mat4                           view{ 1.0f };
        glm::mat4                           projection{ 1.0f };
        glm::vec3                           position{ 0.0f };
    };

    struct EnvironmentRenderData
    {
        float                               exposure = 4.5f;
        float                               gamma = 2.2f;
        float                               scaleIBLAmbient = 1.0f;
        // TODO: exposure/gamma/scaleIBLAmbient 严格说是"显示/色调参数"，边界模糊，
        // 暂留 Environment（通过 uboParams 上传，改 shader 布局风险大），后续再拆
    };

    // 全局渲染/显示设置：由 UI 驱动，控制 shader 输出形态，不依赖具体资源。
    // CPU 侧独立结构；shader 侧暂通过 uboParams 上传（字段顺序与 UBOParams 对齐）。
    struct RenderSettings
    {
        float                               debugViewInputs = 0.0f;
        float                               debugViewEquation = 0.0f;
        float                               debugBsdfType = 0.0f;
    };

    struct RenderItem
    {
        resource::ModelHandle               modelHandle;

        uint32_t                            firstIndex = 0;
        uint32_t                            indexCount = 0;
        uint32_t                            vertexCount = 0;
        bool                                hasIndices = false;

        // 两个编号语义不同，不能合并：
        // meshIndex 指向模型内的几何（VBO/IBO 对），按共享的几何编号，多个节点可相同；
        // instanceSlot 指向逐帧实例矩阵 SSBO 的槽位，按「带 mesh 的节点实例」编号。
        uint32_t                            meshIndex = 0;
        uint32_t                            instanceSlot = 0;
        uint32_t                            materialIndex = 0;

        glm::mat4                           worldTransform{ 1.0f };

        RenderQueue                         renderQueue = RenderQueue::Opaque;

        PipelineVariant                     pipeline = PipelineVariant::Pbr;        

        VkDescriptorSet                     materialDescriptorSet = VK_NULL_HANDLE;
    };
    
    struct RenderScene
    {
        CameraRenderData                    camera;
        EnvironmentRenderData               environment;
        LightRenderData                     light;
        RenderSettings                      settings;

        // 当前 shader 的 UBO model 矩阵（场景主物体的世界变换）
        glm::mat4                           modelMatrix{ 1.0f };

        // 场景引用的全部模型（显式注册，与 RenderItem 分开）：
        // descriptor set 分配、就绪轮询等"资源存在性"遍历用；
        // 实际绘制按 opaque/masked/transparentItems 队列。
        std::vector<resource::ModelHandle>  modelHandles;

        std::vector<RenderItem>             opaqueItems;
        std::vector<RenderItem>             maskedItems;
        std::vector<RenderItem>             transparentItems;
    };




}

