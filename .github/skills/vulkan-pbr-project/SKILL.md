---
name: vulkan-pbr-project
description: "适用于 Vulkan glTF PBR 项目的开发指引。Use when: working on Vulkan渲染引擎, C++项目结构, CMakeLists配置, PBR渲染管线, Win32窗口系统"
---

# Vulkan glTF PBR 项目开发指引

## 项目结构

```
vulkan_pbr/
├── app/                        ← 应用层：入口 + 窗口 + 渲染组合
│   ├── main/main.cpp           ← WinMain / main 入口
│   ├── application/            ← Application 类，管理 Device/Renderer/Window 生命周期
│   └── ui/                     ← ImGui 调试 UI（app 专属，不在引擎中）
│
├── engine/                     ← 引擎层：跨项目可复用的核心
│   ├── core/                   ← 设备/交换链/StagingRing/工具/宏（基础层）
│   ├── resource/               ← 资源层：纹理 + glTF 模型 + ResourceManager(Handle 池)
│   ├── scene/                  ← 场景层：Camera + SceneExtractor
│   ├── render/                 ← 渲染层：Renderer + Pass 框架 + RenderScene
│   ├── platform/               ← 平台层：窗口抽象（PIMPL 模式）
│   └── utils/                  ← 工具层：日志（Logger 单例）
│
├── external/                   ← 第三方库（glm, gli, imgui, tinygltf, basisu）
├── data/                       ← 运行时资产（模型、纹理、环境贴图）
└── CMakeLists.txt              ← 项目根 CMakeLists
```

## 架构层级

```
app        → 表现层：窗口 + 事件循环 + 渲染管线编排 + ImGui 调试面板
engine     → 逻辑层：GPU 设备管理、资源加载、场景数据、帧循环骨架
external   → 外部库：数学、文件解析、UI 框架
```

### engine 子层依赖关系

```
render/  Pass 框架     scene/  场景数据
   ↑                       ↑
resource/  资源加载与管理
   ↑
core/  设备/交换链/工具    platform/  窗口抽象
   ↑
utils/  日志
```

**所有依赖单向向下，不允许循环依赖。**

## 核心设计决策

- **include 路径**：从项目根写起（`#include "engine/core/device.h"`），搜索根由 `target_include_directories(engine PUBLIC ..)` 保证。
- **命名空间**：`engine::core / resource / scene / render / platform / utils`；应用层 `app`。
- **窗口抽象**：`engine/platform/window.h` 用 PIMPL 隐藏 Win32 句柄。
- **渲染框架（Pass 模式）**：Renderer 管"画框"（swapchain/commandPool/sync/framebuffer/统一 descriptor pool），按序执行 `std::vector<std::unique_ptr<RenderPass>>`，不关心 Pass 具体画什么；Pass 管"画什么"（layout/pipeline/Execute）。
- **资源管理**：`ResourceManager` 单例 + 强类型 Handle（index+generation）+ `ResourceState` 状态机（Free/Uploading/Ready/PendingDelete）+ 延迟删除队列。
- **日志**：全局单例 `engine::utils::Logger`，宏 `LOG_INFO/LOG_WARN/LOG_ERROR/LOG_FATAL`（流式写法，FATAL 自动 abort）。

## 代码排版与语义层级

优先遵循项目现有风格，并吸收 Google C++ 规范中关于命名清晰、类型安全、资源生命周期和可读性的原则；不要机械套用 Google 的 80 列限制或大括号格式。

### 语句与调用

- 花括号独立占行；多行调用的结束符 `);` 独立占行。
- 一个完整操作结束后留空行，不在操作内部随意插空行。
- 长调用不机械逐参数拆行：按外层参数的**语义**分行；拆分产生大量短小不对称行、降低可读性时，保留完整表达式。

```cpp
if (condition)
{
    Execute();
}

SUCCESS_OR_LOG(
    vkAllocateMemory(device.GetLogicalDeviceHandle(), &memAllocInfo, nullptr, &attachment.memory_) == VK_SUCCESS,
    "Failed to allocate image memory"
);
```

