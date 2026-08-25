# Phase 8 验证

## 结果

2026-08-26，Phase 8 `DebugClient` 实现验证通过。

## 已验证内容

- `inputweaver::debug::DebugClient` 提供进程内 `Connect`、`StartCapture`、`StopCapture`、`RequestExecutorStop`、`ReadState` 和 `Disconnect` 接口，`WindowsDebugClient` 完成进程与管道服务端 PID 校验、协议握手、可取消读写、断开和重新连接。
- `ReadState()` 发布版本化的 `shared_ptr<const DebugClientState>` 本地不可变快照，包含有界输入历史、来源感知的按下状态、规则执行条目、运行异常历史和当前捕获可信状态。
- Input origins retain `PHY`, `ECHO`, `EXT`, and `INIT`; verification covers ordinary Down, repeated Down, matched Up, `unmatchedUp` without a corresponding pressed origin, the first concrete Down replacing `INIT` as a repeat, and the first concrete Up removing `INIT`.
- `RuleMatched` 可以先于关联输入到达；多个 marker 可交错更新，执行条目保留触发输入、条件程序、完整动作程序、当前指令、最近三个步骤及 `Completed`、`Failed`、`Cancelled` 结果。
- 客户端严格校验 `targetSessionId`、`captureEpoch` 和 `protocolSequence`；损坏帧、序列断裂、未知 marker、调试流丢失、不一致状态和本地容量溢出会终止当前周期归约并请求新的捕获，新的 `CaptureStarted` 会清空旧周期状态并恢复可信状态。
- 输入历史、按下状态、执行历史、待关联执行、异常历史、已存指令和接收帧均受固定容量约束；最近输入、最近异常和已结束执行使用有界保留策略。
- 假服务端测试覆盖握手、命令、损坏帧恢复、阻塞读取取消、断开和重连；现有 `WindowsDebugServer` 集成测试覆盖真实握手、`INIT`、提前到达的 `RuleMatched`、执行关联、停止捕获和请求执行器停止。

## 验证命令

| 命令 | 结果 |
| --- | --- |
| `script\build_runtime_tests.bat` | 通过；新增 `DebugClientTests.exe`、`WindowsDebugClientTests.exe` 和扩展后的 `WindowsDebugServerTests.exe` 均成功构建。 |
| `script\test_runtime.bat` | 通过；运行时核心、Windows 运行时适配器、调试协议、平台无关归约、Windows 假服务端、Windows 调试服务端集成和运行时 CLI 测试全部通过。 |
| `script\build.bat` | 通过；`InputWeaver.exe`、Windows 平台测试、共享程序测试和 focused runtime 测试全部成功构建。 |
| `script\test.bat` | 通过；Windows 平台、共享程序和全部 runtime 测试通过。 |
| `script\analyze_all.bat` | 通过；33 个源实现单元使用 `-fanalyzer -fsyntax-only` 分析通过。 |
| `script\audit_dependencies.bat` | 通过；33 个源实现单元符合模块依赖边界。 |
| `git diff --check` | 通过。 |

## 边界确认

The final producer adjustment stops sampling a timestamp for `ExecutionEnded`; its fixed protocol-header time is zero while the marker and result remain unchanged. The protocol format and executor task-event publication contract remain unchanged. `RequestExecutorStop` only sends the existing protocol command.
