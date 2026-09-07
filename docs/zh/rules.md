# 输入映射与规则

[文档首页](README.md) · [上一篇：语言基础](language.md) · [下一篇：动作与流程控制](actions.md)

重映射一个键使用 `->`；在某个输入发生时执行一段动作，使用带 `:down` 等事件后缀的规则。

## 把一个键换成另一个键

```weave
TARGET = "notepad.exe";

A -> B;
```

按下 A 时按下 B，按住 A 时把后续重复按下交给 B，松开 A 时松开 B。A 自己的输入由映射接管。

映射可以附加条件。下面的程序在左 Ctrl 按住时把 A 映射为 C，其余时候映射为 B：

```weave
A -> C when LCtrl == held;
A -> B;
```

条件在来源键首次按下时判断。选中的映射保持到来源键松开；中途松开 Ctrl，目标也仍然是当初选中的 C。多个映射从上往下检查，使用第一个满足条件的映射。

## 在输入发生时执行动作

```weave
F6:down => tap(B);
```

这个规则在首次按下 F6 时按一下 B。键盘事件后缀决定规则在哪个时刻触发：

| 后缀 | 时机 | 常见用途 |
| --- | --- | --- |
| `down` | 来源从松开变为按下 | 开始一个宏、切换变量 |
| `again` | 已按住时又收到一次按下报告 | 响应键盘自动重复 |
| `up` | 来源从按下变为松开 | 松开时执行动作 |

判断条件时，当前输入的物理状态已经更新：`down` 时该键为 `held`，`up` 时为 `idle`。程序生成的模拟输入可以在 Debug 中看到，但规则匹配使用物理输入。

## 箭头决定什么

“接管”表示这次物理输入不再交给目标应用；“放行”表示应用仍然收到它。“继续检查”表示还要检查同一输入的后续规则。

| 箭头 | 对物理输入的处理 | 匹配后是否继续检查 |
| --- | --- | --- |
| `=>` | 接管 | 停止 |
| `=>>` | 接管 | 继续 |
| `~>` | 放行 | 停止 |
| `~>>` | 放行 | 继续 |

例如，`A:down ~> tap(B);` 会保留原来的 A 输入，并额外按一下 B；`A:down => tap(B);` 接管这次 A 按下，只执行 B 的动作。

条件不满足时继续向下检查。一旦某条匹配规则决定接管，这次输入就会被接管，后续的放行型规则仍可以执行动作。

```weave
number count = 0;

F6:down ~>> set(count, count + 1);
F6:down => tap(B);
```

按一次 F6 会触发计数和按键两个任务。因为第二条规则要求接管，目标应用不会收到这次 F6 按下。

空动作也有用途。例如 `F6:down =>;` 只接管首次按下。需要屏蔽完整生命周期时，分别写 `down`、`again` 和 `up`：

```weave
F6:down =>;
F6:again =>;
F6:up =>;
```

## 组合键条件

组合键由一个触发事件和其他键的物理状态组成。例如，按住任意侧 Ctrl 再按 F6：

```weave
F6:down when LCtrl == held or RCtrl == held => tap(B);
```

`when` 只读取修饰键状态。这个例子中，物理 Ctrl 仍然处于按住状态，因此目标应用可能把输出 B 解释为 Ctrl+B。设计组合键时，要同时考虑触发条件和目标应用接收到的修饰键状态。

## 同一输入的判断顺序

一次物理输入按下面的层次处理：先判断退出规则，再判断目标和排除条件、暂停规则与普通运行开关，最后处理映射和普通事件规则。

来源键首次按下时，映射与普通规则按源码顺序检查。已经建立的映射会先处理自己的 `again` 和 `up`，后续普通规则可以添加动作。

同一次输入的规则条件读取相同的变量和数组状态。动作在条件匹配之后执行，所以前一条规则里的 `set` 不会改变这次输入后续规则的判断结果：

```weave
number count = 0;

F6:down ~>> set(count, count + 1);
F6:down when count >= 3 => tap(B);
```

