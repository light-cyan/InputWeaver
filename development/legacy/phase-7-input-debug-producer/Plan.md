# Phase 7：输入调试事件生产

## 状态

Phase 7 已完成并归档。本阶段只修改 `InputWeaver.exe`、共享协议类型和 Windows 调试管道服务端；验证结果记录在 `Verification.md`。

## 目标

`InputWeaver.exe` 仅发送追加型调试事件，不维护或发送按键状态、任务视图、历史快照和界面数据。没有 `--debug-session` 时不创建调试队列、管道或线程。

```text
InputWeaver.exe --program <file.weavec> [existing options] --debug-session <opaque-token>
```

## 事件

| 事件 | 内容 |
| --- | --- |
| `CaptureStarted` | 新的捕获周期。 |
| `InputEvent` | 控件、`Down/Up`、来源和最终 `Forward/Suppress`；初始按下状态使用 `INIT Down`，处理结果为空。 |
| `RuleMatched` | 调试标记、触发输入、命中的条件规则和完整编译动作序列。 |
| `ActionStarted` | 调试标记和即将执行的编译指令下标。 |
| `ExecutionEnded` | 调试标记和 `Completed/Failed/Cancelled`。 |
| `RuntimeIssue` | 容量不足、跳过、超时、执行故障和调试流丢失等异常。 |

`RuleMatched` 的标记只用于关联后续事件，不是运行时任务编号。协议不发送 `KeyState`、`OutputEvent`、状态增量、计数器更新或完整快照。

## 输入来源

| 内部类型 | 显示标签 | 判定 |
| --- | --- | --- |
| `PhysicalCandidate` | `PHY` | Windows 注入标志未设置。 |
| `CurrentInstanceInjected` | `ECHO` | 已注入并携带当前实例的 `selfTag`。 |
| `ExternalInjected` | `EXT` | 已注入但不携带当前实例的 `selfTag`。 |
| `InitialSample` | `INIT` | 捕获开始时采样到的已按下控件。 |

`ExternalInjected` 包含其他 InputWeaver 实例和其他模拟输入来源。`CurrentInstanceInjected` 是当前实例注入来源的内部名称；输入来源不使用数字等级。

## 最小接入点

| 位置 | 修改 |
| --- | --- |
| Windows 捕获开始 | 建立 `CaptureEpoch`，发送 `CaptureStarted`，随后为当前按下控件发送 `INIT Down`。 |
| `WindowsProgramRuntimeSession::HandleInput` | 在最终决定产生后发送 `InputEvent`，保留钩子已经判断的来源。 |
| 规则调度 | 把调试标记、触发输入和规则引用随 `WorkItem` 传递；任务真正进入 `Ready` 前发送一次 `RuleMatched`。 |
| `RunTaskSlice` | 每次取出编译指令并执行前发送 `ActionStarted`；`Wait`、跳转和循环指令均包含在内。 |
| `FinishTask` | 清理完成后发送一次 `ExecutionEnded`。 |
| `ProgramRuntime::PublishDiagnostic` | 只把异常诊断转换为固定大小的 `RuntimeIssue` 记录。 |

在任务真正被接受前不发送 `RuleMatched`；容量拒绝等情况只发送 `RuntimeIssue`，避免产生不存在的执行条目。

## 运行时最小修改

1. 增加可选的无阻塞 `DebugEventPort`；禁用调试时调用路径为空。
2. Windows 会话为输入分配 `InputSequence`，并把它作为可选关联传入运行时。
3. `WorkItem` 携带调试标记、触发输入和规则引用；`TaskInstance` 只保留后续步骤和结束事件需要的调试标记。
4. 把 `FinishTask(..., bool cancelled)` 改为明确的 `Completed/Failed/Cancelled` 结果，并在延迟清理期间保留结果。
5. 钩子和任务线程只写固定大小的内部记录；管道线程再从不可变编译程序读取规则和动作序列并编码 `RuleMatched`。

## 捕获与丢失

`StartCapture` 必须在钩子线程上建立边界，保证 `CaptureStarted` 和全部 `INIT Down` 早于本周期普通输入事件。协议顺序断裂后客户端重新开始捕获；执行器创建新周期并重新发送 `INIT`，不发送快照。

## 代码位置

- `src/debug/`：协议值类型、帧编解码和固定容量记录。
- `src/runtime/`：可选发布端口和三处任务事件接入。
- `src/platform/windows/runtime/`：输入来源、最终决定、初始采样和诊断接入。
- `src/platform/windows/debug/`：有界队列和命名管道服务端。

## 实现步骤

1. 实现协议类型、编解码和固定样例。
2. 实现调试选项、服务端、捕获边界和有界事件队列。
3. 接入 `InputEvent`、`INIT` 和 `RuntimeIssue`。
4. 接入规则命中、动作开始和三种结束结果。
5. 使用假客户端验证顺序、溢出、重启捕获和关闭。

## 完成条件

- 普通执行模式的输入决定、输出行为和诊断保持不变。
- 调试热路径无等待、无分配；队列满时输入处理继续并报告流丢失。
- 所有字段、数组、消息和队列都有固定上限。
- 构建和相关验证通过，并在本目录写入 `Verification.md`。

Phase 7 不实现 `DebugClient`、App、TUI 或进程管理。
