# InputWeaver TUI 使用指南

`InputWeaverHost.exe` 是程序库、源码编辑、运行配置、执行器控制和输入调试的统一入口。它仍然通过独立的 `InputWeaverCompiler.exe` 生成 `.weavec`，再启动独立的 `InputWeaver.exe`；TUI 不把源码直接交给运行时，也不在内存中把编译程序传给执行器。无控制台托盘宿主持有应用状态，随附的 `InputWeaverTUI.exe` 只提供可关闭和重新创建的原生 Windows 界面。

当前 Weave 语法、类型、规则匹配和动作语义统一定义在发行包的 `docs\grammar.md`。

## 启动与页面

Windows 10 或 Windows 11 x64 用户解压完整的 `InputWeaver-windows-x64.zip`，进入其中的 `InputWeaver` 文件夹并运行 `InputWeaverHost.exe`。不要在 ZIP 内直接运行，也不要拆散同目录中的四个 EXE 和 `res` 文件夹；发行版已静态链接 MinGW 的 GCC 与 C++ 运行库，不要求目标电脑安装 MinGW。源码仓库的维护者可以运行 `script\package_release.bat`，在 `bin\release\` 中重新生成同样的目录和 ZIP。

前端窗口至少保留 `80x24` 个文本单元格。程序启动后进入 Program 页；Program、Console 和 Debug 是三个顶层页面。顶部边框使用 `InputWeaver | ‹[ Console · PROGRAM · Debug ]›` 形式显示固定页面顺序，当前页面使用大写，整段页面轨道使用当前页面或区域颜色突出显示；普通模式下，可用按键说明显示在顶部边框内。

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

SOURCE 显示程序库中的 `.weave` 源码副本，包含行号、弱化的竖向分隔线、当前行底色和语法高亮。浏览源码时，方向键和 `[PageUp]` / `[PageDown]` 移动视图位置，`[Home]` 跳到第一行，`[End]` 跳到最后一行。

语法颜色参考 Visual Studio Code 的默认配色并按 Weave 语义归类：

| 类别 | 示例 | 默认颜色 |
| --- | --- | --- |
| 结构关键字 | `when`、`repeat`、`if`、`else` | 紫色 |
| 类型名 | `state`、`number`、`duration` | 青绿色 |
| 变量、数组和内蕴值 | 用户变量、用户数组、`TARGET`、`PAUSE`、`TAP_DURATION`、`ACTION_GAP` | 浅蓝色 |
| 常量 | `GLOBAL`、`on`、`off`、`held`、`idle`、`down`、`again`、`up`、数字、时长 | 浅绿色 |
| 控制名 | `A`、`LCtrl`、`Mouse.Left`、`Windows.VirtualKey` | 亮蓝色 |
| 动作和分隔符 | `tap`、`set`、`toggle`、`append`、`pop`、`clear`、`|` | 黄色 |
| 规则和映射箭头 | `->`、`~>`、`=>`、`=>>`、`~>>` | 亮白色 |
| 普通源码文本 | `and`、`or`、`not`、括号、方括号、逗号、冒号、分号、点和表达式运算符 | 默认前景色 |
| 字符串 | `"..."` | 橙色 |
| 注释 | `//...`、`/*...*/` | 绿色 |

`[V]` 在 Source 和 Compiled Dump 之间切换。没有已保存的 Dump 时，切换会立即调用编译器生成；生成失败会进入 Console。源码一旦保存，旧 Dump 会被移除，原 `.weavec` 文件可以暂时保留，但它的源码摘要不再匹配，因此不会被下一次运行复用。

`[Z]` 切换文档全屏。这里的全屏只隐藏 PROGRAM 和 PROGRAM INFORMATION，不改变前端窗口状态。进入全屏不会自动进入编辑，源码和 Dump 都可以全屏浏览；切换到其他顶层页面再返回 Program 时仍保持文档全屏。

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

应用操作失败、编译或运行启动失败、执行器写入标准错误，或者执行器以非零代码退出时，TUI 会自动切换到 Console 并保留对应输出。普通运行信息不会抢走当前页面，源码自动校验诊断仍留在源码视图中。

使用 `[Up]` / `[Down]` 逐行滚动，`[PageUp]` / `[PageDown]` 整页滚动，`[Home]` 跳到第一行，`[End]` 跳到最新一行，`]` 或 `[Esc]` 返回 Program。

## Debug 页

Debug 页由 EVENTS、STATE、ACTION EXECUTIONS 和固定高度的 HEALTH 组成。页面首先处于区域选择状态：EVENTS 的 `[Right]` 进入 STATE、`[Down]` 进入 ACTION EXECUTIONS，STATE 的 `[Left]` 返回 EVENTS、`[Down]` 进入 ACTION EXECUTIONS，ACTION EXECUTIONS 的 `[Up]` 返回 EVENTS。按 `[Enter]` 进入所选区域后，方向键、`[PageUp]` / `[PageDown]`、`[Home]` 和 `[End]` 滚动该区域；按 `[Esc]` 返回区域选择。

