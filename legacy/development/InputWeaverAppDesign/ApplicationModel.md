# 应用模型

## ProgramEntry

每个程序条目包含稳定内部 ID、唯一显示名称、运行配置、编译产物路径和保存的 Dump 路径。显示顺序由程序索引文件决定。

程序条目不保存源文件路径。修改显示名称只更新条目元数据，不改变内部 ID、`.weavec` 文件名或 Dump 文件名。

## RunConfiguration

每个程序条目保存以下运行配置：

- Target 使用 `Compiled`、`Executable` 或 `Global`。
- `Executable` 同时保存一个非空执行文件选择器。
- Logging 使用 `Off`、`Operational` 或 `Input Trace`。

Debug、dry-run 和执行权限是下一次启动使用的一次性选项，不写入程序条目。

## ManagedExecutor

托管执行器记录关联程序 ID、进程句柄、进程 ID、启动模式、当前日志路径和停止能力。进程 ID 只用于进程管理、debug 管道连接和校验，不作为用户选择依据，也不在 Programs 页面展示。

程序列表只在执行器存在时显示运行标签。执行器完全退出后，条目恢复为只显示名称。

## DebugSession

应用最多保存一个 DebugSession。DebugSession 记录程序 ID、执行器进程、debug token、`WindowsDebugClient` 和当前只读 `DebugClientState`。

启动 debug 时，应用生成 token，使用 `--debug-session` 启动执行器，使用子进程 ID 和 token 建立连接，然后调用 `StartCapture()`。收到可信的 `CaptureStarted` 状态后，应用自动切换到 Debug 页面。

对另一个条目启动 debug 时，应用调用当前客户端的 `RequestExecutorStop()`，等待当前执行器退出，释放客户端，再建立新的 DebugSession。

## 下一次运行选项

Programs 页面使用三个顺序切换键：

| 按键 | 标签 | 含义 | 执行器参数 |
| --- | --- | --- | --- |
| `T` | Trace | 启用 debug 会话 | `--debug-session <token>` |
| `S` | Safety | 启用无注入模拟运行 | `--dry-run` |
| `P` | Permission | 授予进程启动权限 | `--allow-exec` |

选项状态固定显示在 Programs 页面底部，并同时解释选项对下一次启动的实际影响：

```text
NEXT RUN  [T] Trace/Debug: OFF  [S] Safety/Dry-run: OFF  [P] Permission/EXEC: OFF
          T connects DebugClient and opens Debug | S skips SendInput/CreateProcess | P authorizes EXEC
```

选项可以依次切换后使用 `Space` 启动。成功启动或改变程序选择后，三个选项全部恢复为 Off。

`S` 与 `P` 可以同时开启：`P` 通过 `EXEC` 权限检查，`S` 仍保证实际不调用 `CreateProcess`。

## 停止

Debug 执行器通过现有 `RequestExecutorStop()` 请求有序停止。普通执行器由应用创建为独立 Windows 进程组，并通过 `CTRL_BREAK_EVENT` 请求有序停止。

应用等待执行器退出并排空重定向的控制台输出。停止完成后删除内存中的托管执行器状态，Programs 列表不保留停止文字。

## 通用滚动

所有只读滚动区域使用统一按键：

| 按键 | 行为 |
| --- | --- |
| `Up` / `Down` | 滚动一行 |
| `PageUp` / `PageDown` | 滚动一页 |
| `Home` | 移动到开头 |
| `End` | 移动到结尾 |

Console、EVENTS 和 ACTION EXECUTIONS 默认跟随最新内容。用户向上滚动后停止跟随，按 `End` 返回结尾并恢复跟随。

## 焦点显示

页面内当前焦点通过对应区域的标题颜色和边框颜色表示。同一页面内每个可聚焦区域使用不同的专属焦点色，未获得焦点的区域统一使用灰色边框和普通灰色标题。实际 RGB 值从 `res/InputWeaverTUI.colors.json` 读取，下表描述默认配色的视觉含义。

| 页面 | 区域 | 获得焦点时的标题与边框颜色 |
| --- | --- | --- |
| Console | CONSOLE | 亮蓝色 |
| Programs | PROGRAMS | 亮青色 |
| Programs | PROGRAM INFORMATION | 亮黄色 |
| Programs | COMPILED DUMP | 亮品红色 |
| Debug | EVENTS | 亮青色 |
| Debug | PRESSED | 亮绿色 |
| Debug | ACTION EXECUTIONS | 亮品红色 |

HEALTH 不接受焦点，使用固定的健康状态配色。

Programs 列表中的 `>` 表示当前选中条目，区域边框表示键盘焦点，两者含义独立。

## 实时按键栏

每个页面底部固定显示实时按键栏。按键栏只列出当前焦点和当前操作模式下有效的按键；焦点切换、进入编辑、进入移动排序或打开确认框时立即更新，不显示当前不可用的命令。

按键栏优先使用一行，空间不足时自然换成两行。`NEXT RUN` 状态栏只在 Programs 页面显示，位于实时按键栏上方。
