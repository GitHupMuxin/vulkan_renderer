# Vulkan PBR 项目当前状态

最后更新：2026-09-14

## 快速定位

- 实际仓库：`E:\vulkanProject\Vulkan-glTF-PBR-master\vulkan_pbr`。
- 稳定分支：`main`；本阶段稳定标记：`7C`，具体提交以该 tag 指向为准。
- 上一稳定点：`879af95 feat(7b): add HDR tone mapping render pipeline`，tag `7b`。
- 当前完成阶段：Stage 7C，外部资源绑定与公共 RenderPass 构建、执行链路。
- 下一阶段尚未确定，先与用户讨论范围，不自动继续增加功能。

新任务先核对 Git 与代码，再阅读本文。个人协作原则见 `E:\Agents\AGENTS.md`：默认简短互动，讨论时不修改，只实现已确认需求。验证优先使用命令行、构建和日志，只有用户明确要求时才使用电脑遥控。

## 已完成阶段

| 阶段 | 结果 | 稳定点 |
|---|---|---|
| Stage 0 | Resize、per-image attachment、基础稳定化 | `stage-0` |
| Stage 1 | FrameContext 和帧/图像同步索引分离 | `stage-1` |
| Stage 2 | Debug Utils、对象命名、Label、Timestamp | `stage-2` |
| Stage 3A | 强类型资源 Handle、generation 校验 | `arch-stage-3a-resource-handles` |
| Stage 3B | RenderScene/RenderItem 提取，Render 与 Scene 解耦 | `arch-stage-3b-render-scene` |
| Stage 4A | ResourceState、延迟销毁和 GPU 生命周期 | `arch-stage-4a-resource-lifetime` |
| Stage 4B | 基础异步上传 | `arch-stage-4b-upload-context` |
| Stage 4C | Timeline Semaphore、staging ring、异步就绪状态 | `arch-stage-4c-staging-ring-r2` |
| Stage 5A | Descriptor schema、layout registry、persistent allocator 和所有权回收 | `e4f8cbd` |
| Stage 5B | Pipeline Cache 磁盘持久化与设备兼容性校验 | `arch-stage-5b-pipeline-cache` |
| Stage 6 | RenderPass 资源声明与合法性校验 | `arch-stage-6-pass-resources` |
| Stage 7A | 显式 Dependency、DAG 拓扑排序与执行计划接入 | `09b8819` |
| Stage 7B | HDR SceneColor、ToneMapping、跨 Pass 同步与 UI 输出 | `7b` |
| Stage 7C | 配置驱动资源绑定、公共 Descriptor/Pipeline 构建和 Execute | `7C` |

## 已确认的方向与阶段边界

主线是渲染框架搭建。长期希望通过蓝图组织节点，在节点中配置 graphics pipeline 和 shader；目前没有要求实现蓝图编辑器、外部配置解析或 Shader 反射。

当前默认 Skybox → PBR → ToneMapping 管线用于验证框架。Stage 7C 已打通“描述 → 资源准备 → Descriptor/Pipeline 构建 → Execute”，不继续把默认 PBR 的材质效果细化当作本阶段任务。实验工具的价值是降低添加节点、替换 shader 和验证渲染想法的成本，不承诺跨引擎迁移整条管线。

## 当前对象所有权

| 对象 | 持有者与职责 |
|---|---|
| Scene、Renderer、RenderContext、UI | Application 持有，组织初始化和帧循环 |
| Pass 描述、公共 RenderPass 实例、FrameGraph | RenderContext 持有；明确组织节点和依赖 |
| 节点、显式边、编译执行计划 | FrameGraph 持有；节点借用 RenderPass 指针 |
| Swapchain、FrameContext、RenderResourceRegistry、UI render target | Renderer 持有，负责创建资源、录制、同步和提交 |
| 内部 image/buffer、默认 sampler | Registry 持有；导入的外部资产只保存 Manager handle |
| Model、普通 Texture、EnvironmentCubeMap | ResourceManager 持有，handle 使用 index + generation |
| VkRenderPass、framebuffer、Set 0、pipeline layout、pipelines | 公共 RenderPass 持有；descriptor set layout 由 LayoutRegistry 管理 |
| Material Set 1、逐帧 MeshData Set 2、MaterialBuffer Set 3 | Model 分配、写入并回收 |

Registry 当前由 Renderer 持有，不是 FrameGraph 成员，也不是单例。销毁时先由 Renderer 等待 GPU 并清理 render target，再销毁 Context/Pass，之后清理 ResourceManager 和底层 descriptor/upload 服务。

## 配置与协议的权威来源

| 文件 | 负责内容 |
|---|---|
| `engine/core/config/schema.h` | Set 0/1/2/3 的 binding、descriptor type、count 和 stage；公共 Pass 与 Model 共同参考 |
| `engine/render/config/default_render_pipeline.h` | 默认 Pass、shader 路径、绘制类型、管线变体，以及资源到 schema binding 的对应 |
| `engine/render/config/pass_resource.h/.cpp` | ResourceId、资源格式/实例策略、外部路径与资源引用 |
| `engine/render/config/shader_protocol.h` | Camera/SceneParam 的 CPU 格式、set/binding 常量引用和字段 offset 断言 |
| `engine/render/config/graphics_pipeline_defaults.h` | 当前固定的 graphics pipeline 状态和采样默认值 |
| `engine/scene/config/scene_description.h` | 场景模型配置 |
| `engine/render/render_pass_description.h` | 配置与运行时的接口类型，按用户要求放在 config 目录外 |
| `data/shaders/includes/camera.glsl`、`scene_params.glsl` | Shader 共享参数块，与 CPU 协议人工保持一致 |