`[C]` 停止当前捕获或开始一个新的捕获代次，但不停止 Debug 执行器。`[X]` 停止 Debug 执行器，或者在延迟启动期间取消待启动操作。`[` 返回 Program；区域选择状态下也可以使用 `[Esc]` 返回 Program。

### EVENTS

EVENTS 的每一行依次显示捕获时间、控制、转换、来源和处置结果，并可能附加 `AGAIN` 或 `NO-DOWN`：

```text
16:28:38.582  A                 down     PHY   PASS
16:28:38.610  B                 down     SYN   DROP
16:28:38.700  F22               down     INIT  -
```

来源 `PHY` 表示物理候选输入，`SYN` 表示注入来源，`INIT` 表示开始捕获时取得的已按下控制快照。`INIT` 不是捕获后发生的一次新按下；它只把初始按下状态送入 DebugClient，因此转换固定为 `down`，处置结果固定为 `-`。例如 `F22 down INIT -` 表示 Windows 在捕获开始时报告 F22 已处于按下状态。

`PASS` 表示运行时放行当前输入，`DROP` 表示运行时决定消费当前输入；在 Dry-run 中，界面仍显示正常模式下的逻辑决定，但物理输入最终始终放行。`AGAIN` 表示按下发生在已有按下状态之后，`NO-DOWN` 表示释放前没有对应的已知按下。

### STATE

STATE 先显示全部用户 `state`、`number` 和 `duration` 当前值以及全部数组，再显示当前按下的控制；`off`、`0` 和空数组也会显示。数组始终在名称后显示逻辑长度，分别显示为 `[values[0]=[]]`、`[values[3]=[2, 4, 8]]`，长数组显示为 `[values[1000]=[0, 1, 2, 3, ..., 996, 997, 998, 999]]`。项目按顺序使用固定最小间隔填充当前行，剩余宽度不足以容纳下一个完整项目时移到下一行；单个项目超过 STATE 区域宽度时才会换行，并随 STATE viewport 一起滚动。控制项同时标出其当前来源，例如 `[LCtrl PHY]` 或 `[F22 INIT]`。

开始捕获时，执行器发送 `PAUSE`、全部用户标量值和全部数组快照；实际运行产生的变化随后以增量方式同步。`PAUSE` 不重复放在 STATE 中，而是固定显示在 HEALTH。

### ACTION EXECUTIONS

每个匹配并进入执行管线的规则使用源码级文本显示：

```text
#17  EVENT 16:28:38.582  MATCH 16:28:38.582  A down (PASS)
  AS   a < 5
  ACT  tap(B) | set(a,a+1)
```

`EVENT` 是触发输入的捕获时间，`MATCH` 是规则匹配并建立执行记录的时间。执行首行不重复显示来源；处置结果使用括号显示在转换之后。`#编号`、`AS` 和 `ACT` 使用弱化色，条目其余部分统一使用当前执行状态颜色：运行中为青色、完成为深绿色、失败为红色、取消为黄色。条件和动作来自编译产物内保存的源码片段，而不是降低后的指令序列。

普通动作规则的 ACT 显示完整动作管线；完整按键映射虽然不经过普通动作程序，但也会建立执行记录，并把完整映射源码显示在 ACT。规则没有条件时 AS 显示 `always`。

### HEALTH

HEALTH 显示连接、捕获、信任状态、Dry-run、`PAUSE`、DebugClient 故障、运行时问题数量和最近问题。没有 Debug 会话时使用普通颜色；等待 Debug 启动或恢复捕获时使用恢复颜色，状态可信且没有问题时使用健康颜色，出现故障或运行时问题时使用故障颜色。

开始新的捕获会清空 DebugClient 根据上一代消息建立的 EVENTS、按下控制、变量和数组视图、ACTION EXECUTIONS、运行时问题以及未完成执行关联，再由新的完整快照和后续增量重新建立可信视图；这个过程不会重置执行器内部的用户变量、数组或 `PAUSE`。

## TUI 按键保护

宿主启动每个 `InputWeaver.exe` 时都会附加 `--exclude InputWeaverTUI.exe`。Exclude 使用与 Target 相同的进程定位器，并随前台窗口变化重新定位；前端关闭并重新创建后保护继续生效。排除选择命中前台进程时，执行器不会把操作 TUI 的物理输入送入规则分派，也不会消费这些输入或发布新的 `down` 和 `again` 输出；保护同样应用于 Global 目标。为清理执行器已经持有的控制而产生的释放仍然允许通过。

该保护不隐藏 Debug EVENTS 中观察到的原始输入，而是保证这些输入被放行且不触发新的映射或规则效果。命令行排除选择器的完整解析规则和生命周期见 `docs/safety-guide.md` 与 `docs/runtime-boundaries.md`。

## 配色

托盘宿主启动时读取 `InputWeaverHost.exe` 同目录下的 `res\InputWeaverTUI.colors.json`。所有颜色使用 `#RRGGBB`；修改后需要重新启动 InputWeaver。文件缺失、版本错误、字段缺失、字段重复、包含未知字段或颜色值无效时，托盘宿主会报告错误并停止启动。