### Vulkan 创建代码

按资源创建阶段分组，并在每个完整步骤后留空行：

1. 填写 CreateInfo
2. 创建 Vulkan 对象
3. 查询内存需求
4. 分配内存
5. 绑定内存
6. 创建 View

不要为了减少函数长度而破坏这一顺序，也不要进行不符合项目风格的过度抽象。

### 注释规范

核心原则：**代码说"是什么"，注释只说代码说不出来的东西**。写注释前先问：删掉这条注释，半年后的维护者会不会做出错误修改？会，才值得写。

1. **Why, not What**：解释为什么这样做，不复述代码。`// 初始化 descriptor pool`（函数名已表达）是坏注释；`// 过滤 0 项（VUID-00302 要求 >0），场景模型在 PrepareFrame 之后才加载` 是好注释。
2. **契约在声明处**：接口语义（如 `PeekModel` 与 `GetModel` 的区别）只写在头文件声明处；实现和调用点不复述。调用点只写该处特有的上下文（如"此处 PrepareFrame 时模型必为 Uploading"），不抄接口契约。
3. **三类注释必须写**：
   - **陷阱**：违反直觉的做法及原因（如"不要用 vkResetQueryPool，1.2 才进核心，本工程是 1.0"）；
   - **规范出处**：VUID 条款、spec 章节、平台差异；
   - **不变量与所有权**：状态机转换条件、谁负责销毁、跨帧生命周期、Handle/generation 语义。
4. **死代码直接删**：不留注释掉的函数/变量（git 历史即存档）。想留设计痕迹，写一行"曾考虑 X，因 Y 放弃"比留整块尸体有价值。
5. **一个文件一种语言**：引擎层设计注释统一中文；从 VulkanSample 继承的英文注释不必专门翻译，但新写的不再混英文复述体。
6. **数量直觉**：越底层越少（buffer/image 创建序列自解释），决策越密集越多（同步、生命周期、状态机）。

#### 头文件注释：作用与参数

- **作用（语义摘要）要写**：头文件是"读接口不读实现"的消费者视角，非平凡公有函数值得一行契约级描述（语义边界、前置后置条件），如 `PeekModel` 的注释。
- **参数只在名字和类型说不清楚时写**：逐参数 Doxygen（`@param x ...`）在本项目是噪音，`fileName` 不需要解释。但**非显而易见的约束必须写**：
  - 单位（弧度还是度、字节还是元素数）；
  - **所有权与生命周期**（谁销毁、指针有效期到何时、能否为 null）；
  - 合法范围与默认值语义（如 `oldLayout` 默认 UNDEFINED，且 UNDEFINED/PREINITIALIZED 时 srcAccessMask=0）。
- 判别法：注释写的是"调用者不读实现就会用错的东西"就写；读名字就知道的不写。

## 架构演进路线

后续开发者必须根据下表的时间与依赖顺序逐步构建。每个阶段都必须恢复到可运行、可审查、可回退的稳定状态后，再继续下一阶段。

