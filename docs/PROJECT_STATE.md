# Vulkan PBR 项目当前状态

最后更新：2026-10-08

## 快速定位

- 实际仓库：`E:\vulkanProject\Vulkan-glTF-PBR-master\vulkan_pbr`。
- 全局协作规则：[E:\Agents\AGENTS.md](E:/Agents/AGENTS.md)，新会话开始时先读取。
- 稳定分支：`main`；最后稳定实现提交为 `2a3bcd5 feat(8b): connect static models to scene rendering`，已推送至 `origin/main`。进度文档提交可以跟随该实现提交。
- 上一已标记稳定点：`31b093b feat(8a): add CPU model data and validation`，tag `8A`；本次未创建新 tag。
- 当前完成阶段：Stage 8B，ModelData 转运行时 Model，以及默认静态模型的 Scene/RenderItem/绘制端接入；不代表候选 Stage 8F/8G 的完整能力已完成。
- Stage 7E 已通过构建、30 项配置测试和运行日志验证，用户已确认阶段完成。
- 当前主线是资产导入；`AssetLoader → ModelData → Model → Scene/RenderItem → RenderPass` 已接入默认场景路径并提交。构建、Scene 集成用例和运行日志验证已通过，用户已实际运行并同意结束本段。
- 下个窗口先读取本文并核对 Git。建议讨论的下一项实验是同一 Model 添加两个 SceneObject，打通各对象独立变换的实际绘制；尚未授权实施，不自动展开 FBX 或其他能力。

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
| Stage 8A | ModelData/MeshData/NodeData、Mesh 局部 AABB 与纯 CPU 校验 | `31b093b`，tag `8A` |
| Stage 8B | 运行时 Model、共享 MeshResource、glTF 导入新数据层、静态 Scene/Item 与绘制端接入 | `2a3bcd5`，未创建 tag |

## 已确认的方向与阶段边界

主线是渲染框架搭建。长期希望通过蓝图组织节点，在节点中配置 graphics pipeline 和 shader；目前没有要求实现蓝图编辑器、外部配置解析或 Shader 反射。

当前默认 Skybox → PBR → ToneMapping 管线用于验证框架。Stage 7C 已打通“描述 → 资源准备 → Descriptor/Pipeline 构建 → Execute”，不继续把默认 PBR 的材质效果细化当作本阶段任务。实验工具的价值是降低添加节点、替换 shader 和验证渲染想法的成本，不承诺跨引擎迁移整条管线。

## 后续推进共识（2026-09-17）

遵循 [全局协作准则](E:/Agents/AGENTS.md) 中的“需求与架构共同演进”。以现有 PBR 管线和 Stage 7E 后的成果为基础，近期主线改为完成 Blender/FBX 静态场景导入，以 Classroom 和 Barcelona 场景作为真实需求来源。长期仍希望方便组合渲染流程、修改 Shader 和观察结果，但不在资产导入阶段提前展开蓝图、反射或任意 Shader 协议。

以下阶段保留为候选能力地图。近期任务以本文记录的最新共识和实际代码为准，不按表格顺序自动展开；新 Model 的静态渲染链路已接通，下一项实验另行讨论：

| 阶段 | 目标 | 明确边界 |
|---|---|---|
| Stage 8A | 建立统一资产数据层 | 定义与文件格式、Vulkan 无关的 ModelData/MeshData/NodeData 及校验；不接 FBX、不创建 GPU 资源 |
| Stage 8B | ModelData 转换为运行时 Model | 创建共享 MeshResource、GPU 上传和 CPU 重数据释放策略；不解析 FBX |
| Stage 8C | 接入 ufbx 和静态几何 | FBX 转换为 MeshData，保证顶点属性、索引、Primitive 和共享 Mesh 正确 |
| Stage 8D | 导入节点层级与变换 | 处理父子关系、Mesh 引用、坐标系、单位和 FBX 变换语义 |
| Stage 8E | 导入基础材质和纹理 | 支持明确的基础 PBR 子集、纹理寻址与去重；不承诺还原 Blender 程序材质 |
| Stage 8F | 接入 ResourceManager 和 Scene | 建立资源句柄、共享资源与独立实例变换；不实现完整场景编辑器 |
| Stage 8G | 完整场景验收 | Classroom 作为完整静态场景验收，Barcelona 用于复杂材质和室外场景验证 |
| Stage 8H | 评估统一 glTF 导入链路 | 仅在前述数据协议验证稳定后，决定是否让 glTF 同样输出 ModelData |

