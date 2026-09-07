# Vulkan PBR 项目当前状态

最后更新：2026-09-07

## 快速定位

- 稳定分支：`main`
- 当前完成阶段：Stage 7B，HDR SceneColor + ToneMapping 真实多 Pass 管线。
- 稳定标记：`7b`
- 下一阶段：Stage 7C，外部数据绑定与 RenderPass 抽象收口。

新任务应先核对 Git 和代码；本文是交接摘要，不替代实现。

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

## 当前架构数据流

```text
Application
  ├─ RenderContext
  │    ├─ Skybox / PBR / ToneMapping RenderPass
  │    ├─ FrameGraph
  │    └─ 中央资源描述表
  ├─ Renderer
  │    ├─ RenderResourceRegistry
  │    ├─ 按 ExecutionPlan 录制与执行 Pass
  │    └─ 命令提交、同步和 GPU 资源创建
  └─ SceneExtractor -> RenderScene

Skybox.Output(SceneColorHdr)
  -> PBR.Output(SceneColorHdr)
  -> ToneMapping.Input(SceneColorHdr)
  -> ToneMapping.Output(BackBuffer)
  -> UI
```

## Stage 7B 完成状态

- `SceneColorHdr` 是 Renderer 创建并由 `RenderResourceRegistry` 持有的 HDR 图像，格式为 `VK_FORMAT_R16G16B16A16_SFLOAT`。
- Skybox 先清理并写入 `SceneColorHdr`，PBR 以 attachment continuation 方式继续写入。
- FrameGraph 生成 Skybox -> PBR 的 attachment dependency，以及 PBR -> ToneMapping 的运行时 image barrier。
- ToneMapping 采样 HDR 图像，输出线性颜色到 sRGB Swapchain。
- UI 使用独立 RenderPass，在 ToneMapping 之后 load BackBuffer，最终转换到 Present layout。
- RenderPass 基类持有 `VkRenderPass`、framebuffer 和 clear value，根据 Pass Output 的 Color/Depth attachment 声明创建 render target。
- Renderer 不识别具体 Skybox/PBR/ToneMapping 类型，只按 FrameGraph 执行计划调用 RenderPass 基类接口。
- Resize 会先销毁 framebuffer，重建 Swapchain 和内部图像，再重建 Pass framebuffer 与 ToneMapping descriptor。
- 当前按已确认范围关闭 MSAA；MSAA 开启路径不属于 `7b` 验收范围。
- 用户已确认编译、Validation、画面与运行正常。

## 当前边界

- Context 是决策者：持有 Pass 和 FrameGraph，明确组织三个 Pass 及两条资源依赖。
- Renderer 是执行者：创建 image/buffer，录制 barrier 和 Pass，提交 Vulkan 命令。
- RenderResourceRegistry 按 `RenderResourceId` 持有内部 image/buffer，不是单例。
- RenderPass 声明 Input/Output；Input 目前为 descriptor 资源，Output 目前为 Color/Depth attachment。
- FrameGraph 根据显式 edge 生成顺序和同步配置，暂不自动推导 dependency。

## 已知遗留项

1. `MainCamera` 和 `SceneParam` 已是固定 `RenderResourceId`，但它们从外部 Scene/Camera 到资源的数据源绑定仍在 `Renderer::UploadFrameUniformData()` 中硬编码。
2. Descriptor set/layout/update 仍由各派生 RenderPass 处理，尚未通过统一 Input 绑定描述驱动。
3. `GetResourceUsage()` 与 Input/Output 声明存在重复，后续应由 RenderPass 公共层统一查询。
4. FrameGraph Node ID 的重置和再构建语义需要在支持动态管线前收口。
5. UI 暂时保持在 FrameGraph 外的独立 RenderPass。
6. 尚未实现资源别名、Pass culling、自动生命周期分析和 async compute。
7. Model material/SSBO descriptor 的分配与写入所有权仍分散在 Model 和 PBR Pass。
8. Descriptor 和 staging 单例按主线程使用设计，引入后台资源线程前需要明确同步边界。

## 下一阶段：Stage 7C

阶段名：**外部数据绑定与 RenderPass 抽象收口**。

目标：

1. 建立外部 Scene/Camera 数据源到 `MainCamera` / `SceneParam` 的明确绑定。
2. 移除 Renderer 对两份数据内容的硬编码组装。
3. 由 RenderPass 公共层根据 Input/Output 查询 usage，删除派生 Pass 重复的 `GetResourceUsage()`。
4. 只抽象当前真实需要的 descriptor input 绑定，不提前引入完整 RHI 或资源优化器。
