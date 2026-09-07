# Codex 项目入口

本文件用于新任务或上下文重置后的快速接管。它只保存稳定的协作规则；当前进度统一记录在 `docs/PROJECT_STATE.md`。

## 新任务启动顺序

1. 运行 `git status --short --branch` 和 `git log --oneline --decorate -10`，确认代码事实。
2. 完整阅读 `docs/PROJECT_STATE.md`，获取当前阶段、对象所有权、已知遗留项和下一步。
3. 按需阅读 `.github/skills/vulkan-pbr-project/SKILL.md` 中的项目结构、编码规范和 Vulkan 约束。
4. Skill 中的历史“当前状态”可能滞后；发生冲突时，以代码、Git 和 `docs/PROJECT_STATE.md` 为准。
5. 修改前检查实际调用链，不用路线表代替代码事实。

## 协作方式

- 默认使用中文交流。
- 讨论架构时按“理念 → 目标 → 细节 → 验收”展开，一步一步推进。
- 用户说“看看、分析、解释”时只读检查；明确要求修改后再写文件。
- 保留用户未提交修改；发现范围外改动时不覆盖、不回滚。
- 只实现已确认的需求，不自行添加额外需求，不因“以后可能用到”预先增加策略、抽象或兼容分支。发现确有新增需求或需要扩大范围时，先说明原因和影响，与用户核对并获得确认后再写代码。
- Commit、Tag、Push 是三个独立动作，分别等待明确授权。
- 阶段完成前先构建、运行并检查 Validation/日志，再交由用户确认。
- 不把开发阶段和重构过程写进公开 README；README 只介绍产品、构建和用户可见限制。

## 文件与临时数据

- 项目内容只写入当前仓库。
- 临时补丁放系统临时目录，任务结束立即删除。
- 不在 `C:\Users\86178\Documents\ChatGPT\vulkan engine` 中转项目文件。
- 不删除资产、第三方库、平台目录或 Git 历史，除非用户明确确认精确范围。

## 构建与验证

```bash
cmake --build --preset gcc-ninja
```

运行目标：`build/gcc/bin/Vulkan-pbr.exe`。

最低验收：

- 编译和链接成功。
- Validation Layer 无新增 error。
- 模型、天空盒和 PBR 正常显示。
- 退出时无 descriptor `unknown set`、double-free 或对象生命周期错误。
- `git diff --check` 通过。

`external/gli` 的 deprecated enum warning 是既有第三方 warning，不作为阶段阻塞项。

## 状态文档维护

每个阶段正式完成后，只更新 `docs/PROJECT_STATE.md` 中的以下内容：

- 最后稳定提交和 tag。
- 已完成阶段摘要。
- 当前真实对象所有权。
- 已知遗留项。
