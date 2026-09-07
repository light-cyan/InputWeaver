<a id="section-debugging-and-troubleshooting"></a>

# 调试与排错

[English](../en/debugging.md)

[文档首页](README.md) · [界面操作](tui.md)

先看输入有没有被观察到，再看条件是否匹配，最后看动作有没有完成。Debug 页把这三个过程放在同一个界面中，Console 和日志提供具体错误信息。

<a id="section-starting-a-debugging-session"></a>

## 启动一次调试

1. 在 Program 页选择要检查的程序，退出源码编辑。
2. 按 `T` 打开 Trace and Debug。想先检查匹配和动作计算，可以再按 `S` 打开模拟运行。
3. 按空格。界面进入 Debug，短暂显示 `STARTING` 后启动执行器。
4. 将目标应用切到前台，再触发要检查的输入。
5. 回到 Debug 查看 EVENTS、变量和动作记录。

```weave
TARGET = "notepad.exe";
number count = 0;

F6:down => set(count, count + 1) tap(B);
```

用这个例子调试时，在记事本前台按 F6，EVENTS 会记录输入，VARIABLES 中的 `count` 会增加，ACTION EXECUTIONS 会显示这条规则的执行过程。模拟运行时，记事本收到原始 F6，B 输出只作模拟。

<a id="section-five-interactive-areas"></a>

## 五个可操作区域

| 区域 | 主要看什么 |
| --- | --- |
| EVENTS | 按键、鼠标按钮和计量器 tick 是否出现，输入是放行还是接管 |
| INPUT STATE | 当前按住的键、输入来源和实时鼠标状态 |
| METERS | 计量器当前进度及最近完成周期 |
| VARIABLES | 内置配置、用户变量和数组的当前值 |
| ACTION EXECUTIONS | 哪条规则匹配、执行了什么、完成还是取消 |

底部 HEALTH 显示连接、捕获和运行问题。用 `Tab` 依次进入五个区域；区域内用方向键、`PageUp`、`PageDown`、`Home`、`End` 滚动，`Esc` 返回区域选择。

<a id="section-reading-events"></a>

## 看懂 EVENTS

每行包含 TIME、SOURCE、EVENT、ORIG、PASS、COUNT。

| 列 | 含义 |
| --- | --- |
| TIME | 捕获时间 |
| SOURCE | 按键、鼠标按钮或计量器名称 |
| EVENT | 事件类型，例如 `down`、`up`、`AGAIN`、`tick` |
| ORIG | 输入来源 |
| PASS | `PASS` 为放行，`DROP` 为规则计算出的接管决定 |
| COUNT | 合并显示的计量器 tick 数；普通按键和按钮为 `_` |

| 来源标记 | 含义 |
| --- | --- |
| `PHY` | 被识别为物理输入 |
| `ECHO` | 当前执行器产生的模拟输入 |
| `EXT` | 其他来源的模拟输入 |
| `INIT` | 捕获开始时已经按住的键或按钮 |

`AGAIN` 表示已按住时又收到按下报告；`NO-DOWN` 表示捕获中尚未建立对应按下状态就收到了释放。连续同来源、同处置的重复按下会合并，连续同名计量器 tick 也会合并。

模拟运行中的 `DROP` 表示正常运行时会接管，实际物理输入仍然放行。看到一行输入只代表观察到了它；是否匹配用户规则，还要看目标、暂停状态和输入来源。

<a id="section-inspecting-variables-and-mouse-state"></a>

## 看变量和鼠标

VARIABLES 顶部以较淡的颜色显示四个只读配置：`TAP_DURATION`（默认按下时长）、`ACTION_GAP`（动作间隔）、`MOUSE_IDLE_TIMEOUT`（鼠标空闲判定时长）和 `RAND_SEED`（随机种子）。这里显示当前运行程序实际采用的配置，包括源码省略配置时的默认值。时间带有单位，随机种子按完整整数显示。

后面依次显示用户变量和数组，包括值为零、`off` 和空数组的条目。长数组展示长度以及开头和末尾的一部分元素。`PAUSE` 在 HEALTH 中显示：`on` 表示普通规则启用，`off` 表示暂停。

INPUT STATE 显示当前按住的键及其来源，然后显示鼠标坐标、位移、滚轮、移动状态和空闲时间。滚轮成对显示时，顺序是水平量、垂直量。

METERS 的当前行显示进度、周期和坐标；下面的 `@` 行显示最近完成记录，`empty` 表示尚无完成记录。数值在界面上最多显示一位小数，程序计算保留原有精度。[计量器字段](mouse.md)

<a id="section-checking-action-completion"></a>

## 看动作是否完成

ACTION EXECUTIONS 的每条记录包括触发事件、匹配时间、条件 `AS` 和动作 `ACT`。没有条件时显示 `always`。完整映射也会产生执行记录。

| 颜色 | 状态 |
| --- | --- |
| 青色 | 正在运行或等待 |
| 深绿色 | 已完成 |
| 红色 | 失败 |
| 黄色 | 已取消 |

目标切换、暂停、停止或运行故障都会导致相应动作取消。记录中的条件和动作对应源程序；较长动作可以通过滚动检查。

