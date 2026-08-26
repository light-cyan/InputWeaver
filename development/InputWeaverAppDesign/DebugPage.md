# Debug 页面

## 布局

Debug 页面上部左侧显示 EVENTS，上部右侧显示 PRESSED，中部显示 ACTION EXECUTIONS，底部固定显示 HEALTH。

```text
DEBUG  Game  TRACE + SAFETY

EVENTS                               PRESSED
16:42:10.243 ↓ A PHY DROP            [A PHY] [LCtrl INIT]
16:42:10.291 ↓ B ECHO PASS           [B ECHO] [Mouse.Left PHY]

────────────────────────────────────────────────────────────

ACTION EXECUTIONS

#17  16:42:10.247  [16:42:10.243 ↓ A PHY DROP]
     press(B) wait(30ms) release(B) end

────────────────────────────────────────────────────────────

HEALTH  Connected | Capturing | Trusted | Epoch 3 | Dry-run
        Fault: None | Runtime issues: 0

KEYS    ↑↓ Event  PgUp/PgDn Page  Home/End Edge  Tab Region
        C Start/Stop Capture  X Stop Executor  ← Programs
```

HEALTH 不滚动。EVENTS、PRESSED 和 ACTION EXECUTIONS 分别保存独立滚动位置。

## 焦点

`Tab` 按以下顺序循环焦点：

```text
EVENTS -> PRESSED -> ACTION EXECUTIONS -> EVENTS
```

EVENTS 获得焦点时使用亮青色标题与边框，PRESSED 使用亮绿色，ACTION EXECUTIONS 使用亮品红色。没有获得焦点的区域统一使用灰色边框和普通灰色标题。

三个区域使用 `Up`、`Down`、`PageUp`、`PageDown`、`Home` 和 `End` 滚动。Debug 页面不使用 `Enter` 打开记录详情。

## EVENTS

EVENTS 按捕获顺序追加输入记录。每条记录显示本地时间、转换、控制、来源、处理结果和附加分类：

```text
16:42:10.243 ↓ A PHY DROP
16:42:10.291 ↓ B ECHO PASS REPEAT
16:42:10.310 ↑ F6 EXT PASS NO-DOWN
16:42:10.330 ↓ LCtrl INIT -
```

来源标签使用 `PHY`、`ECHO`、`EXT` 和 `INIT`。处理结果使用 `PASS` 和 `DROP`。`REPEAT` 表示重复 Down，`NO-DOWN` 表示 Up 没有对应的当前按下状态。`INIT` 没有处理结果。

`NO-DOWN` 直接显示现有 `DebugInputEvent::unmatchedUp`。`DebugClient` 在 Up 既没有移除对应控制的 `InitialSample` 按下项、也没有移除相同来源按下项时设置该字段；TUI 不自行推断。

EVENTS 默认跟随最新记录。向上滚动后停止跟随，按 `End` 恢复。

## PRESSED

PRESSED 显示当前仍然按下的控制和来源，并根据可用宽度排列为多列：

```text
[A PHY]      [LCtrl INIT]  [Mouse.Left PHY]
[B PHY]      [LShift PHY]  [Mouse.Right PHY]
[F1 EXT]     [Space PHY]   [Consumer.Play PHY]
```

控制释放后立即从 PRESSED 移除。列数由区域宽度和单元宽度计算，内容按行滚动。

PRESSED 使用 `Up` 与 `Down` 滚动一行，使用 `PageUp` 与 `PageDown` 滚动一页，使用 `Home` 与 `End` 移动到首行或末行。内容减少时，滚动位置收缩到有效范围。

## ACTION EXECUTIONS

ACTION EXECUTIONS 只显示由事件规则创建的动作任务，不显示完整映射和条件表达式。

每个条目由两条逻辑行组成：

```text
#marker  matched-time  [trigger-event]
         complete-action-instructions
```

例如：

```text
#17  16:42:10.247  [16:42:10.243 ↓ A PHY DROP]
     press(B) wait(30ms) release(B) end
```

第一行的时间是规则匹配并创建动作执行的时间。方括号内使用与 EVENTS 相同的事件格式，时间是触发输入的捕获时间。

两条逻辑行都允许自然换行。续行保持缩进，不重复 marker、匹配时间或触发事件字段。

## 动作状态颜色

第一条逻辑行通过颜色表达状态，不显示状态文字：

| 状态 | 颜色 |
| --- | --- |
| Running | 青色 |
| Completed | 绿色 |
| Failed | 红色 |
| Cancelled | 黄色 |

新增条目直接使用稳定青色，不闪烁。执行结束时，第一行原地切换为最终颜色，条目不重新排序。

ACTION EXECUTIONS 按形成顺序在底部追加。后续条目把已经结束的彩色条目逐渐推向上方，形成历史。

## 动作指令轨迹

`DebugClient` 为每个执行保存 `currentInstructionIndex` 和容量为三的 `recentInstructionIndices`。最近三步包含当前步骤。

运行时第二条逻辑行使用暗青色标记倒数第三步，青色标记倒数第二步，亮青色标记当前指令。当前指令可以同时使用反色或下划线。

执行结束时清除当前指令标记并保留最后三步。保存的轨迹继续显示青色深浅，但不再显示当前指令的反色或下划线。

ACTION EXECUTIONS 默认跟随最新条目。向上滚动后停止跟随，按 `End` 恢复。

## HEALTH

HEALTH 固定在页面底部：

```text
HEALTH  Connected | Capturing | Trusted | Epoch 3 | Dry-run
        Fault: None | Runtime issues: 2 | Latest: OutputFailure
```

HEALTH 显示连接状态、捕获请求、捕获状态、可信状态、capture epoch、DebugClient fault、运行时问题数量、最新运行时问题和应用已知的 dry-run 模式。

恢复捕获时显示：

```text
HEALTH  Connected | Recovering | Untrusted | Epoch 3 | Dry-run
        Fault: DebugStreamLost | Runtime issues: 3
```

## Debug 控制

| 按键 | 行为 |
| --- | --- |
| `C` | 在 `StartCapture()` 与 `StopCapture()` 之间切换 |
| `X` | 调用 `RequestExecutorStop()` |
| `Tab` | 切换可滚动区域焦点 |
| `Left` | 切换到 Programs |

应用建立 DebugClient 连接后自动调用 `StartCapture()`。`StopCapture()` 只停止调试捕获，执行器继续运行。再次开始捕获会建立新的 epoch，并通过 `INIT` 重新形成可信按下状态。

底部 KEYS 行固定可见，并随 EVENTS、PRESSED 或 ACTION EXECUTIONS 焦点更新滚动含义。执行捕获切换和停止执行器后，按键栏立即反映新的可用操作。

```text
EVENTS focused:            KEYS  ↑↓ Event         PgUp/PgDn Page  Home/End Edge  Tab Region  C Capture  X Stop  ← Programs
PRESSED focused:           KEYS  ↑↓ Pressed row   PgUp/PgDn Page  Home/End Edge  Tab Region  C Capture  X Stop  ← Programs
ACTION EXECUTIONS focused: KEYS  ↑↓ Execution     PgUp/PgDn Page  Home/End Edge  Tab Region  C Capture  X Stop  ← Programs
```