管线配置不再另写一套 descriptor layout：它引用 schema，并指定每个 binding 对应的资源。当前配置仍是 C++ 文件，没有外部配置文件解析器。

## 初始化与每帧调用链

```text
Application
  → RenderContext::Init(pipelineDescriptions)
      → 按描述创建公共 RenderPass → 建立 FrameGraph
  → Renderer::PrepareFrame(context)
      → 创建内部资源
      → RenderPass::Prepare(compiledPass, registry, swapChain)
          → RequireResource → 创建 render target
          → SetUpDescriptorSetLayouts → AllocateDescriptorSets → UpdateDescriptorSets
          → SetUpPipelineLayout → SetUpPipelines

每帧：
BeginFrame()       → 等待当前 frame fence、acquire 图像、设置 Registry 当前图像
ExtractScene()     → 生成 RenderScene
SetRenderScene()   → 保存快照、更新当前帧 UBO、处理图像相关 Set 0
Render()          → 按执行计划录制 barrier、render pass 和 Execute
UI                → 独立 Pass 叠加到 BackBuffer
EndFrame()        → 提交与呈现
```

`PrepareFrame/Prepare` 不再接收初始 RenderScene；`Execute(cb, frameIndex, renderScene)` 不再接收 imageIndex。Renderer 仍使用 acquire 返回的 imageIndex 选择 framebuffer 和交换链关联图像。

## Stage 7C 完成内容

- Input/Output 统一使用资源 reference；公共 `GetResourceUsage()` 直接读取声明中的 usage，消除了派生类重复声明和 UBO 用途错误。
- 外部资源由配置路径驱动，Pass 准备时通过 Registry 按需请求 Manager 加载；普通纹理和整套环境资源使用通用 handle，不再有 LUT 专用成员和运行时资源名 switch。
- 环境按整体加载，Pass 用 Source/Irradiance/Prefiltered 选择其中纹理。Scene 的重复加载路线已删除；预滤波 mip 层数从 Registry 中的 EnvironmentCubeMap 获取。
- BRDF、Eu、Eavg LUT 已作为离线 KTX 资产直接加载；环境的 irradiance/prefiltered 仍在加载时生成。
- Camera 和 SceneParam 已有固定 C++/GLSL 公共协议；Renderer 按协议组装 RenderScene 数据并写入当前帧 UBO。字段语义组装仍是显式代码，不是反射驱动。
- 公共层完成 descriptor layout、Set 0 分配/写入、pipeline layout、graphics pipeline 创建和销毁。Model 独立完成 material/mesh buffer 创建与 Set 1/2/3 分配、写入。
- 固定资源的 Set 0 在准备时为各 frame slot 写入；UBO 每帧只更新内容。采样当前交换链关联 HDR 图像的 Set 0 在 acquire 后按当前图像重写；Resize 后同样更新该绑定。
- 公共 Execute 支持 SceneGeometry、SkyboxGeometry、FullscreenTriangle 三种绘制协议；已删除三个旧派生 Pass 和无用 FullScreenPass 预计算辅助模块。
- 默认场景管线已接入 PBR、双面、透明混合、Unlit 选择。当前固定优先级为 Transparent → AlphaBlending，否则 Unlit → DoubleSided → Pbr。

## 验证结果（2026-09-14）

- 最终代码执行 `cmake --build --preset gcc-ninja` 成功；Shader 编译脚本会追踪公共 include 的修改。
- 最终程序通过命令行启动，进入渲染循环并正常退出，退出码 0；Validation/错误日志 0，stderr 为空。
- 环境资源只加载一次；退出没有 descriptor unknown set、double-free 或生命周期报错。
- 本次日志只有既有 `StagingRingAllocator: wrap with in-flight uploads` warning；构建中的 gli enum warning 也属于既有第三方问题。
- 此前已验证默认画面及 Resize；最终收尾仅进行命令行验证，没有重新操作桌面检查画面。
- 当前默认资产未覆盖 BLEND/Unlit 的专项视觉效果，用户明确暂缓，不能将它们作为继续扩展本阶段的理由。
- `git diff --check` 通过。

## 已知边界与待讨论项

1. Dependency 仍由 Context 显式连接当前三个节点，依赖固定顺序；用户要求先不改。动态建图前还需讨论 Node ID 重置和再构建语义。
2. 绘制类型、Set 0/1/2/3 分工、材质变体映射和 push constant 格式目前固定。Scene draw 推送 mesh/material 索引，Fullscreen 当前推送 exposure；不是任意 Shader 协议。
3. Camera/SceneParam 的 CPU 语义组装和 C++/GLSL 格式对应仍人工维护；反射、自定义参数系统见 `SHADER_PARAMETER_PROTOCOL.md`，本阶段不继续展开。
4. 外部纹理热替换尚无通用 descriptor dirty 跟踪；材质混合特性组合、跨材质对象的 descriptor 去重暂未实现。
5. UI 仍在 Graph 外；MSAA 关闭；尚无资源别名、Pass culling、自动生命周期分析和 async compute。
6. Descriptor 与 staging 单例目前按主线程使用；后台资源线程需要另行明确同步边界。

下一次继续时以这些已确认边界为准，先讨论下一项框架需求，不重复开展已经完成的 7C，也不自动开始蓝图、反射或材质效果完善。