| 顺序 | 阶段 | 主要实现 | 完成标准 | 对应 Tag |
|---:|---|---|---|---|
| 0 | 当前稳定基线 | 收口 Resize、per-image attachment、extension getter 和现有 skill 修改 | 编译通过；MSAA 开关、拖拽、最小化和恢复正常；Validation 无新增错误 | `stage-0` |
| 1 | FrameContext | CommandBuffer、Fence、imageAvailable 按 `frameIndex_` 管理；renderFinished 按 `imageIndex_` 管理 | 不再使用平行同步数组；严格区分 `frameIndex_` 与 `imageIndex_`；Resize 正常 | `stage-1` |
| 2 | GPU 可观测性 | Debug Utils、对象命名、Pass Label、Timestamp Query | RenderDoc 可识别 Pass 和对象；UI 能显示已完成帧的 GPU 时间 | `stage-2` |
| 3A | Resource Handle | 强类型 Handle、index + generation、资源状态查询 | Scene 不再长期持有资源裸指针；旧 Handle 可检测失效 | `arch-stage-3a-resource-handles` |
| 3B | RenderScene | SceneExtractor、RenderItem、相机数据、视锥裁剪与分类 | RenderPass 不遍历 Scene/glTF 树；Render 头文件不依赖 `scene.h` | `arch-stage-3b-render-scene` |
| 4A | 资源生命周期 | `ResourceState`、DeferredDeletion、GPU 完成值 | Handle 可立即失效；Vulkan 对象只在 GPU 使用结束后销毁 | `arch-stage-4a-resource-lifetime` |
| 4B | 基础异步上传 | 持久映射 staging、UploadContext、graphics queue 批量上传 | Texture/Model 正常加载路径不使用 `vkDeviceWaitIdle` | `arch-stage-4b-upload-context` |
| 4C | Staging Ring | 环形分配、对齐、回绕、Timeline 回收、大上传回退 | 不覆盖 GPU 未消费数据；连续加载不阻塞帧循环 | `arch-stage-4c-staging-ring` |
| 4D | 独立传输队列（可选） | Dedicated transfer queue、queue family ownership、后台 IO/解码 | 设备不支持独立队列时可安全回退；上传和渲染可并行 | `arch-stage-4d-transfer-streaming` |
| 5A | Descriptor Allocator | Persistent Pool、Frame Pool、耗尽扩容与安全 reset | 长期和逐帧 descriptor 生命周期隔离 | `arch-stage-5a-descriptor-allocator` |
| 5B | Pipeline Cache | 磁盘读取、设备兼容性验证、安全写回 | 重启后可复用缓存；缓存损坏时安全重建 | `arch-stage-5b-pipeline-cache` |
| 6 | Pass 资源声明 | Pass 显式声明 read/write 和 attachment 用途 | 可检查声明与实际使用；暂不自动排序或创建资源 | `arch-stage-6-pass-resource-declarations` |
| 7 | 完整 Frame Graph（条件阶段） | DAG、环检测、拓扑排序、资源生命周期、自动 Barrier/Layout、Pass 剔除 | 只有出现 GBuffer、Shadow、后处理等真实多 Pass 需求后才启动 | `arch-stage-7-frame-graph` |

### 后续开发执行规则

不要在同一修改中跨越多个阶段。每个阶段开始前：

1. 检查当前工作区、最新阶段 Tag 和上一阶段验收结果。
2. 读取当前实现，不要以路线表代替代码事实。
3. 给出具体文件、接口、数据流、兼容策略和验证方案。
4. 在用户审查方案之前不要修改文件。

每个阶段完成后：

1. 完成编译、Validation Layer 和对应运行测试。
2. 保持修改未提交，先交由用户审查。
3. 只有获得用户明确授权后才能提交。
4. 提交并确认稳定后，获用户授权才创建 annotated tag；已发布 Tag 不得强制移动，重新验收用 `-r2` 后缀新 Tag。
5. **Commit、Tag、Push 是三个独立动作**，不能因批准其中一个而自动执行其他。

不要为了到达路线终点而提前实现条件阶段。完整 Frame Graph 只有在出现至少三个中间渲染阶段，并产生真实的跨 Pass 资源依赖与 Barrier 管理压力后才启动。

## 协作与讲解规则

- 讲解阶段任务时始终采用“总—分—总”：先说目标，再讲实现细节，最后给验收标准和阶段结论。
- 用户第一次接触 Vulkan 调试设施。先解释数据流和对象归属，不要求用户记忆套路化 API；用户明确授权后，直接完成套路化代码并保留未提交状态供 review。
- 在架构选择、生命周期、同步语义或修改范围发生变化前，先给具体方案；不要把简单迁移扩张成不必要的抽象。
- 修改前先检查 `git status` 和实际 diff。用户可能同时在 VS Code 编辑文件；遇到“磁盘内容更新”冲突时暂停修改，禁止覆盖用户未保存缓冲区。
- Commit、Push、Tag 继续视为三个独立动作。实际阶段 tag 采用用户选择的短名称；Stage 0/1 已使用 `stage-0`、`stage-1`。

