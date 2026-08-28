# InputWeaver TUI 使用指南

## 启动

运行 `script\verify_project.bat` 构建并验证全部产物，然后运行 `script\run_inputweaver_tui.bat`。`InputWeaverTUI.exe`、`InputWeaverCompiler.exe`、`InputWeaver.exe` 和 `res\InputWeaverTUI.colors.json` 共用 `bin\` 目录结构。

终端尺寸至少为 `80x24` 字符。顶部边框显示 `InputWeaver | 页面名称` 和当前可用按键；按键提示需要换行时会按各组占用宽度均衡分行，并在每行的左右边距、按键组和组间空白中分配多余宽度。顶部标题颜色跟随当前焦点区域。程序启动后进入 Programs 页；使用 `[Left]` 切换到 Console 页，使用 `[Right]` 切换到 Debug 页。

## Programs 页

Programs 页左侧显示已导入程序的纯列表，右上方显示 Target 和 Logging，右侧其余空间显示已保存的编译结果。`[Tab]` 依次切换程序列表、程序信息和编译结果区域的焦点。

- `[Up]` / `[Down]`：选择程序。
- `[A]`：输入一个 `.weave` 文件路径并导入；支持键入、粘贴以及终端拖放产生的路径文本。
- `[D]`：确认删除条目、编译产物和编译结果；已有日志文件会保留。
- `[R]`：修改显示名称。
- `[M]`：进入排序模式；使用 `[Up]` / `[Down]` 移动条目，`[Enter]` 保存，`[Esc]` 取消。
- `[Enter]`：将焦点移到 Target 和 Logging；使用 `[Up]` / `[Down]` 选择字段，使用 `[Enter]` 原位编辑。
- `[T]`：切换下一次运行是否连接 DebugClient，并在启动后自动切换到 Debug 页。
- `[S]`：切换下一次运行是否使用无注入模拟；无注入模拟会跳过模拟输入和 `EXEC` 进程创建。
- `[P]`：切换下一次运行是否授予 `EXEC` 进程创建权限。
- `[Space]`：使用当前 T/S/P 选项启动所选程序；条目已有执行器时保持不变。
- `[X]`：请求停止所选程序的执行器。
- `[Q]`：焦点位于程序信息或编译结果时返回程序列表；焦点位于程序列表时停止应用管理的全部执行器并退出。

正在运行的条目显示 `[RUN]` 或 `[DBG]`；无注入模拟会增加 `[DRY]`，执行权限会增加 `[EXEC]`。所选行始终使用反色高亮。

Target 支持 `Compiled`、`Executable` 和 `Global`。模式选择直接在 Program Information 区域原位显示。选择 `Executable` 后，`Executable ->` 后方会成为可水平滚动的行内文本框；使用 `[Left]`、`[Right]`、`[Home]`、`[End]`、`[Backspace]` 和 `[Delete]` 编辑，使用 `[Enter]` 确认，使用 `[Esc]` 返回模式选择。插入光标会闪烁，长文本水平滚动时始终保持可见。

Logging 支持 `Off`、`Operational` 和 `Input Trace`，同样在原位置选择。启用日志后，每次启动使用的相对路径会写入 Console，日志文件保存在 `programs\logs\`。

NEXT RUN 边框区域显示 `[T] Trace and Debug`、`[S] Skip Simulated Input` 和 `[P] Authorize Execution Permission`。选项布局与顶部按键提示使用相同的响应式分行和外边距。启用的选项使用配置的状态颜色。这三个按键不会在顶部按键提示中重复显示。

## TUI 按键保护

TUI 启动每个 `InputWeaver.exe` 时，会自动通过 `--exclude-process` 排除承载交互终端窗口的进程。InputWeaver TUI 位于前台时，这些执行器不会消费、映射或抑制用户操作 TUI 的物理按键，也不会向 TUI 注入新的输出；即使条目的实际目标是 `GLOBAL`，保护仍然生效，无需修改程序配置。

排除检查一直保留到 Windows 实际注入边界。安全清理仍可释放执行器先前持有的按键，但不会向 TUI 引入新的按下或重复。

## 导入与程序库

导入时会调用 `InputWeaverCompiler.exe compile` 验证源码并生成编译产物，然后调用 `dump` 保存可滚动查看的编译结果。编译失败不会新增或覆盖条目，并会自动切换到 Console 页显示编译器诊断。

名称使用 Windows 序号、不区分大小写的比较方式。名称冲突时可以选择覆盖、换名导入或取消。覆盖会保留原条目的 ID、顺序和运行配置。

程序库保存在 `bin\programs\`。`programs.index` 保存顺序，`.entry` 保存名称和配置，`.weavec` 保存编译程序，`.dump.txt` 保存编译结果。程序库不会复制 `.weave` 源文件。

`programs.index` 丢失时会根据完整条目重新生成。索引中的条目缺少 `.weavec` 时，该条目会被移除，并在 Console 页提示。

## Console 页

Console 页汇总应用、编译器和执行器输出。每组连续的同来源输出只在开始前显示一行 `[Program][Source]` 身份标记，后续输出行不再重复。使用 `[Up]` / `[Down]` 逐行滚动，`[PageUp]` / `[PageDown]` 整页滚动，`[Home]` / `[End]` 跳到第一行或最新一行，使用 `[Right]` 或 `[Q]` 返回 Programs 页。

## Debug 页

Debug 页包含带边框的 EVENTS、STATE、ACTION EXECUTIONS 和 HEALTH 区域。`[Tab]` 在前三个可滚动区域之间切换焦点；使用 `[Up]`、`[Down]`、`[PageUp]`、`[PageDown]`、`[Home]` 和 `[End]` 滚动当前焦点区域。

Debug 页使用 `[C]` 开始或停止捕获，使用 `[X]` 停止 Debug 执行器，使用 `[Left]` 或 `[Q]` 返回 Programs 页。这些按键作用于整个 Debug 页，不属于某个信息区域。

EVENTS 使用对齐的时间、控制、事件转换、来源和处置结果列，并在其后显示 `REPEAT` 与 `NO-DOWN` 标记。

STATE 使用多列显示当前处于按下状态的控制和全部用户 `state`、`number`、`duration` 当前值，包括值为 `off` 的状态，并支持与其他可滚动区域相同的滚动按键。

ACTION EXECUTIONS 的首行显示弱化的执行编号、`EVENT` 触发时间、`MATCH` 匹配时间、控制、事件转换和括号内的处置结果，不重复显示仅有一种可能的来源；`#编号`、`AS` 和 `ACT` 标签使用弱化色，其余首行内容、实际命中规则的源码级条件和完整源码级动作管线统一使用同一个执行状态颜色：运行中为青色、完成为深绿色、失败为红色、取消为黄色。完整按键映射也在此区域形成一条从按下到释放的执行记录，并在 `ACT` 行显示完整映射。

HEALTH 保持固定高度，在独立边框内显示连接状态、捕获状态、信任状态、捕获代次、`PAUSE`、Debug 故障、运行时问题数量和最近问题。`PAUSE` 与 STATE 中的用户值在开始捕获时取得完整快照，此后的实际更新由运行时增量同步。没有 Debug 故障或运行时问题时使用健康颜色；出现问题时改用配置的橙红色故障颜色。

Windows Debug 启动会延迟 500 毫秒，使用户能在输入捕获开始前松开启动按键。

应用全局最多管理一个 Debug 执行器。为另一个条目启动 Debug 时，会先停止当前 Debug 执行器。不同条目仍可同时普通运行，每个条目最多拥有一个执行器。

## 配色

程序启动时读取 `bin\res\InputWeaverTUI.colors.json`。颜色使用 `#RRGGBB` 格式；修改文件后需要重新启动 TUI。文件缺失、字段缺失、包含未知字段或颜色无效时，程序会报告错误并停止启动。
