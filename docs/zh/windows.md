# Windows 执行器

[文档首页](README.md) · [输入映射与规则](rules.md) · [命令行](command-line.md)

这篇说明 Windows 执行器的控制编码、输入输出能力和外部进程路径处理。Weave 的类型、规则和动作语义分别见[语言基础](language.md)、[输入映射与规则](rules.md)和[动作与流程控制](actions.md)。

## 控制编码与能力

Windows 专用控制使用 `Windows.` 命名空间。下面是编译器接受的原始编码范围，编码可使用十进制或小写 `0x` 开头的十六进制。

| 写法 | 编码范围 |
| --- | --- |
| `Windows.VirtualKey(code)` | `0` 到 `0xFF` |
| `Windows.ScanCode(code)` | `0` 到 `0xFF`，普通扫描码 |
| `Windows.ScanCode(code, E0)` | `0` 到 `0xFF`，E0 扩展扫描码 |
| `Windows.ScanCode(code, E1)` | `0` 到 `0xFF`，E1 扩展扫描码 |

```weave
Windows.VirtualKey(0x41) -> B;
```

这个例子把 Windows 虚拟键 `0x41` 映射为 B。`Windows.Keyboard.IMEOn` 是虚拟键 `0x16` 的命名写法。

用作事件来源时，控制必须能接收对应输入；用于 `held`、`idle` 条件时，必须能查询物理状态；用于 `press`、`release`、`tap` 或映射目标时，必须支持输出。使用了不支持的用途时，执行器会在启动时报错。Windows 的支持范围如下：

| 控制表示 | 输入观察 | 物理状态查询 | 输出 |
| --- | --- | --- | --- |
| [命名键盘键和多媒体键](rules.md#按键名称速查) | 按物理报告分派 `down`、`again`、`up` | 支持 | 按下、松开和重复按下 |
| `Mouse.Left`、`Mouse.Right`、`Mouse.Middle`、`Mouse.X1`、`Mouse.X2` | 按物理报告分派 `down`、`up` | 支持 | 对应鼠标按钮 |
| 对应上述命名控制的 `HID.Usage(page, usage)` | 与命名控制相同 | 与命名控制相同 | 与命名控制相同 |
| `Windows.VirtualKey(code)`，code 为 `1` 到 `0xFF` | 按该虚拟键对应的键盘或鼠标报告分派 | 支持 | 键盘虚拟键输出或对应鼠标按钮 |
| 普通或 E0 的 `Windows.ScanCode`，code 为 `1` 到 `0xFF` | 按扫描码及前缀匹配键盘报告 | 能转换为 Windows 虚拟键时支持 | 按扫描码输出 |
| 原始 E1 扫描码，例如 `Windows.ScanCode(0x45, E1)` 表示 Pause | 按扫描码及前缀匹配；Pause 使用 `0x45` | 能转换为 Windows 虚拟键时支持，Pause 有专门处理 | 输入专用 |

Windows 支持上述键盘键、五个鼠标按钮和七个多媒体键的 HID 写法，对应 Usage Page 分别为 `0x07`、`0x09`、`0x0C`。原始 Windows 虚拟键和扫描码在运行时使用非零编码。原始 E1 表示用于输入观察；需要输出 Pause 时可使用命名控制，例如 `tap(Pause)`。

部分原始扫描码可接收事件，但启动时无法转换为虚拟键来查询物理状态。这类来源先观察到一次物理释放，再建立输入基线；使用它的 `held` 或 `idle` 条件时，启动能力检查会报告失败。[启动时的按键状态](running.md#启动时已经按住的键)

同一个物理输入应统一使用一种控制表示。`A`、`Keyboard.A` 和 `HID.Usage(0x07, 0x04)` 是可互换的别名；`Windows.VirtualKey(0x41)` 属于另一种表示。若同时把后者和 `A` 用作输入来源或状态查询，Windows 执行器会因输入重叠而拒绝启动。选择一种表示后，可在多条规则中复用。

命名键盘控制通常按扫描码输出，`Windows.VirtualKey` 按虚拟键输出。目标应用最终收到的字符还取决于键盘布局、输入法和修饰键状态。目标窗口和指针位置的资格检查见[运行行为与限制](running.md)，鼠标坐标和滚动量见[鼠标与计量器](mouse.md)。

## 外部进程的查找和工作目录

Windows 的 `exec` 先从命令字符串中解析可执行文件名，再直接启动进程。包含空格的可执行文件路径应加双引号；Weave 字符串中的双引号写成 `\"`，反斜杠写成 `\\`。

| 可执行文件的写法 | 查找方式 |
| --- | --- |
| 绝对路径，例如 `C:\Tools\Helper.exe` | 使用指定文件 |
| 带目录的相对路径，例如 `.\tools\Helper.exe` | 相对于执行器进程的当前工作目录解析 |
| 文件名，例如 `Helper.exe` | 依次查找执行器所在目录、执行器当前工作目录、Windows 系统目录、Windows 目录下的 `System`、Windows 目录、`PATH` 中的各目录 |

普通无扩展名的可执行文件名会补上 `.exe`。通过宿主启动的执行器以产品所在目录为当前工作目录；从命令行启动时，工作目录来自启动它的命令行环境。

子进程启动后的工作目录固定为解析出的可执行文件所在目录。例如：

```weave
F6:down => exec("\"C:\\Tools\\Helper.exe\" --config settings.json");
```

此时 Helper 的工作目录是 `C:\Tools`。如果 Helper 按当前工作目录解释参数里的相对文件名，`settings.json` 就对应 `C:\Tools\settings.json`。需要让参数指向指定位置时，传入绝对路径。

命令解释器的管道或重定向由明确启动的解释器处理。进程创建成功后，`exec` 继续执行后续动作；本次启动授权和模拟运行行为见[启动外部程序](actions.md#启动外部程序)。
