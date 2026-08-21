# Weave 语法说明 v2

## 文档定位

本文档定义 Weave v2 相对于 Weave v1 的语言增量。除本文明确替换的控制引用、控制名称和控制身份规则外，`grammar.v1.md` 的词法、类型、规则、动作流、`PAUSE` 和运行语义继续成立；两份文档发生冲突时以本文档为准。

Weave v2 使用平台无关、可扩展且可确定比较的离散控制身份。键盘键、鼠标按钮、消费类控制和平台专用按钮可以进入同一套事件、状态、映射与输入动作语法，但连续坐标、鼠标移动、滚轮增量和模拟轴不是离散控制，不使用本章的 `ControlId` 表示。

## 设计目标

- 源码使用简洁且稳定的规范名称，不暴露某个平台的默认原生编号。
- 同一个可移植控制在不同平台上编译成同一个 `ControlId`，平台适配器只负责原生事件与该身份之间的转换。
- 平台独有控制使用显式平台限定名称，不伪装成可移植控制，也不在其他平台上静默替换成近似按键。
- 名称绑定与平台能力分离：编译器可以确定地产生程序，运行时在安装输入钩子之前检查当前平台能否完成程序实际要求的输入观察、状态查询和输出动作。
- 完整身份不依赖一个全局平面枚举；增加标准页、控制族或平台命名空间时不改变既有身份的含义。
- 热路径使用池化后的 `ControlRefId`，不在每次输入事件上比较字符串或查询名称表。

## 控制引用语法

控制引用可以是目录中的规范名称、规范名称的短别名或带参数的原始身份构造式：

```ebnf
control-reference     = named-control | raw-control ;
named-control         = control-segment, { ".", control-segment } ;
control-segment       = ASCII-letter, { ASCII-letter | digit | "_" } ;
raw-control           = hid-usage | windows-virtual-key | windows-scan-code | linux-key | macos-key-code ;
hid-usage             = "HID.Usage", "(", unsigned-code, ",", unsigned-code, ")" ;
windows-virtual-key   = "Windows.VirtualKey", "(", unsigned-code, ")" ;
windows-scan-code     = "Windows.ScanCode", "(", unsigned-code, [ ",", scan-prefix ], ")" ;
linux-key             = "Linux.Key", "(", unsigned-code, ")" ;
macos-key-code        = "MacOS.KeyCode", "(", unsigned-code, ")" ;
scan-prefix           = "None" | "E0" | "E1" ;
unsigned-code         = decimal-integer | hexadecimal-integer ;
hexadecimal-integer   = "0x", hex-digit, { hex-digit } ;
```

原始身份构造式只在语法期待控制引用的位置成立，因此不会与动作调用混淆。`unsigned-code` 必须能表示为 32 位无符号整数；十六进制数字中的 `A` 至 `F` 不区分大小写，`0x` 前缀固定为小写。

```weave
A := B;
Consumer.VolumeUp:down ~> tap(Consumer.Mute);
Windows.Keyboard.IMEOn:down => tap(Enter);
HID.Usage(0x000C, 0x00E9):down ~>;
Windows.ScanCode(0x45, E1):down =>;
```

原始身份构造式是目录尚未提供易读名称时的逃生口。它使身份可表达，不承诺该编号代表离散控制，也不承诺当前平台能够观察、查询或输出该控制。编译器能从标准目录确定该身份不是离散控制时必须报编译错误；目录无法分类的原始身份由激活阶段要求后端证明其离散事件语义和实际能力，否则激活失败。

## 名称空间与规范名称

### 可移植名称

可移植名称不带操作系统前缀。v1 已有的 `A`、`F6`、`LCtrl`、`Mouse.Left` 等名称继续有效，并绑定到 v2 的稳定身份。v2 同时提供分类后的规范全名；短名称只是源码别名，不能形成第二个身份。

```weave
A
Keyboard.A
F6
Keyboard.F6
LCtrl
Keyboard.LCtrl
Mouse.Left
Consumer.VolumeUp
```

`A` 与 `Keyboard.A` 必须编译成同一个 `ControlId`，`F6` 与 `Keyboard.F6` 也必须编译成同一个 `ControlId`。确定性程序转储只打印规范全名和完整身份；诊断可以同时打印用户实际书写的别名。

