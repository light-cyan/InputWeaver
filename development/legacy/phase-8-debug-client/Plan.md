# Phase 8：DebugClient

## 状态

Phase 7 事件契约已完成并归档。本文件是当前 Phase 8 实现计划。

## 目标

实现进程内 `DebugClient`，连接一个 `InputWeaver.exe` 调试管道，解释事件流并向调用方发布只读派生状态。它不是独立进程，不启动执行器，也不包含界面逻辑。

## 派生状态

`DebugClient` 维护四组有界数据：

| 数据 | 归约方式 |
| --- | --- |
| 输入事件 | 按接收顺序保存最近事件。 |
| Pressed state | Clear on `CaptureStarted`; reduce `INIT/Down/Up` by control and origin, classify another `Down` from the same origin as repeated, and classify an `Up` without a corresponding pressed origin as `unmatchedUp`. |
| 规则执行 | `RuleMatched` 建立条目，`ActionStarted` 更新步骤，`ExecutionEnded` 记录结果。 |
| 运行异常 | 保存最近 `RuntimeIssue`，并标记是否影响当前捕获可信度。 |

输入来源和 `Forward/Suppress` 原样保留。`DebugClient` 不接收 `KeyState` 或目标快照。

`INIT` 是临时来源：同一控件的第一个具体来源 `Down` 把它替换为该来源并记为重复，第一个具体来源 `Up` 将它移除。

## 规则执行条目

每个条目保存：

- 触发输入和命中的条件规则。
- 完整编译动作程序。
- 当前指令下标。
- 最近三个 `ActionStarted` 指令下标，最新项排在最后。
- An optional `Completed/Failed/Cancelled` result.

收到第四个步骤时删除最旧步骤。完整动作程序只在 `RuleMatched` 时建立；后续步骤消息只携带标记和下标。结束时清除当前下标并保留最近步骤，界面可据此显示快速执行轨迹，但颜色和闪烁由 TUI 决定。

触发输入通过 `InputSequence` 关联；即使 `RuleMatched` 先于对应的 `InputEvent` 到达，也等待该输入后再形成完整条目。

## 顺序与恢复

- 拒绝会话、捕获周期或标记不匹配的消息。
- `ActionStarted` 和 `ExecutionEnded` 找不到对应标记时，将当前周期标记为不可信。
- 协议顺序断裂、消息损坏或本地容量溢出后停止归约，重新发送 `StartCapture`。
- 收到新的 `CaptureStarted` 后清空事件、按键、执行条目和异常状态，再处理 `INIT`。

## 接口

```text
Connect(ProcessIdentity, debugToken)
StartCapture()
StopCapture()
RequestExecutorStop()
ReadState()
Disconnect()
```

`ReadState()` 返回不可变的本地派生状态；它不是来自 `InputWeaver.exe` 的快照。通知只表示状态版本发生变化。

## 代码位置

- `src/debug/`：协议解码、事件归约、派生状态和平台无关客户端接口。
- `src/platform/windows/debug/`：进程验证、命名管道客户端、等待和取消。
- `tests/debug/`：协议、归约、容量和假服务端测试。

## 实现步骤

1. 实现连接、握手、读取、取消和关闭。
2. 实现协议顺序检查与事件解码。
3. 实现输入历史和按键状态归约。
4. 实现规则条目、当前步骤、最近三个步骤和结束结果归约。
5. 实现异常归约、捕获重启和不可变 `ReadState()` 发布。
6. 使用假服务端和真实 Phase 7 执行器验证。

## 完成条件

- `INIT`、普通按下、重复和释放可以仅由事件流得到确定按键状态。
- 交错执行的规则由标记正确隔离，每条执行只保留最近三个步骤。
- 完整动作程序中的 `Wait`、跳转和循环指令可以按下标定位。
- 断开、损坏、乱序、未知标记和容量溢出都有确定结果。
- 所有历史、条目、消息和等待都有固定上限。
- App 或 TUI 类型不进入 `DebugClient`，并在本目录写入 `Verification.md`。
