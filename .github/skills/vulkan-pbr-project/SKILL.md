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
│   ├── core/                   ← 设备/交换链/工具/宏（基础层）
│   ├── resource/               ← 资源层：纹理 + glTF 模型加载
│   ├── scene/                  ← 场景层：Camera
│   ├── render/                 ← 渲染层：Renderer + Pass 框架
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

### include 路径
所有 `#include` 从项目根（`vulkan_pbr/`）写起，搜索根由 `target_include_directories(engine PUBLIC ..)` 保证。

```cpp
#include "engine/core/device.h"        // ✅ 正确
#include "engine/resource/model.h"     // ✅ 正确
#include "app/application/application.h" // ✅ 正确
```

### 命名空间
- `engine::core` — Vulkan 设备、交换链、Buffer 工具
- `engine::resource` — 纹理、模型资源
- `engine::resource::vkglTF` — glTF 模型解析命名空间
- `engine::scene` — 相机等场景数据
- `engine::render` — Pass、Renderer 框架
- `engine::platform` — 窗口抽象
- `engine::utils` — Logger 单例
- `app` — 应用层入口

### 窗口抽象（PIMPL 模式）
`engine/platform/window.h` 使用 PIMPL 隐藏 Win32 句柄：
```cpp
class Window {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    void Init(void* hinstance, void* wndproc);
    void* NativeHandle() const;
};
```

### 渲染框架（Pass 模式）
```
Renderer（帧循环骨架）
  ├── swapchain / commandPool / syncObjects
  └── std::vector<std::unique_ptr<RenderPass>> passes_

RenderPass（抽象基类）
  ├── virtual void Setup(const RenderPassInitInfo& info)
  ├── virtual void Execute(VkCommandBuffer cmd, uint32_t frameIndex)
  └── virtual void Cleanup(VkDevice device)
```

Renderer 不关心 Pass 具体画什么，只按序执行。

### 日志系统
全局单例 `engine::utils::Logger`，宏封装：
```cpp
LOG_INFO(msg)    // 流式写法，如 LOG_INFO("Failed: " << res)
LOG_WARN(msg)
LOG_ERROR(msg)
LOG_FATAL(msg)   // 自动 abort
```

## 构建系统
- 构建工具：CMake + Ninja
- 预设：`gcc-ninja`（GCC + Ninja Debug）
- 编译器：MinGW g++
- `engine/` 输出为 `libengine.a`（静态库）
- `app/` 输出为 `Vulkan-pbr.exe`
- `add_subdirectory(engine)` + `add_subdirectory(app)`
- `file(GLOB ...)` 收集各模块源文件
- `target_include_directories(engine PUBLIC ..)` 设置项目根搜索路径

## CMake 配置参考
```cmake
# engine/CMakeLists.txt
file(GLOB ENGINE_SRC "core/*.cpp" "resource/*.cpp" "scene/*.cpp" "render/*.cpp" "platform/*.cpp" "utils/*.cpp" "../external/imgui/*.cpp")
add_library(engine STATIC ${ENGINE_SRC} ${ENGINE_HEADERS} ${BASISU_SOURCES})
target_include_directories(engine PUBLIC ..)
target_link_libraries(engine PUBLIC ${Vulkan_LIBRARY})
```

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
  └── 数据源：initInfo_.scene->model_（勿在类定义时静态取 ResourceManager 指针，此时单例未 Init）

SkyBoxRenderPass（管"画什么"）
  ├── 1 个 layout（3 binding：matrices UBO + params UBO + prefilteredCube sampler）
  ├── descriptorSets × frameCount
  ├── 1 个 pipeline（cullMode=NONE, depth=FALSE），无变体、无 push constant
  └── Execute(cb, frameIndex)：bind descriptor + bind pipeline + skybox.Draw()
```

**RenderPassInitInfo 是 Pass 的显式依赖声明**（不要传 Renderer* 给 Pass，会破坏封装）：
```cpp
struct RenderPassInitInfo {
    SwapChain* swapChain_;         // 格式/extent/imageCount
    Scene* scene;                  // model/cubeMap/UBO
    VkPipelineCache pipelineCache_;
    VkRenderPass mainRenderPass_;  // 公共画框句柄
    VkDescriptorPool descriptorPool_;
    bool multiSamplingEnabled_;
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

## 日志系统
全局单例 `engine::utils::Logger`，宏封装：
```cpp
LOG_INFO(msg)    // 流式写法，如 LOG_INFO("Failed: " << res)
LOG_WARN(msg)
LOG_ERROR(msg)
LOG_FATAL(msg)   // 自动 abort
```
