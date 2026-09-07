# 输入调试开发路线图

## 状态

Input debug development started from commit `3aed66f2d424384dc7c7c52dbffe19e1cd53bfbd` or a successor. Phase 7 and Phase 8 are complete and archived; Phase 9 App and Phase 10 TUI have a unified current product design.

## 简化结构

```text
InputWeaver.exe --debug-session <token>
    -> 调试管道
    -> 进程内 DebugClient
    -> App
    -> TUI
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
| Phase 8: Debug Client | Complete and archived | Connects to the debug pipe and reduces the event stream into read-only state. |
| Phase 9: App | 设计完成 | 程序模型、持久化、进程生命周期和单一调试会话。 |
| Phase 10: TUI | 设计完成 | Programs、Console、Debug 页面及终端交互。 |

## 文档入口

- Phase 7：`development/legacy/phase-7-input-debug-producer/Plan.md`、`TargetDataContract.md` 与 `Verification.md`。
- Phase 8: `development/legacy/phase-8-debug-client/Plan.md` and `Verification.md`.
- App 与 TUI 统一设计：`development/InputWeaverAppDesign/README.md`。
- Phase 9：`development/phase-9-app/Proposal.md`。
- Phase 10：`development/phase-10-tui/Proposal.md`。

Phase 7 不依赖后续模块。Phase 8 只依赖 Phase 7 协议。App 与 TUI 的当前产品行为以统一设计目录为准。
