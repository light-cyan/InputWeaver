# Phase 7 验证

## 结果

2026-08-26，Phase 7 实现验证通过。

## 已验证内容

- `InputWeaver.exe` 仅在指定 `--debug-session` 时创建调试服务、队列和管道线程。
- 调试流包含 `CaptureStarted`、`InputEvent`、`RuleMatched`、`ActionStarted`、`ExecutionEnded` 和 `RuntimeIssue`，不包含按键状态或快照。
- 输入来源可区分 `PHY`、`ECHO`、`EXT` 和 `INIT`；最终输入决定可区分 `Forward` 与 `Suppress`。
- 捕获开始时先发送 `CaptureStarted` 和当前按下控件的 `INIT Down`，随后发送普通输入事件。
- 服务端按生产顺序发送后续事件；`RuleMatched` 可以先于关联的 `InputEvent`，由客户端使用 `InputSequence` 关联。
- 规则事件携带条件规则和完整编译动作序列；动作开始与执行结束通过调试标记关联。
- 调试热路径仅写入固定大小的有界记录；协议编码和动作序列展开位于管道线程。
- 队列溢出不会阻塞输入处理，会终止当前捕获并报告 `DebugStreamOverflow`；客户端可以开始新的捕获周期。
- 命名管道只接受本机同一用户的单个客户端，并支持停止捕获和请求执行器退出。
- 原有任务完成与取消指标语义保持不变。

## 验证命令

| 命令 | 结果 |
| --- | --- |
| `script\build.bat` | 通过。 |
| `script\test.bat` | Windows 平台、编译程序契约、运行时核心、Windows 运行时适配器、调试协议、Windows 调试服务和运行时 CLI 测试全部通过。 |
| `script\analyze_all.bat` | 通过；新增调试实现另以同等严格参数执行 `-fanalyzer -fsyntax-only` 并通过。 |
| `script\audit_dependencies.bat` | 通过；新增调试实现的直接依赖另以 `g++ -MM` 核对并符合模块边界。 |
| `git diff --check` | 通过。 |

## 边界确认

`development/CompilerRuntimeCompletionHandoff.md` 未修改。Phase 7 不包含 `DebugClient`、App、TUI 或进程管理。
