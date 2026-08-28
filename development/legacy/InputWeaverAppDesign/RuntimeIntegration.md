# 现有组件集成与必要源码改动

## 集成边界

`InputWeaverTUI.exe` 只编排现有的编译器、执行器和 `DebugClient`，不重新实现编译、运行或调试归约逻辑。

```text
导入：InputWeaverTUI.exe -> InputWeaverCompiler.exe compile -> .weavec
转储：InputWeaverTUI.exe -> InputWeaverCompiler.exe dump -> dump text
运行：InputWeaverTUI.exe -> InputWeaver.exe --program <file.weavec> ...
调试：InputWeaverTUI.exe -> DebugClient -> InputWeaver.exe debug pipe
```

## 编译器调用

导入时调用 `InputWeaverCompiler.exe compile <source.weave> <temporary.weavec>`，成功后在源文件仍可访问时调用 `InputWeaverCompiler.exe dump <source.weave>`。`compile` 已经完成词法、语法、语义、类型和结构验证，因此 App 不增加另一套验证器。

编译器的标准输出和标准错误重定向到 Console。任一步失败都终止本次导入，临时文件被清理，现有目录与条目不变。

编译器、`.weavec` 格式和编译结果内容不需要修改。

## 执行器参数映射

每次启动都包含 `--program <managed.weavec>`，其余参数由条目配置和本次运行选项生成。

| App 状态 | 执行器参数 |
| --- | --- |
| Target 为 Compiled | 不传目标覆盖参数。 |
| Target 为 Executable | 传 `--target <selector>`。 |
| Target 为 Global | 传 `--target-global`。 |
| Logging 开启 | 传 `--log <jsonl-path>`；如需输入追踪，同时传 `--trace-input`。 |
| `T` 开启 | 生成一次性令牌并传 `--debug-session <token>`。 |
| `S` 开启 | 传新增的 `--dry-run`。 |
| `P` 开启 | 传 `--allow-exec`。 |

运行选项只影响下一次启动；执行器成功启动后立即清除 `T`、`S`、`P`。

## 执行器生命周期

App 为每个条目最多持有一个受管执行器。不同条目可以同时普通运行；所有条目合计最多有一个调试执行器。

启动新的调试执行器时，App 先通过当前 `DebugClient` 发送 `RequestExecutorStop`，等待当前调试执行器退出并断开连接，然后启动新执行器、连接新调试管道并发送 `StartCapture`。离开 Debug 页面不停止捕获，也不停止执行器。

普通执行器停止使用独立进程组和控制台控制事件请求有序退出；超时后的错误进入 Console。TUI 退出时依次停止所有由它启动且仍在运行的执行器。

## `--dry-run`

`--dry-run` 是无注入模拟运行模式。程序仍加载 `.weavec`，仍建立目标监听、输入状态、规则匹配、变量、`PAUSE`、动作任务、调试事件和诊断输出，但不对系统产生输入注入或外部进程启动效果。

无注入模式下的具体规则如下：

- 不调用 `SendInput`，但仍执行动作调度并形成完整的 Action Execution 调试轨迹。
- 物理输入始终放行；调试事件中的 `DROP` 表示正常模式下本应抑制，便于观察规则决策。
- `EXEC` 指令不调用 `CreateProcess`，但按成功完成继续执行后续动作。
- 含 `EXEC` 的程序仍需显式传入 `--allow-exec`，因此 dry-run 不绕过程序原有的执行权限门槛。
- 编译的退出规则仍然生效，可以正常结束 dry-run 执行器。

实现边界包括运行时 CLI 的 `dryRun` 选项、`WindowsExecutorOptions` 的传递、Windows 输出与进程启动端口的无效果实现，以及 Windows hook 对物理事件的最终放行。平台无关 `ProgramRuntime` 的规则匹配与动作语义保持不变。

## 调试墙钟时间

调试协议版本 2 在每个捕获周期建立单调时间与 UTC 墙钟时间的锚点，使 Debug 页可以显示 `HH:MM:SS.mmm`。

`CaptureStarted` 携带 UTC Unix 毫秒锚点，其消息头中的 `captureTimeNanoseconds` 作为同一时刻的单调时间锚点。客户端按下式换算每条事件的 UTC Unix 毫秒：

```text
eventUnixMilliseconds = captureStartUnixMilliseconds
                      + (eventCaptureNanoseconds - captureStartNanoseconds) / 1,000,000
```

`DebugClient` 分别新增 `DebugInputEvent::captureUnixTimeMilliseconds`、`DebugRuleExecution::matchedUnixTimeMilliseconds` 和 `DebugRuntimeIssue::captureUnixTimeMilliseconds`，同时保留现有单调纳秒字段。TUI 把新增字段按本地时区格式化为 `HH:MM:SS.mmm`。这样既保留单调时钟用于顺序和间隔，又得到可用于现场排查的真实时间。

这项改动涉及调试协议编解码、Windows 调试服务端、`DebugClient` 派生状态及相应测试，不改变 `.weavec`，也不改变输入事件、规则匹配和动作执行的已有含义。

## 现有源码扩展结果

当前 App 设计依赖的两项源码扩展为：

1. 为 `InputWeaver.exe` 增加 `--dry-run` 及其无效果输出边界。
2. 为调试协议和 `DebugClient` 增加可换算真实时间的捕获锚点。

`StartCapture`、`StopCapture`、`RequestExecutorStop`、最近三个动作指令、Action Execution 结果、输入事件、按下状态和 `DebugInputEvent::unmatchedUp` 均已经具备，TUI 直接消费这些能力。App 本身及 TUI 页面属于新增组件，不计入对现有三个组件的功能修改。
