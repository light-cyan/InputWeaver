# Programs 页面

## 布局

Programs 页面左侧是程序列表，右上是所选程序的信息与可编辑运行配置，右侧其余空间全部用于显示保存的编译 Dump。

```text
┌─ PROGRAMS ───────────────────┬─ PROGRAM INFORMATION ─────────────────────┐
│ > Game                       │ Target:   Compiled                         │
│   Browser          [RUN]     │ Logging:  Operational -> programs/logs/   │
│   Media       [DBG][DRY]     ├─ COMPILED DUMP ────────────────────────────┤
│   Tools      [RUN][EXEC]     │ source display=...                         │
│                              │ settings target=...                        │
│                              │ requirements states=...                    │
│                              │ controls 4                                 │
│                              │ ...                                        │
│                              │                                            │
└──────────────────────────────┴────────────────────────────────────────────┘
NEXT RUN  [T] Trace/Debug: OFF  [S] Safety/Dry-run: OFF  [P] Permission/EXEC: OFF
          T connect DebugClient + open Debug | S skip SendInput/CreateProcess | P authorize EXEC
KEYS      ↑↓ Select  A Add  D Delete  R Rename  M Move  Enter Configure  Space Start  X Stop
          Tab Focus  ← Console  → Debug  Q Quit
```

左侧约占可用宽度的三成，右侧约占七成。PROGRAM INFORMATION 固定为 Target 和 Logging 两行，COMPILED DUMP 使用右侧剩余高度。

## 程序列表

空闲条目只显示名称，不显示状态占位文字。运行条目在行尾显示紧凑标签：

| 标签 | 含义 | 颜色 |
| --- | --- | --- |
| `[RUN]` | 普通执行器正在运行 | 青色 |
| `[DBG]` | debug 执行器正在运行 | 亮青色 |
| `[DRY]` | 当前执行器使用 dry-run | 绿色 |
| `[EXEC]` | 当前执行器获得进程启动权限 | 品红色 |

`[DBG]` 已表示执行器正在运行，不与 `[RUN]` 同时显示。`[DRY]` 和 `[EXEC]` 作为附加标签显示。

```text
Game
Browser               [RUN]
Media             [DBG][DRY]
Tools            [RUN][EXEC]
Macro       [DBG][DRY][EXEC]
```

应用创建执行器后显示对应运行标签，执行器完全退出后移除全部标签。

## 信息区

PROGRAM INFORMATION 只显示两行：

```text
Target:   Compiled
Logging:  Operational -> programs/logs/
```

Target 的显示形式为：

```text
Target:   Compiled
Target:   Executable -> game.exe
Target:   Global
```

Logging 为 `Off` 时不显示路径。Logging 为 `Operational` 或 `Input Trace` 时在同一行显示相对日志目录：

```text
Logging:  Off
Logging:  Operational -> programs/logs/
Logging:  Input Trace -> programs/logs/
```

每次运行生成的具体日志文件路径由 Console 输出。

## Dump

COMPILED DUMP 显示导入时保存的 `.dump.txt`。选择变化时载入对应 Dump，并把滚动位置重置到开头。

Dump 使用自然文本换行，不提供搜索和记录详情。

## 焦点

`Tab` 按以下顺序循环焦点：

```text
PROGRAMS -> PROGRAM INFORMATION -> COMPILED DUMP -> PROGRAMS
```

PROGRAMS 获得焦点时使用亮青色标题与边框，PROGRAM INFORMATION 使用亮黄色，COMPILED DUMP 使用亮品红色。没有获得焦点的区域统一使用灰色边框和普通灰色标题。

PROGRAMS 区域使用 `>` 表示当前条目。选择标记不表示键盘焦点。

所选条目整行使用反色显示，包含名称与状态标签；`>` 继续保留，用于强化位置并兼容颜色辨识不清的终端。PROGRAMS 失去焦点时仍保留较暗的反色选择行，获得焦点时使用高亮反色。

## 实时按键栏

页面底部的 KEYS 行随焦点和操作模式更新。PROGRAMS 焦点显示选择、添加、删除、改名、排序、配置、启动和停止按键；PROGRAM INFORMATION 焦点改为显示字段选择与编辑按键；COMPILED DUMP 焦点改为显示滚动按键。

例如 PROGRAM INFORMATION 获得焦点时显示：

```text
KEYS  ↑↓ Field  Enter Edit  Tab Dump  Esc Programs  ← Console  → Debug
```

Target 模式选择或 Executable 文本输入期间，按键栏只显示当前编辑操作，不显示页面导航。移动排序、添加路径、改名、覆盖选择和删除确认同样显示各自当前可用的确认与取消按键。

## PROGRAMS 按键

| 按键 | 行为 |
| --- | --- |
| `Up` / `Down` | 选择条目 |
| `A` | 添加一个 `.weave` |
| `D` | 删除所选条目 |
| `R` | 修改所选条目名称 |
| `M` | 进入移动排序模式 |
| `Enter` | 把焦点移到 PROGRAM INFORMATION |
| `T` | 切换下一次运行的 Trace |
| `S` | 切换下一次运行的 Safety |
| `P` | 切换下一次运行的 Permission |
| `Space` | 按当前 T/S/P 选项启动所选条目 |
| `X` | 停止所选条目的执行器 |
| `Tab` | 把焦点移到 PROGRAM INFORMATION |
| `Left` | 切换到 Console |
| `Right` | 切换到 Debug |
| `Q` | 有序停止全部托管执行器并退出 |

所选条目已有执行器时，`Space` 不创建进程，也不显示额外提示。

## 移动排序

按 `M` 进入移动排序模式后，`Up` 与 `Down` 移动条目，`Enter` 确认并保存新顺序，`Esc` 恢复进入移动模式前的顺序。

## Target 编辑

PROGRAM INFORMATION 获得焦点后，使用 `Up` 与 `Down` 在 Target 和 Logging 两行之间选择，使用 `Enter` 编辑所选行，使用 `Tab` 移到 COMPILED DUMP，使用 `Esc` 返回 PROGRAMS。

在 PROGRAM INFORMATION 中选择 Target 并按 `Enter` 后，使用 `Up` 与 `Down` 选择 `Compiled`、`Executable` 或 `Global`。

`Compiled` 和 `Global` 使用 `Enter` 确认。`Executable` 使用 `Enter` 进入当前行内的文本输入框。

Executable 输入框使用 `Left` 与 `Right` 移动光标，使用 `Backspace` 与 `Delete` 修改文本，使用 `Enter` 确认，使用 `Esc` 返回 Target 模式选择。

## Logging 编辑

在 PROGRAM INFORMATION 中选择 Logging 并按 `Enter` 后，使用 `Up` 与 `Down` 选择 `Off`、`Operational` 或 `Input Trace`，使用 `Enter` 确认，使用 `Esc` 取消。

## Dump 按键

| 按键 | 行为 |
| --- | --- |
| `Up` / `Down` | 滚动一行 |
| `PageUp` / `PageDown` | 滚动一页 |
| `Home` / `End` | 移动到开头或结尾 |
| `Tab` | 把焦点移回 PROGRAMS |
| `Esc` | 把焦点移回 PROGRAMS |