## 当前交接状态（2026-08-24）

### Git 状态

- `stage-0` → `ead68b1`，`stage-1` → `160b872`，`stage-2` → `4e5ae83`，均已推送。
- `arch-stage-3a-resource-handles` / `arch-stage-3b-render-scene` / `arch-stage-4a-resource-lifetime` / `arch-stage-4b-upload-context` / `arch-stage-4c-staging-ring` 均已创建并推送。
- 4C 收尾补丁（PendingUpload 简化、Uploading->Ready 状态机、PeekModel 解耦）已提交 `2de712b`。

### Stage 4C 摘要（Staging Ring 异步上传）

- StagingRingAllocator 取代旧 UploadContext（upload_context.* 已删）：`SubmitImageCopy`（一对 barrier 夹 N 条 copy，region 偏移加 ring 切片起点）、`SubmitOversizedBufferCopy`（>64MB 专用 staging + timeline 延迟销毁）、`Tick()` 挂 Renderer::PrepareFrame 每帧回收。
- 上传全走 ring：texture.cpp 5 处 + model.cpp 2 处（blit 链保持同步，同队列隐式提交序衔接）。
- `LoadModel` 返回 Uploading 不等待；`OnFrameCompleted` 检查回执（顶点/索引/材质/纹理）转 Ready；`Init` 末尾保留 `WaitAll` 兜底系统资源（skybox/空纹理不走 Handle 状态机）。
- `PeekModel`（只查 index+generation，资源存在即可）用于 descriptor 分配、RenderItem 生成、AddObject 算 AABB；`GetModel`（要求 Ready）只做渲染门控。`RenderScene::modelHandles` 显式注册全部模型供分配/轮询遍历。

### Stage 4C 踩坑记录（重要）

- `CreateDescriptorPool` 必须过滤 descriptorCount==0 的 poolSize 项（VUID-00302）：场景模型在 PrepareFrame 之后才加载，空场景 STORAGE_BUFFER 计数为 0。
- descriptor 分配（`PeekModel`，只查存在）与渲染门控（`GetModel`，要求 Ready）必须解耦；耦合会导致"模型 Uploading → 无 RenderItem → descriptor 永不分配 → 就绪后也不渲染"。`Scene::AddObject`（AABB 纯 CPU）等所有"只查存在"的调用点都要用 `PeekModel`。
- `PrepareFrame` 时 `OnFrameCompleted` 从未运行过（渲染循环未开始），模型必为 Uploading——descriptor 分配不能依赖它。

### 下一步：Stage 5A（Descriptor Allocator）

Persistent/Frame Pool 分离、耗尽扩容与安全 reset。当前一次性 pool（按 PrepareFrame 时场景模型计数）无法处理运行时新增模型。开始前先出细化方案供用户审查。

## 构建系统
- CMake + Ninja，预设 `gcc-ninja`（MinGW g++，Debug）；`engine/` 输出 `libengine.a`，`app/` 输出 `Vulkan-pbr.exe`；`add_subdirectory(engine)` + `add_subdirectory(app)`；`target_include_directories(engine PUBLIC ..)` 设置项目根搜索路径。
- **engine 源文件显式列出（不是 GLOB）**：GLOB 只在 configure 时扫描，新增/删除 .cpp 不会触发重新配置。新增文件必须同步加进 `engine/CMakeLists.txt` 对应 `ENGINE_*_SRC/_HEADERS`（`scene/scene.cpp` 曾因漏列报 undefined reference）。
- Basis Universal 源：`external/basisu/transcoder/basisu_transcoder.cpp` + `zstd/zstd.c`。
- Shader 随构建自动编译（`scripts/compile_shaders.py`）。