从 `count = 0` 开始，第四次按 F6 才会满足第二条规则：这次输入开始时，前三次已经把 `count` 加到了 3。要根据刚修改的值立即作决定，把修改和 `if` 写在同一条规则的动作中。[动作与流程控制](actions.md)

## 暂停与恢复

```weave
pause Pause:down => toggle;

A -> B;
```

按 Pause 键在启用和暂停之间切换。内置状态 `PAUSE` 初始为 `on`，表示普通映射和规则启用；`off` 表示暂停。

暂停规则用 `pause` 开头，接按键事件和可选的 `when` 条件，再用 `=>` 或 `~>` 选择接管或放行，最后写 `on`、`off` 或 `toggle`。第一条匹配的暂停规则立即生效，这次输入不再触发后续规则和映射。

`PAUSE` 改变时，正在执行或等待的动作会取消，活动映射会清理，程序按住的输出会释放。恢复后由新输入触发新的工作，用户变量和数组保留。[运行行为与限制](running.md)

## 退出执行器

默认退出组合键为 `Ctrl+Shift+F12`，Ctrl 和 Shift 各自可以使用任意一侧。要设置自己的退出方式，在顶层写 `exit`：

```weave
exit F12:down when LCtrl == held and LShift == held;
```

显式退出规则组成这个程序自己的退出快捷键列表，替换默认组合键。可以写多条，第一条匹配的规则会接管输入并停止执行器。退出规则优先于目标资格和普通规则，暂停时也可以使用。

## 按键名称速查

名称区分大小写。简短键盘名和加上 `Keyboard.` 的写法等价，例如 `A` 与 `Keyboard.A`。

| 类别 | 名称 |
| --- | --- |
| 字母 | `A` 到 `Z` |
| 功能键 | `F1` 到 `F24` |
| 主键盘数字 | `Digit0` 到 `Digit9` |
| 小键盘数字 | `Numpad0` 到 `Numpad9` |
| 编辑和导航 | `Esc`、`Enter`、`Space`、`Tab`、`Backspace`、`Delete`、`Insert`、`Home`、`End`、`PageUp`、`PageDown` |
| 方向键 | `ArrowLeft`、`ArrowRight`、`ArrowUp`、`ArrowDown` |
| 修饰键 | `LCtrl`、`RCtrl`、`LShift`、`RShift`、`LAlt`、`RAlt` |
| 状态键 | `Pause`、`CapsLock`、`NumLock`、`ScrollLock` |
| 小键盘运算 | `NumpadAdd`、`NumpadSubtract`、`NumpadMultiply`、`NumpadDivide`、`NumpadDecimal` |
| 鼠标按钮 | `Mouse.Left`、`Mouse.Right`、`Mouse.Middle`、`Mouse.X1`、`Mouse.X2` |
| 多媒体 | `Consumer.PlayPause`、`Consumer.ScanNextTrack`、`Consumer.ScanPreviousTrack`、`Consumer.Stop`、`Consumer.Mute`、`Consumer.VolumeUp`、`Consumer.VolumeDown` |

鼠标按钮支持完整映射、`down` 和 `up` 事件，以及 `held`、`idle` 状态判断。例如 `Mouse.X1 -> LCtrl;`，或者 `Mouse.Left:down when LShift == held => tap(B);`。鼠标移动和滚轮使用[专门的事件](mouse.md)。

### 按原始编码选键

原始控制使用带命名空间的编码。`HID.Usage(page, usage)` 使用 USB HID 的 Usage Page 和 Usage ID，其中 page 为 `1` 到 `0xFFFF`，usage 为 `0` 到 `0xFFFF`。例如，下面的来源和命名控制 `A` 使用同一个控制身份：

```weave
HID.Usage(0x07, 0x04) -> B;
```

编码可以使用十进制或小写 `0x` 开头的十六进制。编码在允许范围内时，还需确认运行平台支持它在规则中的用途：接收输入、判断 `held` 或 `idle`，或输出按键。Windows 的虚拟键、扫描码及能力对照见 [Windows 执行器](windows.md#控制编码与能力)。选择编码前可以先在 [Debug](debugging.md) 中观察实际输入。