### 平台限定名称

平台独有身份以 `Windows.`、`Linux.` 或 `MacOS.` 开头。平台前缀是身份的一部分，不是条件编译指令。

```weave
Windows.Keyboard.IMEOn
Linux.Keyboard.Compose
```

平台限定控制可以参与普通事件、状态、完整映射和输入动作。程序只要实际要求了当前后端不支持的控制或能力，激活就必须失败并报告具体控制和能力；运行时不得删除对应规则、改用近似控制或把平台控制解释为同编号的另一平台控制。

目录名称 `Windows.Keyboard.IMEOn` 规范化为与 `Windows.VirtualKey(0x16)` 相同的身份。平台限定目录中的每个易读名称都必须具有这种公开且可测试的唯一绑定，目录不能根据编译主机动态改变名称含义。

### 标准限定名称

`HID.` 表示 USB HID Usage Tables 中的页和用法身份，`Weave.` 表示由语言规范定义但不直接等同于某个平台原生编号的身份。面向用户的 `Keyboard.`、`Mouse.` 和 `Consumer.` 规范名称分别绑定到这两个稳定目录中的身份。

平台后端可以支持一个并非由该平台原生键盘 API 直接命名的 HID 控制。是否支持取决于后端能力和输出配方，而不取决于控制名称是否带平台前缀。

## 物理控制与文本的边界

键盘控制名称描述离散物理控制身份，不描述当前键盘布局产生的 Unicode 字符。`Keyboard.A` 对应 HID Keyboard/Keypad 页中标为 Keyboard `a` and `A` 的用法位置；它在不同布局上仍保持同一个控制身份，即使该位置最终产生的字符不是 `a`。

`Keyboard.Digit1` 表示主键区对应的物理数字行控制，不表示字符串字符 `"1"`；`Keyboard.Numpad1` 是另一控制。输入文字、输入 Unicode 字符和执行输入法编辑不是控制引用，不能通过把字符强制转换成 `ControlId` 来实现。

左右修饰键继续具有不同身份。跨平台中性的系统修饰键名称使用 `Keyboard.LMeta` 与 `Keyboard.RMeta`；平台后端可以把它们映射到对应的 GUI、Command 或 Super 物理控制。名称不承诺目标应用对该控制采用相同的用户界面称呼。

## 稳定中间身份

每个离散控制使用以下逻辑结构进入 v2 编译结果：

```cpp
struct ControlId final {
    std::uint32_t namespaceId{};
    std::uint32_t familyId{};
    std::uint32_t code{};
    std::uint32_t qualifier{};

    auto operator<=>(const ControlId&) const = default;
};
```

四元组的完全相等就是控制身份相等。任何层都不得只比较 `code`，因为相同数值可以在不同名称空间或控制族中表示完全不同的控制。

| `namespaceId` | 名称空间 | `familyId` | `code` | `qualifier` |
|---|---|---|---|---|
| `1` | USB HID | Usage Page | Usage ID | `0` |
| `2` | Weave | 语言定义的控制族 | 族内稳定编号 | `0` 或该控制族定义的限定值 |
| `256` | Windows | `1` 表示 Virtual-Key，`2` 表示 Scan Code | 原生编号 | Scan Code 使用 `0=None`、`1=E0`、`2=E1`，其他族为 `0` |
| `257` | Linux | `1` 表示 `EV_KEY` | `KEY_*` 或 `BTN_*` 编号 | `0` |
| `258` | macOS | `1` 表示原生 Key Code | 原生编号 | `0` |

名称空间编号和既有族编号一旦发布就不能改义或复用。新增名称空间或控制族需要分配新编号，但不需要改变 `ControlId` 的结构。`namespaceId = 0` 保留为无效身份。

`HID.Usage(page, usage)` 直接产生 `{1, page, usage, 0}`。`Windows.VirtualKey(code)` 产生 `{256, 1, code, 0}`。`Windows.ScanCode(code, prefix)` 产生 `{256, 2, code, prefix}`。Linux 和 macOS 原始构造式按上表产生身份。

