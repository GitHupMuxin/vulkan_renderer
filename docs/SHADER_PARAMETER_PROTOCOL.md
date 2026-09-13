# Shader 参数公共协议

最后更新：2026-09-14

## 当前认识

Shader 参数系统连接两套彼此独立的接口：

```text
Scene / Camera 等 CPU 语义数据
  → C++ 协议结构与数据来源映射
  → Uniform Buffer / Descriptor
  → GLSL 参数块
```

Vulkan 不会自动同步 C++ 结构与 GLSL 参数块。Stage 7C 已建立固定公共协议，但跨语言同步仍由人工维护。

## 当前代码事实

- MainCamera 和 SceneParam 的 Buffer 由 RenderResourceRegistry 持有，每个 frame slot 一份。
- CPU 格式集中在 `engine/render/config/shader_protocol.h`：`CameraUniformData` 和 `SceneParamUniformData`，并通过 static_assert 固定字段 offset。
- GLSL 格式集中在 `data/shaders/includes/camera.glsl` 和 `scene_params.glsl`，相关 Shader 通过 include 使用。
- Descriptor 的 set、binding、type、count 和 stage 以 `engine/core/config/schema.h` 为准；默认管线配置引用 schema，并将资源对应到 binding，Model 同样使用 schema。
- Renderer 在 `SetRenderScene()` 中调用 `UpdateCameraUniformData()`、`UpdateSceneParamUniformData()`，按协议组装数据并 memcpy 到当前帧映射的 UBO。
- Camera 与用户参数来自 RenderScene；预滤波 mip 层数来自 Registry 已注册的 EnvironmentCubeMap，不再从 Scene 重复保存、传递。
- Set 0 在准备阶段指向对应 Buffer；每帧 memcpy 改的是 Buffer 内容，不需要因此重新写 descriptor。当前 HDR 图像切换引起的 Set 0 重写是另一件事。

## 当前固定布局

| 协议 | 字段与字节 offset |
|---|---|
| Camera | projection 0、model 64、view 128、camPos 192 |
| SceneParam | lightDir 0、exposure 16、gamma 20、prefilteredCubeMipLevels 24、scaleIBLAmbient 28、debugViewInputs 32、debugViewEquation 36、debugBSDFType 40 |

CPU 当前分配分别为 204 和 44 字节。GLSL 的字段类型、顺序和 offset 必须与其匹配；C++ static_assert 只检查 CPU 侧，不能验证 GLSL。

EnvironmentCubeMap 生成预滤波纹理时，根据分辨率计算 mip 数并保存。目前预滤波分辨率 512，得到 10 层；Renderer 通过 Registry 的 getter 取得该值写入 SceneParam。按用户选择保留这条来源，没有改用 Shader 的 textureQueryLevels()。

## 已确认的遗留问题

固定协议已经收口，但尚无自动同步或通用自定义参数机制。以下四种关系仍需分别保证：

1. 字段语义：CPU 数据应填写到哪个参数。
2. 字节格式：字段类型、顺序、offset、对齐和总大小。
3. 绑定位置：descriptor set、binding 和 shader stage。
4. 使用关系：哪个 RenderPass 订阅哪一组公共参数。

运行时资源绑定由描述和 Registry 处理；这不等于自动解决字段格式和数据语义。Renderer 仍显式组装当前两份公共参数，不能将 Stage 7C 描述为已实现通用参数提供系统。

## 自定义参数与 Shader 反射（待讨论、未实现）

目标是让用户新增普通 Shader 参数时，不必同步修改 C++ 上传结构体。可研究通过 SPIR-V Reflection 获取参数名称、类型、set/binding、成员 offset、大小以及数组和矩阵步长，再由公共参数层按名称或 ID 接收数值，并按实际布局写入 Buffer。

- 此处需要的是 Shader 反射，不要求先建立完整的 C++ 反射系统。
- 反射解决“数据怎么放”，不会推断“数据从哪来”：Camera 来自相机、exposure 来自用户设置，仍需明确的数据提供与赋值逻辑。
- Camera 可以继续采用固定公共协议，自定义 param 则考虑反射驱动，两者可以共存。
- 参数值的保存、赋值接口和上传仍由公共层负责，不能只实现反射就认为参数系统完成。

后续还可讨论代码生成与反射的取舍、参数语义组装的归属，以及 MainCamera 中 model 字段是否迁移到逐对象协议。这里只记录遗留方向，用户明确提出后再展开，不属于 7C 收尾工作。
