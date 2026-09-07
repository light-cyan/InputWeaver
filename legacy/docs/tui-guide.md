# InputWeaver TUI 使用指南

`InputWeaverHost.exe` 是程序库、源码编辑、运行配置、执行器控制和输入调试的统一入口。它仍然通过独立的 `InputWeaverCompiler.exe` 生成 `.weavec`，再启动独立的 `InputWeaver.exe`；TUI 不把源码直接交给运行时，也不在内存中把编译程序传给执行器。无控制台托盘宿主持有应用状态，随附的 `InputWeaverTUI.exe` 只提供可关闭和重新创建的原生 Windows 界面。

Weave 语法、类型、规则匹配和动作语义见[正式语法说明](grammar.md)；发行包中的教程与产品使用说明从[中文文档首页](../docs/zh/README.md)开始。

## 启动与页面

Windows 10 或 Windows 11 x64 用户解压完整的 `InputWeaver-windows-x64.zip`，进入其中的 `InputWeaver` 文件夹并运行 `InputWeaverHost.exe`。不要在 ZIP 内直接运行，也不要拆散同目录中的四个 EXE 和 `res` 文件夹；发行版已静态链接 MinGW 的 GCC 与 C++ 运行库，不要求目标电脑安装 MinGW。源码仓库的维护者可以运行 `script\package_release.bat`，在 `bin\release\` 中重新生成同样的目录和 ZIP。

The frontend opens at 144x44 cells and permits resizing down to 100x32 cells. It starts on Program; Console, Program, and Debug form the page rail shown as `InputWeaver | ‹[ Console · PROGRAM · Debug ]›`. The active page is uppercase and uses the current page or region color. Keyboard hints appear in the header during ordinary interaction.

顶层页面之间的移动如下：

| 当前页面 | 按键 | 结果 |
| --- | --- | --- |
| Program | `[` | 进入 Console |
| Program | `]` | 进入 Debug |
| Console | `]` 或 `[Esc]` | 返回 Program |
| Debug | `[` | 返回 Program |

页面顺序固定为 Console、Program、Debug，并且不会从两端循环。页面切换只改变当前显示的页面；各页面的区域选择、区域进入、滚动、字段、源码和文档全屏状态保持不变。弹窗、确认、选项选择和单行输入期间不会切页；源码编辑中的 `[` 和 `]` 作为源码字符输入。

`[Esc]` 同时承担逐层退出：源码编辑时先退出编辑，文档全屏时再退出全屏，已进入 Program 或 Debug 区域时返回该页的区域选择，Debug 区域选择和 Console 返回 Program，最后从 Program 区域选择进入后台并隐藏到系统托盘。进入后台不停止正在运行的执行器。

## 系统托盘与后台运行

托盘宿主启动后会在 Windows 系统通知区注册 InputWeaver 图标，并启动一个拥有自身 Win32 窗口的独立前端。前端进程直接拥有窗口和任务栏按钮，两处都使用 InputWeaver 图标。最小化前端窗口会将其保留在任务栏；在 Program 区域选择状态按 `[Esc]` 或关闭前端窗口，会结束这个前端并使其从任务栏消失。托盘宿主、应用状态和已启动执行器保持运行。

单击或双击托盘图标会恢复并聚焦正常响应的前端，或回收失联前端并创建新的前端以恢复原有 TUI 状态。右击托盘图标可以选择 `Show TUI` 或 `Hide TUI`；选择 `Exit InputWeaver` 才会真正退出托盘宿主，并请求停止本次 TUI 管理的全部执行器。运行中的前端故障通过托盘通知报告，不会用模态对话框阻塞托盘操作；源码保存失败时，前端保持打开，以便查看错误。

请始终启动 `InputWeaverHost.exe`。`InputWeaverTUI.exe` 是由托盘宿主按需启动的内部前端，只能继承宿主明确提供的两条进程间通信管道，不作为独立入口使用。

## Program 页

Program 页的分栏布局由左侧 PROGRAM、右上 PROGRAM INFORMATION、右下 SOURCE 或 COMPILED DUMP，以及底部 NEXT RUN 组成。页面首先处于区域选择状态，方向键按照区域的实际位置移动选择：PROGRAM 的 `[Right]` 进入 PROGRAM INFORMATION，PROGRAM INFORMATION 的 `[Left]` 返回 PROGRAM、`[Down]` 进入 SOURCE，SOURCE 的 `[Left]` 返回 PROGRAM、`[Up]` 进入 PROGRAM INFORMATION。按 `[Enter]` 进入所选区域后，方向键操作该区域的内容；按 `[Esc]` 返回区域选择。当前所选区域由边框颜色表示。

运行中的程序在列表右侧显示状态标签：普通执行器为 `[RUN]`，Debug 执行器为 `[DBG]`，无注入模拟增加 `[DRY]`，已授予 `exec` 权限增加 `[EXEC]`。所选程序行始终使用反色显示；未进入 PROGRAM 区域时使用较弱的反色。

### Program

In the split Program layout, `[Tab]` cycles through PROGRAM, PROGRAM INFORMATION, and SOURCE or COMPILED DUMP, then wraps to PROGRAM. Each switch directly activates the destination region, from either region selection or an active region.

下列程序管理按键只在进入 PROGRAM 区域后生效：

| 按键 | 功能 |
| --- | --- |
| `[Up]` / `[Down]` | 选择程序 |
| `[A]` | 打开居中的横向 Add Program 选择栏 |
| `[D]` | 确认删除所选程序 |
| `[M]` | 进入排序模式 |

Add Program 使用 `[Left]` / `[Right]` 选择 `New Blank`、`Import .weave` 或 `Cancel`，再按 `[Enter]` 确认。新建空白程序会创建一个空白的可编辑 `.weave` 副本，不会预先编译。导入只接受一个现有 `.weave` 文件，路径输入支持正常键入、`[Ctrl+V]` 粘贴以及向前端窗口拖放文件。

导入时，TUI 会复制源码到程序库，调用编译器生成 `.weavec`，并保存可查看的 Dump。导入编译失败时不会新增或覆盖程序，界面会切换到 Console 显示编译器诊断。程序名取自源文件名；名称冲突时可以覆盖原条目、换名导入或取消。覆盖保留原条目的 ID、位置和运行配置。

删除程序前会先停止它的执行器；确认删除后移除条目元数据、源码副本、编译产物和 Dump，已有日志文件保留。排序模式中使用 `[Up]` / `[Down]` 移动条目，`[Enter]` 保存顺序，`[Esc]` 放弃本次排序。

### Program Information

Program Information 包含 Name、Target 和 Logging。使用 `[Up]` / `[Down]` 选择字段，使用 `[Enter]` 原位编辑；程序列表不再提供独立的重命名按键。

Name 使用单行编辑器修改。程序名按 Windows 序号规则进行不区分大小写的唯一性比较。

Target 有三种模式：

| 模式 | 含义 |
| --- | --- |
| `Compiled` | 使用 `.weave` 中编译得到的 `TARGET` |
| `Executable` | 本次程序条目使用指定的可执行文件名或绝对路径覆盖编译目标 |
| `Global` | 本次程序条目覆盖为全局目标 |

Target 模式选择使用 `[Up]` / `[Down]` 和 `[Enter]`。选择 `Executable` 后，`Executable ->` 后方成为可水平滚动的单行编辑器；使用方向键、`[Home]`、`[End]`、`[Backspace]` 和 `[Delete]` 编辑，`[Enter]` 保存，`[Esc]` 返回模式选择。

Logging 有 `Off`、`Operational` 和 `Input Trace` 三种模式。启用日志后，每次运行的相对日志路径会写入 Console，JSONL 文件保存在 `programs\logs\`。

### 源码与 Dump

SOURCE 显示程序库中的 `.weave` 源码副本，包含行号、弱化的竖向分隔线、当前行底色和语法高亮。浏览源码时，`[Left]` / `[Right]` 每次水平移动四个显示列，`[Up]` / `[Down]` 和 `[PageUp]` / `[PageDown]` 移动当前行，`[Home]` 跳到第一行，`[End]` 跳到最后一行。水平位置在分栏、文档全屏和顶层页面切换之间保留。

语法颜色参考 Visual Studio Code 的默认配色并按 Weave 语义归类：

| 类别 | 示例 | 默认颜色 |
| --- | --- | --- |
| 结构关键字 | `when`、`repeat`、`if`、`else` | 紫色 |
| 类型名 | `state`、`number`、`duration`、`meter` | 青绿色 |
| 变量、数组、计量器和内蕴值 | 用户声明名、`TARGET`、`PAUSE`、`TAP_DURATION`、`ACTION_GAP` | 浅蓝色 |
| 属性与字段 | `values.length`、`Mouse.x`、`path.progress`、`@path.dx` 中的名称与 `@` | 浅蓝色 |
| 常量与事件后缀 | `GLOBAL`、`on`、`off`、`held`、`idle`、`down`、`again`、`up`、`move`、`wheel`、`horizontalwheel`、`tick`、数字、时长 | 浅绿色 |
| 控制名与事件源 | `A`、`LCtrl`、`Mouse.Left`、`Windows.VirtualKey`、`Mouse:move` 中的 `Mouse` | 亮蓝色 |
| 动作和分隔符 | `tap`、`set`、`toggle`、`append`、`pop`、`clear`、`|` | 黄色 |
| 规则和映射箭头 | `->`、`~>`、`=>`、`=>>`、`~>>` | 亮白色 |
| 普通源码文本 | `and`、`or`、`not`、括号、方括号、逗号、冒号、分号、点和表达式运算符 | 默认前景色 |
| 字符串 | `"..."` | 橙色 |
| 注释 | `//...`、`/*...*/` | 绿色 |

属性与字段和用户声明名共用 `syntax_variable` 配色；点号使用正文配色 `text`。成员着色按所属数组、`Mouse` 或已声明计量器识别。`length`、`dx` 等名称也可以用于用户声明，按变量配色显示；具体命名规则见[名称和保留字](grammar.md#名称和保留字)。

`[V]` 在 Source 和 Compiled Dump 之间切换。没有已保存的 Dump 时，切换会立即调用编译器生成；生成失败会进入 Console。源码一旦保存，旧 Dump 会被移除，原 `.weavec` 文件可以暂时保留，但它的源码摘要不再匹配，因此不会被下一次运行复用。

`[Z]` 从分栏进入文档全屏，`[Esc]` 返回分栏。这里的全屏只隐藏 PROGRAM 和 PROGRAM INFORMATION，不改变前端窗口状态。进入全屏不会自动进入编辑，源码和 Dump 都沿用分栏中的浏览按键；切换到其他顶层页面再返回 Program 时仍保持文档全屏。

### 源码编辑

`[E]` 进入源码编辑；如果当前显示 Dump，会先切回 Source。编辑既可以在分栏内进行，也可以在文档全屏内进行。编辑模式不显示顶部按键说明和 NEXT RUN，只在顶部边框最右侧显示 `[Esc] Exit`。

| 按键 | 功能 |
| --- | --- |
| 方向键、`[PageUp]` / `[PageDown]` | 移动光标 |
| `[Home]` / `[End]` | 移到当前行首或行尾 |
| `[Enter]` | 插入新行 |
| `[Tab]` | 插入四个空格 |
| `[Backspace]` / `[Delete]` | 删除文本或当前选择 |
| `[Shift]` + 移动键 | 扩展文本选择 |
| `[Ctrl+C]` | 复制选择到 Windows 剪贴板 |
| `[Ctrl+X]` | 剪切选择到 Windows 剪贴板 |
| `[Ctrl+Z]` | 撤销 |
| `[Ctrl+Y]` | 重做 |
| `[Esc]` | 保存并退出编辑 |

全屏编辑时，第一次 `[Esc]` 只退出编辑并保留文档全屏，第二次 `[Esc]` 才返回分栏。光标移动和编辑都会立即重新开始光标的显示周期；单行输入框和源码编辑器使用相同的细竖线光标。

源码编辑器最多接受 16 MiB 的 UTF-8 文本。撤销历史最多保留 256 个状态，并同时受 16 MiB 历史容量限制；执行新的编辑后，已有重做分支会被替换。

### 自动保存、校验和编译

停止编辑约 400 毫秒后，TUI 会把源码保存到程序库，并调用 `InputWeaverCompiler.exe validate` 完成与正式编译相同的词法、语法、语义和类型检查，但不写出 `.weavec`。离开编辑、切换程序、生成 Dump、运行和删除前也会先保存当前源码。

校验错误所在的整行使用暗红底色，诊断对应的源码区间使用亮红色并带下划线。自动校验只更新源码视图，不会因为普通语法错误抢走当前页面；保存失败、生成 Dump 失败、正式编译失败或运行启动失败会切换到 Console，并保留完整输出。

TUI 没有独立的编译按键。按 `[Space]` 运行时，如果 `.weavec` 不存在，或者记录的源码摘要与当前源码不同，TUI 会先正式编译并更新 Dump，再启动 `InputWeaver.exe`；编译产物仍然与运行时进程彼此独立。

### NEXT RUN 与执行器控制

非编辑状态下，NEXT RUN 在分栏和文档全屏中都显示以下选项；`ON ` 和 `OFF` 使用等宽文本，切换时不会改变后续内容的位置。

| 按键 | 选项 | 作用 |
| --- | --- | --- |
| `[T]` | Trace and Debug | 连接 DebugClient，并自动进入 Debug 页 |
| `[S]` | Skip Simulated Input | 使用 `--dry-run`，放行物理输入并模拟输出效果 |
| `[P]` | Authorize Execution Permission | 使用 `--allow-exec` 授权当前运行执行 `exec` |
| `[Space]` | Run | 使用当前三个选项运行所选程序 |

`[T]`、`[S]`、`[P]`、`[Space]` 和 `[X]` 在 Program 页的区域选择和任意非编辑区域中都可使用。所选程序已经运行时，`[Space]` 保持当前页面和 NEXT RUN 选项不变；成功启动后 NEXT RUN 选项恢复为关闭。`[X]` 请求停止所选程序的执行器；每个程序最多有一个执行器，不同程序可以同时普通运行，整个应用最多管理一个 Debug 执行器。启动另一个程序的 Debug 时，当前 Debug 执行器会先被停止。

选择 Trace and Debug 后按 `[Space]`，TUI 会立即切换到 Debug 页，并等待 500 毫秒后再保存后的正式编译、启动执行器和建立捕获，让启动按键有时间释放。等待期间 Debug 标题显示 `STARTING`，`[X]` 可以取消待启动操作。

## 程序库

程序库位于 `InputWeaverHost.exe` 同目录的 `programs\`：

| 文件 | 内容 |
| --- | --- |
| `programs.index` | 程序显示顺序 |
| `<id>.entry` | 名称、运行配置和已编译源码摘要 |
| `<id>.weave` | TUI 编辑的源码副本 |
| `<id>.weavec` | 持久化编译程序 |
| `<id>.dump.txt` | 最近保存的编译 Dump |

`programs.index` 缺失时，TUI 会根据有效的 `.entry` 文件按 ID 重建顺序。条目缺少源码时会建立空白可编辑源码；已记录为编译完成但缺少 `.weavec` 时会被标记为未编译，下一次运行会重新编译。启动和修复信息写入 Console。

## Console 页

Console 汇总应用、编译器和执行器输出。连续的同程序、同来源输出只在第一行前显示一次 `[Program][Source]` 身份标记，文本会按区域宽度换行；内存中最多保留最近 2048 条原始输出行。

应用操作失败、编译或运行启动失败、普通执行器写入标准错误，或者普通执行器以非零代码退出时，TUI 会自动切换到 Console 并保留对应输出。Debug 执行器的标准错误和退出信息仍写入 Console，但异常退出会进入并停留在 Debug 页查看最终捕获快照。普通运行信息不会抢走当前页面，源码自动校验诊断仍留在源码视图中。

使用 `[Up]` / `[Down]` 逐行滚动，`[PageUp]` / `[PageDown]` 整页滚动，`[Home]` 跳到第一行，`[End]` 跳到最新一行，`]` 或 `[Esc]` 返回 Program。

## Debug 页

Debug uses five independently scrollable regions. EVENTS sits above a compact INPUT STATE on the left; METERS sits above VARIABLES in the wider right column. ACTION EXECUTIONS spans the full width below them, with HEALTH retaining four rows at the bottom. INPUT STATE uses at most seven rows including borders. At the default 144x44 size, the left column is 54 cells wide and the right column is 90; METERS and VARIABLES each receive twelve rows. The three state titles remain distinct even when inactive: METERS is orange, VARIABLES is blue, and INPUT STATE is pink.

Region selection follows the columns: EVENTS moves down to INPUT STATE, and METERS moves down to VARIABLES. Left and right move between EVENTS and METERS, or between INPUT STATE and VARIABLES. Both lower regions move down to ACTION EXECUTIONS, whose up key returns to VARIABLES. `[Enter]` activates the selected region; `[Esc]` returns to region selection. Active regions use `[Up]` / `[Down]`, `[PageUp]` / `[PageDown]`, `[Home]`, and `[End]` to scroll.

`[Tab]` cycles through EVENTS, INPUT STATE, METERS, VARIABLES, and ACTION EXECUTIONS, then wraps to EVENTS. Each switch directly activates the destination region, from either region selection or an active region.

During an active Debug session, `[C]` stops capture or starts a new capture generation while keeping the executor running. `[X]` requests executor shutdown and immediately clears the Debug session, including EVENTS, VARIABLES, INPUT STATE, METERS, ACTION EXECUTIONS, and HEALTH; the same cleanup applies when stopping that program from the Program page. During delayed startup, `[X]` cancels the pending launch. When the executor terminates independently, its final snapshot remains available for inspection until `[X]` clears it or a new Debug session starts. `[` returns to Program; `[Esc]` also returns to Program from region selection.

### EVENTS

Keyboard events, mouse-button events, and meter completions share aligned TIME, SOURCE, EVENT, ORIG, PASS, and COUNT columns. COUNT shows the number of meter ticks represented by the row. Keyboard and mouse-button rows show `_` in this column, including repeated presses. EVENT shows `down`, `up`, `AGAIN`, `NO-DOWN`, or `tick`. Meter rows use the meter name as SOURCE and show `-` in the origin and disposition columns. Completions whose rules do not match also appear.

```text
TIME         SOURCE          EVENT   ORIG PASS COUNT
16:28:38.582 A               down    PHY  PASS     _
16:28:38.610 B               down    EXT  DROP     _
16:28:38.700 F22             down    INIT -        _
16:28:38.950 path            tick    -    -      128
```

Consecutive ticks from the same meter update one row with the latest capture time and total count. Consecutive `AGAIN` reports for the same control, origin, and disposition also share one row. Other visible events start a new row. The visible history retains at most 512 rows, while exact input and cycle identities remain available in separate bounded histories for action correlation.

`PHY` identifies physical candidate input, `ECHO` identifies this executor's injected input, `EXT` identifies other injected input, and `INIT` identifies controls sampled as already held when capture starts. `PASS` and `DROP` report the runtime decision; Dry-run displays that decision while allowing physical input through. `AGAIN` means a down report for an already-held control, and `NO-DOWN` means a release without a known matching press.

### VARIABLES

VARIABLES shows every user scalar and array, including `off`, zero, and empty arrays. Arrays include their logical length, such as `[values[3]=[2, 4, 8]]`; long arrays show bounded prefix and suffix values. Cells fill each row with a two-column gap and wrap when wider than the region. Capture starts with complete scalar and array snapshots, followed by incremental updates. `PAUSE` appears in HEALTH.

### INPUT STATE

INPUT STATE starts with currently held keyboard keys and mouse buttons, including their origins, such as `[LCtrl PHY]`, `[Mouse.Left PHY]`, and `[F22 INIT]`. Releases remove the corresponding held state. Live mouse observation follows these controls.

Debug observes mouse position and numeric input even when the running program only declares key rules. INPUT STATE separates `Mouse pos(x,y)`, `d(dx,dy)`, `wheel(x,y)`, movement status, and `idle(time)` into short fields. Wheel pairs list horizontal then vertical amount; the status becomes `idle` when movement stops. Idle time uses seconds with at most one decimal place, such as `idle(0.1s)` or `idle(60s)`. The writer samples live mouse state every 50 ms while capturing. All numeric mouse fields in INPUT STATE and METERS use at most one decimal place: coordinates, displacement, wheel amounts, distance, progress, period, and durations. Integer results omit the decimal point, and values rounded to zero display as `0`. Language evaluation and captured data retain their original precision.

### METERS

Each meter uses one current row such as `[path 3/24 start(100,200) pos(103,200) d(3,0)]`: the fraction is progress/period, coordinate pairs are x/y, and `d` is net dx/dy. Duration meters also show `dist(length)`; wheel meters show signed progress/period and position. The following `@` row independently summarizes the latest completed cycle, or reads `empty`. Each entry occupies exactly one line; long entries are truncated to the available width. METERS scrolls independently.

### ACTION EXECUTIONS

每个匹配并进入执行管线的规则使用源码级文本显示：

```text
#17  EVENT 16:28:38.582  MATCH 16:28:38.582  A down (PASS)
  AS   a < 5
  ACT  tap(B) | set(a,a+1)
```

`EVENT` 是触发输入的捕获时间，`MATCH` 是规则匹配并建立执行记录的时间。执行首行不重复显示来源；处置结果使用括号显示在转换之后。`#编号`、`AS` 和 `ACT` 使用弱化色，条目其余部分统一使用当前执行状态颜色：运行中为青色、完成为深绿色、失败为红色、取消为黄色。条件和动作来自编译产物内保存的源码片段，而不是降低后的指令序列。

普通动作规则的 ACT 显示完整动作管线；完整按键映射虽然不经过普通动作程序，但也会建立执行记录，并把完整映射源码显示在 ACT。规则没有条件时 AS 显示 `always`。

Mouse trigger labels are `Mouse (move)`, `Mouse (wheel)`, `Mouse (horizontalwheel)`, or `name (tick)`. Each execution retains its exact trigger and selected completed cycles while EVENTS groups repeated observations.

### HEALTH

HEALTH 显示连接、捕获、信任状态、Dry-run、`PAUSE`、DebugClient 故障、运行时问题数量和最近问题。执行器结束后显示 `Terminated` 和退出代码；完整的最终快照显示为 `Complete snapshot`，可能不完整的快照显示为 `Best-effort snapshot`。没有 Debug 会话时使用普通颜色；等待 Debug 启动或恢复捕获时使用恢复颜色，状态可信且没有问题时使用健康颜色，异常退出、best-effort 快照、故障或运行时问题使用故障颜色。

开始新的捕获会清空 DebugClient 根据上一代消息建立的 EVENTS、按下控制、变量和数组视图、ACTION EXECUTIONS、运行时问题以及未完成执行关联，再由新的完整快照和后续增量重新建立可信视图；这个过程不会重置执行器内部的用户变量、数组或 `PAUSE`。

## TUI 按键保护

宿主启动每个 `InputWeaver.exe` 时都会附加 `--exclude InputWeaverTUI.exe`。Exclude 使用与 Target 相同的进程定位器，并随前台窗口变化重新定位；前端关闭并重新创建后保护继续生效。排除选择命中前台进程时，执行器阻止普通分派并放行对应物理输入，同时阻止新的 `down` 和 `again` 输出；保护同样应用于 Global 目标。为清理执行器已经持有的控制而产生的释放仍然允许通过。

Debug EVENTS 仍可观察这些原始输入。退出规则先于排除判断，命中时仍接管输入并停止执行器；模拟运行时物理输入始终放行。命令行排除选择器的完整解析规则和生命周期见[运行安全指南](safety-guide.md)与[运行边界](runtime-boundaries.md)。


## 配色

托盘宿主启动时读取 `InputWeaverHost.exe` 同目录下的 `res\InputWeaverTUI.colors.json`。所有颜色使用 `#RRGGBB`；修改后需要重新启动 InputWeaver。文件缺失、版本错误、字段缺失、字段重复、包含未知字段或颜色值无效时，托盘宿主会报告错误并停止启动。
