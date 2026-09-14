# Vulkan PBR 项目当前状态

最后更新：2026-09-14

## 快速定位

- 实际仓库：`E:\vulkanProject\Vulkan-glTF-PBR-master\vulkan_pbr`。
- 全局协作规则：[E:\Agents\AGENTS.md](E:/Agents/AGENTS.md)，新会话开始时先读取。
- 稳定分支：`main`；本阶段稳定标记：`7E`，具体提交以该 tag 指向为准。
- 上一稳定点：`e51bd68 feat(7d): configure render pipeline dependencies`，tag `7D`。
- 当前完成阶段：Stage 7E，输出节点配置与 Pass 剔除。
- Stage 7E 已通过构建、30 项配置测试和运行日志验证，用户已确认阶段完成；下一阶段范围尚未确定。

新任务先读取全局协作规则，再核对 Git 与代码并阅读本文。协作习惯统一维护在全局文件，本文记录项目状态与已确认的技术边界。

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
| Stage 7D | 管线依赖配置化、连接校验和图内 Node ID 重建 | `7D` |
| Stage 7E | 输出节点配置、反向祖先标记和 Pass 剔除 | `7E` |

## 已确认的方向与阶段边界

主线是渲染框架搭建。长期希望通过蓝图组织节点，在节点中配置 graphics pipeline 和 shader；目前没有要求实现蓝图编辑器、外部配置解析或 Shader 反射。

当前默认 Skybox → PBR → ToneMapping 管线用于验证框架。Stage 7C 已打通“描述 → 资源准备 → Descriptor/Pipeline 构建 → Execute”，不继续把默认 PBR 的材质效果细化当作本阶段任务。实验工具的价值是降低添加节点、替换 shader 和验证渲染想法的成本，不承诺跨引擎迁移整条管线。

## 当前对象所有权

| 对象 | 持有者与职责 |
|---|---|
| Scene、Renderer、RenderContext、UI | Application 持有，组织初始化和帧循环 |
| Pass 描述、公共 RenderPass 实例、FrameGraph | RenderContext 持有；明确组织节点和依赖 |
| 节点、显式边、输出节点 ID、编译执行计划 | FrameGraph 持有；节点借用 RenderPass 指针 |
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
  → RenderContext::Init(defaultPipeline)
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

## Stage 7D 完成内容与验收（2026-09-14）

- 新增 `render_pipeline_description.h`：`RenderPipelineDescription` 包含 Pass 描述和 `PassDependencyDescription`；连接保留来源、目标和资源三个字段。
- `default_render_pipeline.h` 的 `defaultPipeline_` 同时配置三个 Pass 与两条 HDR 依赖；Context 不再按数组位置写死连接。
- Context 校验 Pass 名称非空且唯一、连接端点存在、禁止自依赖，以及两端声明了完整的资源 reference；循环依赖继续交由 FrameGraph 拒绝。
- FrameGraph 的 Node ID 改为图内连续编号，修复 Reset 后重复建图和多个 Context 共用静态计数导致的错误；既有 attachment dependency/barrier 编译链路保持不变。
- 本阶段仅配置已有 edge。PBR 承接天空盒仍通过输出 attachment 的 LOAD；尚未将其改造成通用 Input/Output 端口。
- `cmake --build --preset gcc-ninja` 成功；16 项临时配置测试通过，覆盖默认同步、调换 Pass 排列、重复建图、独立 Context、不同节点数量、非法名称/资源/循环依赖和失败后恢复。
- 最终代码重新构建并通过 16 项配置测试；默认程序进入渲染循环运行 5 秒后正常退出，退出码 0，Validation/错误日志 0、stderr 为空，`git diff --check` 通过。用户已检查画面并确认正常。仍有既有 staging ring warning 和第三方 gli 编译 warning。

## Stage 7E 完成内容与验收（2026-09-14）

- `RenderPipelineDescription::outputPasses_` 配置最终输出 Pass 名称，默认是 `ToneMappingRenderPass`；Context 校验名称并解析为 Node ID，通过 `SetOutputNodes()` 交给 FrameGraph。
- FrameGraph 拒绝空输出列表和不存在的输出 ID；完整图先做拓扑排序与循环检查，再从输出沿入边标记祖先、过滤执行顺序，仅为保留节点生成执行计划和同步信息。未使用分支中的循环依赖仍报错。
- 剔除逻辑前后有中文分隔注释；剔除不删除原始节点或 edge，不改变 Node ID。多个输出可共享祖先；Reset 清空输出起点。
- Renderer 既有资源准备与执行链路继续读取执行计划，被剔除节点不进行 Pass::Prepare/Execute；UI 仍在图外。没有新增运行时热重建或副作用自动识别机制，必须执行的节点应由配置列入输出起点。
- 构建成功；30 项临时配置测试通过，覆盖默认同步、无用节点与整条分支、多输出共享祖先、重复输出、保留节点 ID、重复建图、空/未知输出、未使用分支中的错误及失败恢复。
- 默认程序运行渲染循环约 5 秒并正常退出，退出码 0；日志确认保留 3/3 节点、剔除 0 节点，Validation/错误日志 0、stderr 为空。`git diff --check` 通过；仅有既有 staging ring 和 gli warning。本次未重新检查画面。

## 已知边界与待讨论项

1. Dependency 现由 RenderPipelineDescription 显式配置来源 Pass 名称、目标 Pass 名称和资源，Context 按名称解析 Node ID；不从 Input/Output 自动推导。Node ID 仅在本次建图内有效，Reset 后从 0 重新编号。尚未实现端口连接、资源来源解析或已准备 GPU 资源后的运行时热重建。
2. 绘制类型、Set 0/1/2/3 分工、材质变体映射和 push constant 格式目前固定。Scene draw 推送 mesh/material 索引，Fullscreen 当前推送 exposure；不是任意 Shader 协议。
3. Camera/SceneParam 的 CPU 语义组装和 C++/GLSL 格式对应仍人工维护；反射、自定义参数系统见 `SHADER_PARAMETER_PROTOCOL.md`，本阶段不继续展开。
4. 外部纹理热替换尚无通用 descriptor dirty 跟踪；材质混合特性组合、跨材质对象的 descriptor 去重暂未实现。
5. UI 仍在 Graph 外；MSAA 关闭；已实现基于显式输出节点的 Pass culling，尚无资源别名、自动生命周期分析和 async compute。
6. Descriptor 与 staging 单例目前按主线程使用；后台资源线程需要另行明确同步边界。

下一次继续时以 7E 稳定点和这些已确认边界为准，先讨论下一项框架需求，不重复开展已经完成的 7C/7D/7E，也不自动开始蓝图、反射或材质效果完善。
