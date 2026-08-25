# Phase 9：App 部分提案

## 状态

本文件只记录当前想到的 App 边界，不是实现计划。程序模型、生命周期和 App/TUI 接口仍需单独设计。

## 已确定的产品行为

- 用户选择“程序”进入 Debug，不选择 PID。
- 可以同时运行多个普通 `InputWeaver.exe`。
- 产品最多管理一个启用调试的 `InputWeaver.exe`。
- 开始调试另一个程序时，先关闭当前调试执行器，再启动新的调试执行器。

## 当前结构提案

App 可能作为 `InputWeaverTUI.exe` 内的控制层存在。它负责程序列表、进程启动、进程退出、唯一调试会话和 `DebugClient`；TUI 只提交意图并显示 App 状态。

```text
TUI -> App -> launch InputWeaver.exe --debug-session <token>
           -> DebugClient -> InputWeaver.exe debug pipe
```

这个模块位置仍是提案。

## 当前命令提案

```text
StartDebug(ProgramId)
StopDebug()
StartOrdinary(ProgramId)
StopOrdinary(ExecutorInstanceId)
ReadDebugState()
```

PID 只作为进程信息和校验信息。

## 当前替换流程提案

1. 校验所选程序。
2. 如果另一个调试执行器存在，先请求有序退出并等待进程结束。
3. 创建新令牌并启动调试执行器。
4. 创建 `DebugClient` 并连接管道。
5. 收到 `CaptureStarted` 后进入正在调试状态，后续 `INIT` 事件由 `DebugClient` 归约。

## 当前数据提案

App 状态可能包含程序列表、执行器列表、唯一调试会话、进程状态、连接状态和 `DebugClient` 的派生状态。App 不重复归约调试事件。

## 待设计问题

- 程序定义包含什么，如何保存和编辑。
- App 是否确实位于 `InputWeaverTUI.exe` 内。
- “最多一个调试执行器”的范围是一个 App、一个用户会话还是整台机器。
- 进程无法正常退出时如何处理。
- 离开 Debug 页面是否停止捕获或停止执行器。
- App 快照、通知和并发命令采用什么模型。

这些问题解决后再编写 Phase 9 `Plan.md`。
