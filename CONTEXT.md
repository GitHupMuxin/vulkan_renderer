# 项目上下文导航

本文件提供代码与文档入口；当前进度、稳定标记和已知边界统一以 [项目状态](docs/PROJECT_STATE.md) 为准。

## 接手顺序

1. 阅读 [项目协作规则](AGENTS.md) 和项目状态中引用的全局协作规则。
2. 检查 `git status --short --branch` 与 `git log --oneline --decorate -10`，区分稳定提交和未提交修改。
3. 阅读 [项目状态](docs/PROJECT_STATE.md)，再沿实际调用链核对本次任务涉及的代码。

## 代码入口

| 入口 | 职责 |
|---|---|
| [Application](app/application/application.cpp) | 组织初始化、场景提取和每帧调用 |
| [默认管线](engine/render/config/default_render_pipeline.h) | 配置 Pass、显式依赖和输出节点 |
| [RenderContext](engine/render/render_context.cpp) | 按配置创建 Pass 并建立 FrameGraph |
| [FrameGraph](engine/render/frame_graph.cpp) | 编译依赖、从输出保留所需节点并生成执行计划 |
| [Renderer](engine/render/renderer.cpp) | 准备资源、录制命令、同步、提交与呈现 |

默认渲染顺序：Skybox → PBR → ToneMapping；UI 在图外叠加到 BackBuffer。

## 构建入口

在仓库根目录执行 `cmake --build --preset gcc-ninja`。
环境准备见 [README](README.md)，运行与验收要求见 [AGENTS.md](AGENTS.md)。