Stage 8A 的核心约束：MeshData 保存可共享的几何和 Primitive，不保存实例变换；NodeData 保存资产原始层级、局部变换和 MeshData 引用；多个节点可以引用同一 MeshData；ModelData 不出现 Vulkan 类型。阶段验收包括合法数据构造、共享引用，以及对无效索引、错误父子关系、节点环和非法矩阵的 CPU 校验。现有 glTF 路径和默认画面必须保持可用。

Stage 8A 已于 2026-09-18 经用户 review 确认完成，并已提交为 `31b093b`、标记 `8A`。新增纯 CPU 的 AABB 与 ModelData 数据层；Mesh AABB 从全部顶点计算，处于 Mesh 局部空间，空 Mesh 的 AABB 无效。校验覆盖顶点/Primitive/材质索引、Mesh 引用、父子关系、节点环、根节点、有限矩阵及 AABB 一致性。临时 CPU 用例和完整 CMake 构建通过；8A 验收时新数据层尚未接入 glTF 或运行时 Model，未进行新链路的画面验证。

### Stage 8B 完成内容与验收状态（2026-10-08）

- `Model::Create(const ModelData&, std::string*)` 已实现数据校验、每份 MeshData 创建共享几何 MeshResource、节点层级与模型空间变换、模型 AABB、节点实例槽位、材质参数、纹理、GPU buffer 和 Set 1/2/3 创建与写入。多个节点可引用同一份几何，`meshIndex` 与 `instanceSlot` 分别编号。
- `AssetLoader::LoadModelData` 已有 glTF/glb 分派，`GltfAssetLoader` 已有生成 ModelData 的实现；FBX 分支仍返回未实现。`external/ufbx` 源码与许可证已纳入仓库，但尚未加入构建或接入导入器。新引擎源文件已列入 `engine/CMakeLists.txt`。
- 默认入口已改为 `ResourceManager::LoadModel → AssetLoader::LoadModelData → Model::Create`，模型池改为持有新 Model。Scene 从新模型 AABB 计算自动居中与缩放，并拒绝失效 handle；空包围盒与退化为点的模型不会除以零。
- SceneExtractor 遍历新 Model 的扁平节点和 MeshResource 的 Primitive 生成 Item，分别填写 `meshIndex`、`instanceSlot`、`materialIndex` 和索引区间；节点层级矩阵由 Model 计算，提取时仅叠加 SceneObject 变换。RenderPass 按 Item 的 mesh 绑定 VBO/IBO，并使用新 Model 的 Set 1/2/3；天空盒与环境预计算仍使用旧 GLTFModel 系统资源。
- 新路径仅消费静态模型数据，Application 的旧动画 UI 和动画更新调用已移除。新 glTF 导入器的 tinygltf 编译选项已对齐；Model 就绪检查已覆盖纹理回执，空实例不再分配 Set 2，几何创建失败清理前等待已提交上传。
- `cmake --build --preset gcc-ninja` 编译和链接成功；临时 Scene 集成用例通过，覆盖共享 mesh、父子变换、实例槽位、Primitive 区间、材质队列、各 SceneObject 的 Item 变换、自动居中缩放、空资产、失效 handle、导入失败不占槽、上传就绪与延迟释放。临时用例文件已清理。
- 默认程序通过命令行运行并进入渲染循环，正常退出码 0；默认运行与集成用例日志均无 Validation/error、descriptor unknown set 或生命周期错误，stderr 为空。用户已实际运行，并同意结束本次静态链路接入；本次没有逐项重新验收全部材质视觉效果。提交前复核构建和暂存区 `git diff --check` 通过，旧 glTF 拆分文件的空白问题已清理。FBX、蓝图、反射及额外材质能力仍按后续实际需求单独确认。
- 用户曾反馈高 FPS 下鼠标旋转持续跳动，随后认为是电脑当时负载过高，本次不继续排查。只读检查发现 FPS 统计未包含消息处理时间、相机矩阵在绘制结束后更新，但没有确认它们是卡顿主因，也未修改相机、输入、计时或增加限帧策略。

