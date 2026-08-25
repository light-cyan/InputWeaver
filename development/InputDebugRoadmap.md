# 输入调试开发路线图

## 状态

输入调试从提交 `3aed66f2d424384dc7c7c52dbffe19e1cd53bfbd` 或其后继版本开始开发。Phase 7 已完成并归档，Phase 8 是当前实现阶段，Phase 9 App 与 Phase 10 TUI 目前只保存部分提案。

## 简化结构

```text
InputWeaver.exe --debug-session <token>
    -> 调试管道
    -> 进程内 DebugClient
    -> App（提案）
    -> TUI（提案）
```

App 负责启动调试执行器并持有 `DebugClient`。`InputWeaver.exe` 只发送调试事件；`DebugClient` 解释事件流并形成只读派生状态，不形成独立进程。

## 来源命名

| 内部名称 | 界面标签 | 含义 |
| --- | --- | --- |
| `PhysicalCandidate` | `PHY` | 当前实例观察到的非注入事件。 |
| `CurrentInstanceInjected` | `ECHO` | 当前实例生成的输入再次被当前实例观察到。 |
| `ExternalInjected` | `EXT` | 当前实例之外产生的注入输入，包括其他 InputWeaver 实例。 |
| `InitialSample` | `INIT` | 开始捕获时为已经按下的控件生成的初始 `Down` 事件。 |

事件不再使用数字等级。来源、转换和处理结果分别保存，互不推导。

## 阶段划分

| 阶段 | 状态 | 内容 |
| --- | --- | --- |
| Phase 7: Input Debug Producer | 完成并归档 | `InputWeaver.exe` 发布有界的追加型调试事件。 |
| Phase 8: Debug Client | 当前实现阶段 | 连接调试管道并把事件流归约为只读状态。 |
| Phase 9: App | 部分提案 | 设计程序模型、进程生命周期和单一调试会话。 |
| Phase 10: TUI | 部分提案 | 设计终端交互和显示方式。 |

## 文档入口

- Phase 7：`development/legacy/phase-7-input-debug-producer/Plan.md`、`TargetDataContract.md` 与 `Verification.md`。
- Phase 8：`development/phase-8-debug-client/Plan.md`。
- Phase 9：`development/phase-9-app/Proposal.md`。
- Phase 10：`development/phase-10-tui/Proposal.md`。

Phase 7 不依赖后续模块。Phase 8 只依赖 Phase 7 协议。Phase 9 和 Phase 10 在形成各自的 `Plan.md` 前不进入实现。