编译结果中的控制池保存完整 `ControlId`，其他表只保存池索引 `ControlRefId`。编译收尾阶段按 `ControlId` 排序、去重并重写全部引用，因此源码别名和重复使用不会增加热路径身份数量。

`ControlId` 只回答“这是哪个控制”，不携带“当前平台能用它做什么”。规范名称、离散控制分类、前台目标或指针目标路由等稳定目录元数据与身份关联保存，但不参与身份相等比较；事件观察、状态查询、输出按下与松开、输出重复和具体发送配方属于后端能力。平台 API 需要的键盘、鼠标或其他设备分类也由后端解析结果持有，不再把封闭的 `DeviceKind` 当作跨平台身份的一部分。

## 可移植目录的基础绑定

键盘基础目录使用 USB HID Keyboard/Keypad Usage Page `0x07`。下列绑定既定义短名称，也定义对应的 `Keyboard.` 规范全名：

| Weave 名称 | HID Usage |
|---|---|
| `A` 至 `Z` | `0x04` 至 `0x1D` |
| `Digit1` 至 `Digit9`、`Digit0` | `0x1E` 至 `0x27` |
| `Enter`、`Esc`、`Backspace`、`Tab`、`Space` | `0x28` 至 `0x2C` |
| `CapsLock` | `0x39` |
| `F1` 至 `F12` | `0x3A` 至 `0x45` |
| `ScrollLock`、`Pause` | `0x47`、`0x48` |
| `Insert`、`Home`、`PageUp`、`Delete`、`End`、`PageDown` | `0x49` 至 `0x4E` |
| `ArrowRight`、`ArrowLeft`、`ArrowDown`、`ArrowUp` | `0x4F` 至 `0x52` |
| `NumLock` | `0x53` |
| `NumpadDivide`、`NumpadMultiply`、`NumpadSubtract`、`NumpadAdd` | `0x54` 至 `0x57` |
| `Numpad1` 至 `Numpad9`、`Numpad0`、`NumpadDecimal` | `0x59` 至 `0x63` |
| `F13` 至 `F24` | `0x68` 至 `0x73` |
| `LCtrl`、`LShift`、`LAlt`、`LMeta`、`RCtrl`、`RShift`、`RAlt`、`RMeta` | `0xE0` 至 `0xE7` |

鼠标离散按钮使用 HID Button Usage Page `0x09`。规范名称 `Mouse.Button1` 至 `Mouse.Button5` 分别使用 Usage `1` 至 `5`；兼容名称 `Mouse.Left`、`Mouse.Right`、`Mouse.Middle`、`Mouse.X1`、`Mouse.X2` 依次是这五个身份的别名。

消费类名称使用 HID Consumer Usage Page `0x0C`。目录至少提供 `Consumer.PlayPause`、`Consumer.ScanNextTrack`、`Consumer.ScanPreviousTrack`、`Consumer.Stop`、`Consumer.Mute`、`Consumer.VolumeUp` 和 `Consumer.VolumeDown`，并分别绑定到 Usage `0x00CD`、`0x00B5`、`0x00B6`、`0x00B7`、`0x00E2`、`0x00E9` 和 `0x00EA`。

## 编译绑定与平台激活

编译器按以下顺序处理控制引用：

1. 对目录名称执行区分大小写的精确查找，并把别名规范化为唯一 `ControlId`。
2. 对原始身份构造式执行语法、范围和保留值检查，直接构造 `ControlId`。
3. 根据使用位置合并 `ControlRequirement`，包括事件源、物理状态、输出按下与松开、输出重复等能力。
4. 把完整身份写入控制池，并让事件、表达式、映射和动作只引用 `ControlRefId`。

编译器不把 `ControlId` 转换成 Windows Virtual-Key、Windows Scan Code、Linux `KEY_*` 或其他原生发送结构。原生表示、扩展前缀、多事件配方和平台 API 参数属于平台适配器。

运行时在安装钩子和接收物理输入之前解析全部控制要求。每个要求必须同时找到原生输入归一化规则和所需输出配方；缺少任一实际使用的能力都会让激活失败。仅在条件中读取的控制不要求输出能力，仅作为输出目标的控制不要求物理状态能力。