## 当前对象所有权

| 对象 | 持有者与职责 |
|---|---|
| Scene、Renderer、RenderContext、UI | Application 持有，组织初始化和帧循环 |
| Pass 描述、公共 RenderPass 实例、FrameGraph | RenderContext 持有；明确组织节点和依赖 |
| 节点、显式边、输出节点 ID、编译执行计划 | FrameGraph 持有；节点借用 RenderPass 指针 |
| Swapchain、FrameContext、RenderResourceRegistry、UI render target | Renderer 持有，负责创建资源、录制、同步和提交 |
| 内部 image/buffer、默认 sampler | Registry 持有；导入的外部资产只保存 Manager handle |
| 场景模型池（新 Model）、天空盒（旧 GLTFModel）、普通 Texture、EnvironmentCubeMap | ResourceManager 持有，池中资源 handle 使用 index + generation；天空盒仍为系统直接成员 |
| 新 Model 的 MeshResource、ModelNode、材质数据和模型内 Texture | 新 Model 持有；节点仅引用 meshIndex，多个节点共享 Model 内同一 MeshResource |
| 新 MeshResource 的 VBO/IBO、Primitive 与局部 AABB | MeshResource 持有；不保存节点实例变换或 CPU 顶点/索引数组 |
| VkRenderPass、framebuffer、Set 0、pipeline layout、pipelines | 公共 RenderPass 持有；descriptor set layout 由 LayoutRegistry 管理 |
| 场景模型 Material Set 1、逐帧实例矩阵 Set 2、MaterialBuffer Set 3 及对应 buffer | 新 Model 管理；descriptor set 经 DescriptorAllocator 分配、归还，layout 由 LayoutRegistry 管理 |

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
7. 新 Model 已接入默认运行链路并通过构建和 Validation/日志检查，用户已实际运行并同意结束本段。新路径按静态数据处理，不提供旧动画接口；天空盒仍沿用旧系统模型。
8. 新 Model 的材质映射目前沿用固定 metallic-roughness shader 协议：金属度与粗糙度使用同一张打包纹理，shader 固定读取 B/G 通道；SurfaceQuantity 的任意通道选择尚未传入 shader。Model 不保留输入顶点、索引和纹理编码字节，输入 ModelData 的释放时机由调用方决定，模型仍保留 CPU 实例与材质参数数组。
9. SceneExtractor 的 Item 包含各 SceneObject 的完整变换，但当前 shader 仍统一使用第一个 SceneObject 的 UBO model 矩阵，尚未支持多个场景对象独立变换的实际绘制；Model 内共享 mesh 的各节点使用各自的 instanceSlot。新材质数据没有 Unlit 字段；未指定材质的 Primitive 当前跳过，导入器尚未补齐默认材质。

下一次继续时以稳定实现提交 `2a3bcd5` 为基础，结合已知边界确定下一项任务。建议先讨论“同一 Model、两个 SceneObject、不同位置”的实验，解决当前 shader 统一使用第一个对象 model 矩阵的限制；此建议不是实施授权。不重复开展已完成的静态链路接入，不按候选能力地图自动展开 FBX 或其他后续能力。
