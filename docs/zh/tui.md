# 界面操作

[文档首页](README.md) · [第一次使用](getting-started.md)

运行 `InputWeaverHost.exe` 打开界面。Program 页管理程序和源码，Console 页查看输出，Debug 页观察输入和动作。

## 页面与区域导航

| 当前页面 | 按键 | 去向 |
| --- | --- | --- |
| Program | `[` | Console |
| Program | `]` | Debug |
| Console | `]` 或 `Esc` | Program |
| Debug | `[` | Program |

Program 和 Debug 页面包含多个区域。方向键选择区域，`Enter` 进入后操作该区域的内容，`Esc` 退回区域选择。`Tab` 会直接进入下一个区域，并在末尾返回第一个。

`Esc` 按层次返回：先退出编辑，再退出文档全屏或当前区域，最后从 Program 的区域选择状态返回托盘。切换页面会保留各页的选择、滚动和文档显示状态。输入框和弹窗有自己的按键操作；源码编辑中的方括号作为文本输入。

## 新建和导入

进入 Program 页左侧的 PROGRAM 列表后：

| 按键 | 操作 |
| --- | --- |
| 上、下方向键 | 选择程序 |
| `A` | 打开 Add Program |
| `D` | 确认删除所选程序 |
| `M` | 调整列表顺序 |

Add Program 用左右方向键选择 `New Blank`、`Import .weave` 或 `Cancel`，再按 `Enter`。

`New Blank` 先打开名称输入框，确认名称后创建空白源码并进入 SOURCE 区域。`Import .weave` 接受一个已有源文件，路径可以输入、用 `Ctrl+V` 粘贴，或向窗口拖放文件。导入会把源码复制到程序库，校验并编译成功后加入列表；之后编辑的是程序库中的副本。

名称冲突时，可以覆盖、换名导入或取消。覆盖会沿用原条目的位置和运行配置。删除会先停止所选程序，再移除它的源码、编译文件和条目；日志文件保留。

调整顺序时用上下方向键移动，`Enter` 保存，`Esc` 放弃。

## 名称、目标和日志

Program 页右上方的 PROGRAM INFORMATION 有三个字段。进入区域后用上下方向键选字段，按 `Enter` 编辑。

| 字段 | 内容 |
| --- | --- |
| Name | 程序名称，同一程序库中的名称不区分大小写地保持唯一 |
| Target | 选择源码目标、指定应用或全局运行 |
| Logging | 选择日志级别 |

Target 的 `Compiled` 使用源码中的 `TARGET`；`Executable` 使用输入的可执行文件名或绝对路径；`Global` 在全局范围运行。选择 `Executable` 后，在同行输入框填写目标，`Enter` 保存，`Esc` 返回目标类型选择。[目标行为](running.md)

