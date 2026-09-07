# Phase 7：目标调试事件契约

## 范围

本契约连接一个启用调试的 `InputWeaver.exe` 和一个本地 `DebugClient`。执行器只发送事件流，客户端负责解释并建立可读状态。

## 标识

| 标识 | 含义 |
| --- | --- |
| `TargetSessionId` | 一个调试执行器和管道端点。 |
| `CaptureEpoch` | 一次捕获周期。 |
| `ProtocolSequence` | 当前连接中的消息顺序。 |
| `InputSequence` | 当前捕获周期中的输入顺序。 |
| `ExecutionMarker` | 一次被接受的规则执行；只用于关联消息。 |

PID 和管道令牌不进入数据消息。每条消息包含固定魔数、协议版本、类型、负载长度、会话、捕获周期、顺序和单调时间；所有负载都有固定上限。

## 消息

### `CaptureStarted`

宣布新的 `CaptureEpoch`。客户端必须先清空上一周期的派生状态，随后处理本周期的 `INIT` 输入事件。

### `InputEvent`

包含 `InputSequence`、控件、`Down/Up`、`InputOrigin` 和最终 `Forward/Suppress`。`InitialSample` 只与 `Down` 一起使用，其处理结果为 `NotApplicable`。

| `InputOrigin` | 显示 | 含义 |
| --- | --- | --- |
| `PhysicalCandidate` | `PHY` | Windows 未标记为注入。 |
| `CurrentInstanceInjected` | `ECHO` | 当前实例注入后再次观察到。 |
| `ExternalInjected` | `EXT` | 当前实例之外产生的注入。 |
| `InitialSample` | `INIT` | 捕获开始时采样到的按下状态。 |

`Forward` 只表示当前实例没有吞掉事件。重复按下由客户端根据事件流判断，执行器不增加另一种输入消息。

### `RuleMatched`

包含 `ExecutionMarker`、`TriggerInputSequence`、命中的事件规则、条件程序和完整编译动作程序。动作程序保留 `Wait`、跳转、循环和 `End` 等编译指令。

同一标记只发送一次 `RuleMatched`。标记是调试关联值，不代表运行时原生任务编号。

### `ActionStarted`

包含 `ExecutionMarker` 和动作程序内的指令下标，在该指令执行前发送。循环和跳转可以使相同或较小的下标再次出现。

### `ExecutionEnded`

包含 `ExecutionMarker` 和以下一种结果：

- `Completed`：正常到达动作程序结尾。
- `Failed`：动作或表达式执行失败。
- `Cancelled`：执行因失效、停止或运行时取消而结束。

一个已宣布的标记最终只发送一次结束事件。

### `RuntimeIssue`

包含异常种类及定位异常所需的最少上下文。正常条件不命中、正常等待和普通状态变化不产生该事件。

## 捕获顺序

1. `StartCapture` 建立新周期并发送 `CaptureStarted`。
2. 执行器为捕获边界上已按下的控件发送若干 `INIT Down`。
3. 执行器持续发送普通输入、规则执行和异常事件。
4. `StopCapture` 停止发送并清空执行器内的待发调试记录。

协议顺序断裂、消息损坏或接收溢出后，客户端停止解释当前周期并重新执行 `StartCapture`。新周期重新发送 `INIT`；协议没有快照或状态增量。

## 客户端命令

| 命令 | 作用 |
| --- | --- |
| `Hello` | 协商协议版本。 |
| `StartCapture` | 开始新的捕获周期。 |
| `StopCapture` | 停止当前捕获。 |
| `RequestExecutorStop` | 请求执行器有序退出。 |

## 边界

管道只接受同一用户的一个本地客户端。捕获内容和令牌不得写入普通日志、标准输出或 JSONL 诊断。消息不得包含指针、句柄、C++ 对象布局、界面布局、颜色或动画信息。