一个输出配方可以产生多个原生事件，不能假设一个 `ControlId` 等于一个原生整数或一次系统调用。Windows 的普通扫描码、扩展扫描码和 E1 特例因此可以共享统一的上层身份，而由 Windows 适配器保留各自正确的发送方法。

## 运行时转换路径

```text
native input
    -> platform normalizer
    -> ControlId / activated ControlRefId
    -> physical state and Weave rules
    -> requested output ControlRefId
    -> platform output recipe
    -> native injected events
```

平台归一化器必须让同一物理候选控制稳定地产生同一个 `ControlId`。平台原生事件包含的时间戳、注入来源、坐标、扫描信息和其他诊断数据可以作为事件元数据保留，但不能混入控制身份的相等比较。

一个后端可以把多个原生编码规范化为同一稳定身份，但不能根据当前规则上下文把同一原生编码随机解释为不同身份。确实受布局或模式影响的输入必须由后端根据明确的原生状态完成归一化，并在调试信息中记录选择结果。

## C++ 名词与边界

平台无关代码使用 `ControlId` 表示稳定身份，使用 `ControlRefId` 表示编译程序内的稠密引用，使用 `ControlRequirement` 表示所需能力。`ControlCode` 不再承担平台无关身份，因为一个没有名称空间的整数无法区分 HID Usage、Windows Virtual-Key、Windows Scan Code 和 Linux `KEY_*`。

可移植常量必须体现标准目录，例如 `control_id::keyboard::kF6` 表示 HID 身份。Windows 原生值只能出现在 Windows 适配器命名空间，例如 `windows::virtual_key::kF6` 或 `windows::scan_code::kF6`。禁止在 `core` 中用 `kF6 = 0x75` 这类看似通用、实际等于 Windows Virtual-Key 的定义。

名称目录是数据表，不为每个源码别名复制运行时分支。推荐的目录项包含源码名称、规范名称、`ControlId` 和静态语法特征；平台能力与输出配方由后端表按 `ControlId` 提供。

## 诊断与调试输出

未知名称是编译错误。合法但当前后端不支持的身份是激活错误。合法身份只缺少特定能力时，诊断必须指出缺少的是事件源、物理状态、输出按下与松开还是输出重复，而不是笼统报告“按键不可用”。

确定性程序转储使用以下形式显示控制：

```text
Keyboard.F6 [ns=1 family=0x0007 code=0x003F qualifier=0]
Windows.Keyboard.IMEOn [ns=256 family=1 code=0x0016 qualifier=0]
```

输入跟踪在安全策略允许显示控制身份时也使用规范名称和完整四元组。平台适配器可以追加原生事件信息，但原生编号不能替代规范身份。

## v2 一致性要求

- 同一源码名称在所有编译主机上产生同一个 `ControlId`。
- 一个短别名与其规范全名产生完全相同的 `ControlId` 和控制池项。
- 不同名称空间中数值相同的控制保持不同身份。
- 平台限定名称在不支持它的后端上确定地激活失败，不发生静默降级。
- 原始身份构造式和目录名称经过规范化后可以合并成同一控制池项。
- 当前键盘布局不会改变 HID 键盘控制的身份，但可能改变目标应用最终解释出的字符。
- 平台输入归一化与输出配方对每个受支持身份形成可测试的往返关系；输入专用或输出专用控制必须通过能力表明确表达。

## 规范依据

- USB-IF 的 [HID Specifications and Tools](https://www.usb.org/hid) 页面发布 HID Usage Tables，并说明 Usage 用于标识集合和数据项的用途；v2 的 USB HID 名称空间以该表的 Usage Page 与 Usage ID 为身份基础。
- Microsoft 的 [Virtual-Key Codes](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes) 文档定义 Windows Virtual-Key 常量和值；这些值只进入 `Windows` 名称空间和 Windows 适配器。
- Linux 内核的 [Input event codes](https://docs.kernel.org/input/event-codes.html) 文档定义 `EV_KEY` 的 `KEY_*` 与 `BTN_*` 事件代码；这些值只进入 `Linux` 名称空间和 Linux 适配器。
- Apple 的 [CGKeyCode](https://developer.apple.com/documentation/coregraphics/cgkeycode) 文档定义 macOS 键盘事件使用的虚拟键码；这些值只进入 `macOS` 名称空间和 macOS 适配器。