## 数据类型归属
| 类型/对象 | 属于 | 原因 |
|---|---|---|
| VkInstance / VkDevice / VkQueue | Device (core) | GPU 硬件抽象 |
| VkSwapchainKHR / VkSurfaceKHR | SwapChain (core) | 帧循环基础设施 |
| VkRenderPass / VkFramebuffer | **Renderer (render)** | **描述"输出目标"，与 swapchain 强绑定，由 Renderer 统一管理（mainRenderPass_ + frameBuffers_）** |
| VkPipeline / PipelineLayout | 各 Pass (render) | 跟随各 Pass 的 shader/状态 |
| VkDescriptorSetLayout / VkDescriptorSet | 各 Pass (render) | 各 Pass 的 binding 结构不同 |
| VkDescriptorPool | Renderer (render) | 统一分配入口，Pass 从 pool 分配 set |
| VkCommandPool / VkCommandBuffer | Renderer (render) | 帧循环骨架 |
| Fence / Semaphore | Renderer (render) | 帧同步 |
| VkImage / VkImageView (纹理) | resource | GPU 资源 |
| vkglTF::Model (Mesh/Node/Material) | resource | 模型数据 |
| Camera | scene | 场景数据 |
| HWND / 窗口句柄 | Window (platform) | 平台抽象 |

## Vulkan 句柄 RAII 规则（重要）

**任何直接持有 Vulkan 句柄作为成员变量的类都应是 move-only**（拷贝 = 两个对象拥有同一 GPU 资源 → double-free）：

```cpp
class Foo {
public:
    Foo() = default;
    ~Foo();                        // 析构调 Destroy()
    Foo(const Foo&) = delete;
    Foo& operator=(const Foo&) = delete;
    Foo(Foo&&) noexcept;
    Foo& operator=(Foo&&) noexcept;
    void Destroy();
private:
    VkImage image_ = VK_NULL_HANDLE;   // 必须默认 VK_NULL_HANDLE
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
};
```

关键点：
- **每个 vkDestroy* / vkFree* 后立即置 `VK_NULL_HANDLE`**，`Destroy()` 内部加 `!= VK_NULL_HANDLE` 守卫 → 重复 Destroy 安全
- 移动后源对象句柄清零 → 源对象析构安全
- 已有 move-only 类：`Texture`、`Buffer`、`Attachment`（renderer.h/render_pass.h）
- 局部临时对象赋给成员必须 `std::move`（如 `GenerateCubemaps` 中的 cubemap、`textures_.push_back`）
- 启用 move-only 后编译报错即"有漏改的拷贝点"：`push_back(texture)` → `push_back(std::move(texture))`

## Renderer / RenderPass 职责划分（重构后现状）

```
Renderer（管"画框"）：swapchain / commandPool / sync / pipelineCache
  ├── VkRenderPass mainRenderPass_   ← 描述输出（格式/MSAA/clear）
  ├── VkFramebuffer frameBuffers_[N] ← N = swapchain imageCount
  ├── MainRenderPassAttachmentList   ← MSAA color/depth + resolve depth
  ├── VkDescriptorPool               ← 统一分配
  └── 持有 PBRRenderPass_ / SkyBoxRenderPass_（unique_ptr）

PBRRenderPass（管"画什么"）
  ├── 4 个 DescriptorSetLayout：scene(7b) / material(5b) / materialBuffer(1b) / meshDataBuffer(1b)
  ├── DescriptorSets：scene×frameCount + material×materialCount + SSBO×1 + meshSSBO×frameCount
  ├── pipelineLayout + pipelines_（pbr / pbr_double_sided / pbr_alpha_blending）
  ├── Execute(cb, frameIndex)：绑顶点缓冲 → 遍历节点 → 绑管线/descriptor → push constant → draw
  └── 数据源：initInfo_.renderScene_（勿在类定义时静态取 ResourceManager 指针，此时单例未 Init）

SkyBoxRenderPass（管"画什么"）
  ├── 1 个 layout（3 binding：matrices UBO + params UBO + prefilteredCube sampler）
  ├── descriptorSets × frameCount
  ├── 1 个 pipeline（cullMode=NONE, depth=FALSE），无变体、无 push constant
  └── Execute(cb, frameIndex)：bind descriptor + bind pipeline + skybox.Draw()
```