<a id="section-capture-versus-stopping"></a>

## 捕获和停止的区别

`C` 停止捕获或开始一次新捕获，执行器继续运行。重新捕获会清空旧的界面记录，并从当前状态建立新视图；程序内部的变量、数组、计量器和暂停状态继续保留。

`X` 停止当前 Debug 执行器并清空调试视图。执行器自行结束时，界面保留最终状态供检查，之后按 `X` 清空，或启动新的调试会话。

HEALTH 中的 `Complete snapshot` 表示停止时的最终状态已完整取得；`Best-effort snapshot` 表示可能缺少最后一部分数据。输入记录产生过快、来不及处理时，Debug 会报告捕获溢出，并尝试重新取得当前状态后继续显示。

<a id="section-common-problems"></a>

## 常见问题

| 现象 | 检查方法 |
| --- | --- |
| 编译失败 | 看 SOURCE 的红色区间或 Console 的行列诊断，检查名称、类型、分号和括号 |
| 编译文件无法读取或版本不匹配 | 使用同一发行包中的编译器重新编译 `.weave` 源码，再运行新产物；见[编译文件版本处理](command-line.md#section-compiled-file-version-mismatch) |
| 启动时提示原始控制不可用 | 检查该平台是否支持控制的用途，以及同一物理输入是否使用了相互重叠的编码；见 [Windows 控制能力](windows.md#section-control-encodings-and-support) |
| 显示等待目标 | 检查程序是否启动，`TARGET` 是否是实际可执行文件名；多个匹配实例时把所需实例切到前台 |
| EVENTS 有输入，动作没有出现 | 确认目标在前台、鼠标位置属于目标、`PAUSE` 为 `on`、输入来源为物理输入，以及 `when` 条件满足 |
| 操作 TUI 时规则没有触发 | 宿主自动排除了 TUI，切到目标应用后测试 |
| 动作完成，却没有实际按键或鼠标变化 | 检查是否启用了 `S` 或 `--dry-run`，以及目标前台和输出位置 |
| 输出字符和预期不同 | 检查键盘布局、输入法、Caps Lock，以及仍按住的物理修饰键 |
| 动作变黄、连发中断 | 检查目标切换、暂停或停止；取消后的动作由新输入重新触发 |
| 数组或计量器表达式报错 | 检查索引、数组长度、空数组 `pop`、`@name.valid` 和正的计量周期 |
| 一次求值错误后整个执行器退出 | 检查条件、标量 `set`、`wait` 和流程控制的表达式；这些位置的除零或越界等错误会请求停止执行器，具体范围见[错误处理](running.md#section-expression-and-action-errors) |
| `exec` 程序启动被拒绝 | 本次运行打开 `P`，或传入 `--allow-exec` |
| 源码已修改但运行行为未变 | 停止执行器后重新运行，确认编译成功 |
| 窗口关闭后宏仍在工作 | 从托盘恢复界面后按 `X`，或通过托盘的 `Exit InputWeaver` 退出 |

目标应用和 InputWeaver 通常都以普通用户身份运行；两者权限等级不同可能影响输入注入。排查注入失败时，先对齐运行权限，再看 Console 和日志中的具体错误。

<a id="section-saving-diagnostic-logs"></a>

## 保存诊断日志

在 PROGRAM INFORMATION 中选择 Logging，或使用命令行的 `--log`；需要输入与输出轨迹时，选择 `Input Trace` 或加入 `--trace-input`。[命令行例子](command-line.md)

JSONL 文件每行是一条记录。输入轨迹可能包含按键和鼠标位置；分享日志前按排查需要检查内容。

常见运行问题包括：

| 记录或指标 | 含义和排查方向 |
| --- | --- |
| `TargetEligibilityChange` | 目标资格变化，检查窗口前台切换 |
| `PhysicalStateSynchronization` | 输入来源建立初始松开基线 |
| `PredicateFault` | 条件或计量周期求值错误，按出错位置检查[影响范围](running.md#section-expression-and-action-errors) |
| `TaskExpressionFault` | 动作参数求值错误，检查对应表达式和任务或执行器的最终状态 |
| `TaskActionFault` | 动作操作失败，例如空数组 `pop` 或数组增长受限；结合记录位置和详细值定位 |
| `TransactionCapacity` | 同时触发的工作过多时，新工作会被拒绝；单个鼠标报告的完成周期超限会停止执行器，见[资源上限](running.md#section-resource-limits) |
| `TaskBudgetExceeded` | 一段动作连续执行过多，检查循环和等待 |
| `OutputRateExceeded` | 输出过于密集，增加间隔或减少输出 |
| `Cancellation` | 任务因目标、暂停、停止等原因取消 |
| `injection_failures` | Windows 输入注入失败次数 |
| `transaction_rejections` | 同一次事件匹配到的新工作因容量不足而被整批拒绝的次数 |
| `rejected_array_growth` | 数组增长被容量或分配限制拒绝 |

单个日志文件上限为 8 MiB。结束后检查 `Diagnostic log stopped.` 的最终统计：丢弃计数和 `jsonl_truncated` 表示日志是否完整。[资源限制与处理方式](running.md)
