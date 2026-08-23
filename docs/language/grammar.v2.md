# Weave 语法说明 v2

## 文档定位

Weave v2 是对 v1 的增量扩展，只扩展控制名称的写法。v1 的映射、事件、状态查询、动作和控制结构保持原有写法。

本文中的控制包括离散的键盘按键、鼠标按钮和多媒体按键。

## 使用位置

扩展后的控制名称可以出现在 v1 原本接受控制名称的所有位置，包括映射两侧、事件来源、`[held]` 与 `[idle]` 状态查询，以及 `press`、`release` 和 `tap` 动作的参数。

```weave
A := B;
Keyboard.F6:down => tap(Mouse.Left);
Consumer.VolumeUp:down ~> press(Keyboard.LCtrl);
```

## 命名控制

控制名称区分大小写。

v1 已有的简短名称继续有效，例如 `A`、`F6`、`LCtrl` 和 `Mouse.Left`。

带类别前缀的名称用于表达可移植控制，例如：

```text
Keyboard.A
Keyboard.F6
Keyboard.LCtrl
Mouse.Left
Mouse.X1
Consumer.PlayPause
Consumer.VolumeUp
```

已有简短名称和对应的完整名称表示同一个控制，例如 `A` 与 `Keyboard.A`、`F6` 与 `Keyboard.F6`。

带操作系统前缀的名称用于表达平台专有控制，例如：

```text
Windows.Keyboard.IMEOn
Linux.Keyboard.Compose
MacOS.Keyboard.Fn
```

平台前缀是名称的一部分。平台专有名称不会在其他平台上自动替换为名称相近的控制。

具体可用名称由控制名称目录确定。未知名称属于编译错误。

## 原始编号

当控制名称目录没有对应名称时，可以直接书写标准编号或平台原生编号。

```weave
HID.Usage(0x0007, 0x0004)
Windows.VirtualKey(0x41)
Windows.ScanCode(0x1E)
Windows.ScanCode(0x45, E1)
Linux.Key(30)
MacOS.KeyCode(0)
```

`HID.Usage(page, usage)` 使用 HID Usage Page 和 Usage ID 表示控制。

`Windows.VirtualKey(code)` 使用 Windows Virtual-Key 编号。

`Windows.ScanCode(code)` 使用没有扩展前缀的 Windows 扫描码；带有扩展前缀时写成 `Windows.ScanCode(code, E0)` 或 `Windows.ScanCode(code, E1)`。

`Linux.Key(code)` 使用 Linux 输入事件编号。

`MacOS.KeyCode(code)` 使用 macOS 键盘编号。

编号可以使用非负十进制整数或带 `0x` 前缀的十六进制整数。参数数量错误、负数、非整数、超出对应编号范围或无效的扫描码前缀都属于编译错误。

原始编号写法可以出现在命名控制能够出现的任何位置：

```weave
HID.Usage(0x000C, 0x00E9):down => tap(F6);
Windows.ScanCode(0x45, E1):down =>;
Linux.Key(30) := Keyboard.B;
```

`HID.Usage` 表达标准控制编号；带操作系统名称的原始编号只表达对应平台的控制。一个合法控制是否能在当前平台上被接收、查询或模拟，由当前平台对该控制的支持情况决定。