**RenderPassInitInfo 是 Pass 的显式依赖声明**（不要传 Renderer* 给 Pass，会破坏封装）：
```cpp
struct RenderPassInitInfo {
    bool                        multiSamplingEnabled_;
    engine::core::SwapChain*    swapChain_;        // 格式/extent/imageCount
    const RenderScene*          renderScene_;      // RenderItem/环境贴图（不再直接依赖 scene.h）
    VkPipelineCache*            pipelineCache_;
    VkRenderPass*               mainRenderPass_;   // 公共画框句柄
    VkDescriptorPool*           descriptorPool_;
    std::vector<core::Buffer>*  matricesUBOBuffers_;  // 共享 UBO（Renderer 持有，Pass 引用）
    std::vector<core::Buffer>*  paramsUBOBuffers_;
};
```

## Descriptor Pool 统计：poolSizes vs maxSets

**单位不同，不能相加**：
- `poolSizes[type].descriptorCount`：所有 set 里该类型 descriptor 的**总数**（一个 7-binding set 贡献 UBO+2, SAMPLER+5）
- `maxSets`：**实际分配的 VkDescriptorSet 句柄数量**

```cpp
struct DescriptorSetCount { uint32_t uniformBufferCount, imageSamplerCount, storageBufferCount, maxSets; };
// 各 Pass 实现 GetDescriptorSetCount()，Renderer 累加后创建 pool
// maxSets = scene×frameCount + material×materialCount + SSBO×1 + meshSSBO×frameCount（PBR）
// maxSets = frameCount（Skybox）
```

pool 分配偏大无害，偏小则 `vkAllocateDescriptorSets` 失败。

## Window Resize 流程

两条触发路径：
1. `WM_SIZE` → `Application::WindowResize()`（拖拽窗口）
2. `BeginFrame()` acquire 或 `EndFrame()` present 返回 `VK_ERROR_OUT_OF_DATE_KHR`（swapchain 失效）

重建链路（Renderer::WindowResize(w,h)）：
```
vkDeviceWaitIdle
→ DestroyMainFrameBuffer()          // 销毁 framebuffers + MSAA/depth attachments
→ swapChain_.CreateSwapChain(w,h)   // 内部 oldSwapchain 机制自动销毁旧链+旧 view
→ RecreateSyncObjects()             // semaphore 数组大小 = imageCount，可能变化
→ CreatMainFrameBuffer()            // 重建 attachments + framebuffers
```

注意：`VkRenderPass` / Pipeline / DescriptorSet / UBO **不需要**重建（不依赖 extent）。
`frameCount_` 必须在 `Init()` 开头赋值（`Renderer(description)` 构造函数不赋会残留垃圾值 → resize 崩溃）

## 静态析构顺序陷阱

- 全局 `std::unique_ptr<Application> application` 与 `Device::Instance()` 单例的析构顺序不确定
- Device 可能先于 Application 析构 → Application 析构时访问已销毁单例
- **修复**：`WinMain` 末尾显式 `application.reset()`（在 Device 存活时先析构 Application）

## 临时资源清理

- `Device::FlushCommandBuffer(cmd, free)`：`free=true` 才 `vkFreeCommandBuffers`
- 所有一次性 cmdBuffer 必须传 `true`（FullScreenPass、GenerateCubemaps 末尾、Texture2D 三处 Load 函数）
- staging buffer：`Buffer` 有 `~Buffer()` 自动销毁，但必须保证 `stagingBuffer.device` 已赋值（`Device::CreateBuffer` 不填该字段）
- `Device::InitDevice()` 不要重复 `vkCreateCommandPool`（曾存在两次调用导致泄漏）