Logging 的三个选项为 `Off`、`Operational` 和 `Input Trace`。后两项分别记录运行日志，以及包含输入和输出轨迹的日志。文件保存在 `programs\logs\`，每次运行的日志路径会显示在 Console。[日志说明](debugging.md)

## 浏览和编辑源码

用 `Tab` 切到 SOURCE 区域。它显示行号、当前行和语法颜色；COMPILED DUMP 是编译内容的文本视图，可在排查编译结果时查看。

| 浏览按键 | 操作 |
| --- | --- |
| 上、下方向键 | 移动当前行 |
| 左、右方向键 | 水平滚动 |
| `PageUp`、`PageDown` | 翻页 |
| `Home`、`End` | 跳到首行、末行 |
| `E` | 编辑源码 |
| `Z` | 把文档区域铺满界面 |
| `V` | 切换 Source 与 Compiled Dump |

在 Compiled Dump 中按 `E` 会切回 Source 并进入编辑。文档全屏保留当前窗口大小；按 `Esc` 返回分栏。

### 编辑按键

| 按键 | 操作 |
| --- | --- |
| 方向键、`PageUp`、`PageDown` | 移动光标 |
| `Home`、`End` | 移到当前行首、行尾 |
| `Enter` | 换行 |
| `Tab` | 插入四个空格 |
| `Backspace`、`Delete` | 删除文本或选区 |
| `Shift` 加移动键 | 扩展选区 |
| `Ctrl+C`、`Ctrl+X`、`Ctrl+V` | 复制、剪切、粘贴 |
| `Ctrl+Z`、`Ctrl+Y` | 撤销、重做 |
| `Esc` | 保存并退出编辑 |

全屏编辑时，第一次 `Esc` 退出编辑，第二次才返回分栏。源码大小最多为 16 MiB。

### 自动保存和校验

停止输入约 400 毫秒后，源码会自动保存并校验。离开编辑、切换程序、生成 Dump、运行和删除之前，也会先保存。

错误行使用暗红底色，具体错误位置用亮红色和下划线标出。自动校验保留当前编辑页面；保存、正式编译或启动失败时，会转到 Console 显示输出。

按空格运行时，界面会检查编译文件是否对应当前源码，需要时先重新编译。修改源码后，正在运行的执行器继续使用启动时的程序；停止并重新运行后才使用修改后的内容。

## 运行与停止

NEXT RUN 表示下一次运行的选项。在 Program 页的非编辑状态下可以使用：

| 按键 | 界面选项 | 作用 |
| --- | --- | --- |
| `T` | Trace and Debug | 开启调试，启动后进入 Debug 页 |
| `S` | Skip Simulated Input | 模拟运行：保留规则和动作计算，物理输入放行，输出只做模拟 |
| `P` | Authorize Execution Permission | 允许本次程序使用 `exec` 启动外部进程 |
| 空格 | Run | 按当前选项运行所选程序 |
| `X` | Stop | 停止所选程序 |

成功启动后，NEXT RUN 的选项会恢复为 `OFF`。已经运行的程序再次按空格会保持当前运行；修改下一次运行选项后，需要先停止再运行。

每个程序最多运行一个执行器，不同程序可以同时运行。列表中的 `[RUN]` 表示普通运行，`[DBG]` 表示调试运行，`[DRY]` 表示模拟运行，`[EXEC]` 表示已授权外部进程启动。

整个应用同时管理一个 Debug 执行器。启动另一个程序的 Debug 会先停止原来的 Debug 执行器。调试启动前有约 500 毫秒的延迟，方便松开启动按键；等待时显示 `STARTING`，可按 `X` 取消。[调试操作](debugging.md)

## 查看 Console

Console 汇总应用、编译器和执行器的输出，保留最近 2048 条原始输出行。行首的程序和来源标签帮助区分多个程序的消息。

用上下方向键逐行滚动，`PageUp`、`PageDown` 翻页，`Home`、`End` 跳到最早或最新内容。普通程序启动失败、标准错误输出或异常退出时，界面会转到这里；Debug 的异常退出会留在 Debug 查看最终状态，相关输出仍可在 Console 找到。

## 窗口和托盘

窗口可以移动和缩放。最小化后保留任务栏按钮；关闭窗口，或从 Program 的区域选择状态按 `Esc`，会进入托盘后台，正在运行的执行器继续工作。

单击或双击托盘图标恢复界面。右击图标可以选择 `Show TUI`、`Hide TUI` 或 `Exit InputWeaver`。`Exit InputWeaver` 退出 InputWeaver，并停止界面管理的所有 Weave 程序。

通过界面启动的程序会自动排除 `InputWeaverTUI.exe`，普通规则和映射不响应操作界面的输入。这也适用于全局程序；[退出规则仍优先处理](running.md#排除应用)。Debug 仍可显示这些原始输入，普通规则是否触发应在目标应用前台时验证。

## 备份和迁移程序库

程序库位于 `InputWeaverHost.exe` 同目录的 `programs` 文件夹。退出 InputWeaver 后复制整个文件夹，恢复时把它放到新安装位置的 `InputWeaverHost.exe` 旁，即可保留程序源码、名称、顺序和运行配置。

其中的 `.weave` 文件保存源码，`logs` 子文件夹保存已启用的运行日志。只导入单个源码文件时，按[新建和导入](#新建和导入)操作。

## 修改配色

配色文件是 `InputWeaverHost.exe` 同目录下的 `res\InputWeaverTUI.colors.json`，颜色使用 `#RRGGBB`。保留现有字段和结构，修改颜色后从托盘退出并重新启动 InputWeaver。文件格式或字段出错时，启动会报告具体问题。

源码中的数组属性、当前鼠标状态和计量器字段都使用变量配色 `syntax_variable`，例如 `values.length`、`Mouse.x`、`path.progress` 和 `@path.dx`；点号使用正文配色 `text`。鼠标按钮 `Mouse.Left` 和事件源 `Mouse:move` 中的 `Mouse` 使用控制配色 `syntax_control`。属性和字段的命名规则见[语言基础](language.md#属性与字段速查)。
