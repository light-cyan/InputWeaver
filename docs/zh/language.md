<a id="section-language-basics"></a>

# 语言基础

[English](../en/language.md)

[文档首页](README.md) · [下一篇：输入映射与规则](rules.md)

Weave 程序通常由目标设置、变量声明和输入规则组成。设置决定程序在哪里运行，变量保存状态，规则说明发生某个输入时要做什么。

```weave
TARGET = "notepad.exe";

state enabled = on;
number count = 0;

F6:down => toggle(enabled);
A:down when enabled == on => tap(B) set(count, count + 1);
```

这个程序用 F6 切换 `enabled`。它为 `on` 时，每次按下 A 都会按一下 B，并把 `count` 加一。`state` 和 `number` 是变量类型，`when` 后面是触发条件，箭头后面是动作。

后面的功能小例子可以放进已设置 `TARGET` 的程序中，也可以在界面的 Target 中指定应用后单独运行。每次修改后，先停止再运行程序，观察新源码的效果。

<a id="section-source-format"></a>

## 源文件的写法

源码使用 UTF-8 编码。关键字、名称、字符串和标点使用 ASCII 字符；注释可以使用中文。名称区分大小写，例如 `count` 和 `Count` 是两个名称，内置的 `TARGET` 要按大写书写。

变量名以英文字母开头，后面可以接字母、数字和下划线，例如 `enabled`、`step2`、`fire_delay`。变量、数组和计量器先声明再使用，名称应避开关键字、内置值和按键名称。

每个顶层设置、声明、映射或规则以 `;` 结束。换行和缩进用于排版。一个规则包含多个动作时，在整条规则末尾写一个分号。

```weave
// A short comment.
TARGET = "notepad.exe";

/* A comment can span
   more than one line. */
F6:down =>
    tap(H)
    wait(100ms)
    tap(I);
```

<a id="section-reserved-words-and-names"></a>

### 保留字和名称速查

变量、数组和计量器共用名称空间，声明名按大小写区分并保持唯一。下面的单词和固定拼写由语言使用；声明用户名称时避开它们以及[命名控制](rules.md#section-key-name-reference)。

| 类别 | 保留字或固定拼写 |
| --- | --- |
| 声明 | `state`、`number`、`duration`、`meter` |
| 配置与内置值 | `TARGET`、`TAP_DURATION`、`ACTION_GAP`、`MOUSE_IDLE_TIMEOUT`、`RAND_SEED`、`RAND01`、`PAUSE`、`Mouse` |
| 常量 | `GLOBAL`、`on`、`off`、`held`、`idle` |
| 规则与计量周期 | `exit`、`pause`、`when`、`every` |
| 事件后缀 | `down`、`again`、`up`、`move`、`wheel`、`horizontalwheel`、`tick` |
| 流程控制 | `if`、`then`、`else`、`end`、`repeat`、`do`、`while` |
| 逻辑运算 | `and`、`or`、`not` |
| 按键与等待动作 | `press`、`release`、`tap`、`wait`、`gap` |
| 变量与数组动作 | `set`、`toggle`、`append`、`pop`、`clear` |
| 进程、鼠标与计量器动作 | `exec`、`move_by`、`move_to`、`scroll`、`scroll_horizontal`、`restart` |
| 原始控制 | `HID.Usage`、`Windows.VirtualKey`、`Windows.ScanCode`、`Linux.Key`、`MacOS.KeyCode` |
| 扫描码前缀 | `E0`、`E1` |

限定控制名由命名空间和成员组成，例如 `Keyboard.A`。选择按键表示时，还需确认运行平台支持它的用途；Windows 支持的输入和输出见 [Windows 执行器](windows.md)。

<a id="section-properties-and-fields"></a>

### 属性与字段速查

属性和字段通过点号读取，例如 `values.length`、`Mouse.x`、`path.progress`；计量器的已完成周期使用 `@path.dx` 这样的写法。

| 所属对象 | 属性或字段名 |
| --- | --- |
| 数组 | `length` |
| `Mouse` 当前状态 | `x`、`y`、`dx`、`dy`、`wheel_x`、`wheel_y`、`moving`、`idle_time` |
| 计量器（按类型和周期视图选择） | `x`、`y`、`dx`、`dy`、`wheel_x`、`wheel_y`、`moving`、`start_x`、`start_y`、`distance`、`period`、`progress`、`remaining`、`valid` |

属性和字段名按所属对象识别，也可用于用户声明，例如 `number length = 0;`、`number dx = 0;`。`values.length` 读取数组长度，单独的 `length` 读取同名用户变量；名为 `length` 的数组通过 `length.length` 读取长度。字段的类型和可用范围见[当前鼠标状态](mouse.md#section-reading-current-mouse-state)与[计量器字段](mouse.md#section-field-reference)。

<a id="section-configure-a-program"></a>

## 配置程序

配置写在程序顶层，每项最多设置一次。

| 配置 | 默认值 | 写法与作用 |
| --- | --- | --- |
| `TARGET` | 运行时需要指定目标 | `"notepad.exe"` 选择应用，`GLOBAL` 选择全局；界面和命令行可以覆盖 |
| `TAP_DURATION` | `30ms` | 一次 `tap` 的按住时间，取值范围为 `0ms` 到 `1min` |
| `ACTION_GAP` | `10ms` | `\|` 和 `gap()` 的等待时间，取值范围为 `0ms` 到 `1min` |
| `MOUSE_IDLE_TIMEOUT` | `80ms` | 多久没有物理移动后认为鼠标停止移动，设置为正的时间值 |
| `RAND_SEED` | `0` | 随机数种子，使用 `0` 到 `18446744073709551615` 之间的十进制整数 |

```weave
TARGET = "notepad.exe";
TAP_DURATION = 40ms;
ACTION_GAP = 100ms;

F6:down => tap(H) | tap(I);
```

这个例子中，H 和 I 分别按住 40 毫秒，H 松开后再等待 100 毫秒，才开始按 I。

`TARGET` 也可以写成可执行文件的绝对路径，例如 `TARGET = "C:\\Tools\\Editor.exe";`。目标选择和窗口切换见[运行行为与限制](running.md)。

<a id="section-three-variable-types"></a>

## 三种变量

| 类型 | 保存什么 | 声明示例 |
| --- | --- | --- |
| `state` | `on` 或 `off` | `state enabled = on;` |
| `number` | 整数或小数 | `number distance = 24.5;` |
| `duration` | 非负时间 | `duration delay = 80ms;` |

声明时填写对应类型的直接值，例如 `number count = 0;`。运行期间通过 `set` 修改变量，通过 `toggle` 切换 `state`。变量由整个运行中的程序共享。

```weave
number count = 0;
duration delay = 100ms;
state enabled = off;

F1:down => set(count, count + 1);
F2:down => set(delay, delay + 10ms);
F3:down => toggle(enabled);
```

数字使用十进制写法，例如 `12`、`0.5`、`-2.5`。`number` 使用双精度浮点数，数值必须有限。

时间由数字和单位紧接组成：`ms` 是毫秒，`s` 是秒，`min` 是分钟，例如 `30ms`、`1.5s`、`2min`。时间最细可表示到纳秒。

<a id="section-comparisons-and-conditions"></a>

## 比较与条件

`when` 和 `if` 后面需要一个判断结果。用比较运算把变量或按键状态变成条件：

| 表达式 | 含义 |
| --- | --- |
| `enabled == on` | 开关变量为开 |
| `count >= 3` | 数字至少为 3 |
| `LCtrl == held` | 左 Ctrl 处于物理按住状态 |
| `Mouse.Left == idle` | 鼠标左键处于物理松开状态 |
| `count != 0` | 数字不为零 |

`on`、`off` 表示 `state` 的值，`held`、`idle` 表示按键状态。比较和逻辑运算产生布尔条件，供 `when`、`if` 和 `while` 使用。`state` 变量通过 `enabled == on` 这样的比较参与条件；声明标量变量使用 `state`、`number` 或 `duration` 三种类型。

用 `and` 表示同时满足，`or` 表示至少满足一个，`not` 表示取反。括号可以明确分组：

```weave
state enabled = on;

F6:down when enabled == on and (LCtrl == held or RCtrl == held) => tap(B);
```

`and` 和 `or` 从左向右判断，并在结果已经确定时跳过右侧。这可以用来保护数组索引或计量器的已完成数据。

<a id="section-number-and-time-operations"></a>

## 数字和时间运算

数字支持 `+`、`-`、`*`、`/`、`%`，其中 `%` 求余数。数字大小使用 `<`、`<=`、`>`、`>=` 比较；`state`、`number`、`duration` 和按键状态各自支持同类型值的相等比较。

时间支持时间相加、时间相减、乘以数字，以及除以数字。例如 `delay + 20ms`、`delay * 2`、`delay / 2` 都得到时间。

```weave
duration delay = 100ms;
number count = 0;

F6:down =>
    wait(delay / 2)
    tap(B)
    set(count, (count + 1) % 10);
```

时间相减的结果最低为 `0ms`；时间缩放的负结果也按 `0ms` 处理，小数纳秒向零截断。除以零、求余的除数为零、数值变成无穷大或时间溢出都属于表达式错误。

<a id="section-type-and-operator-reference"></a>

### 类型与运算组合速查

表达式按下面的类型组合使用。数组下标读取的结果是对应元素的 `number` 或 `state`；数组的 `.length` 是 `number`。

| 运算 | 操作数类型 | 结果类型 | 示例 |
| --- | --- | --- | --- |
| 一元 `+`、`-` | `number` | `number` | `-count` |
| `+`、`-`、`*`、`/`、`%` | 两个 `number` | `number` | `count % 10` |
| `+`、`-` | 两个 `duration` | `duration` | `delay + 20ms` |
| `*` | `duration` 与 `number`，顺序任意 | `duration` | `2 * delay` |
| `/` | 左侧 `duration`，右侧 `number` | `duration` | `delay / 2` |
| `<`、`<=`、`>`、`>=` | 两个 `number` | 布尔条件 | `count >= 3` |
| `==`、`!=` | 两个 `state` | 布尔条件 | `enabled == on` |
| `==`、`!=` | 两个 `number` | 布尔条件 | `count != 0` |
| `==`、`!=` | 两个 `duration` | 布尔条件 | `delay == 100ms` |
| `==`、`!=` | 两个按键状态 | 布尔条件 | `LCtrl == held` |
| `and`、`or` | 两个布尔条件 | 布尔条件 | `count > 0 and enabled == on` |
| `not` | 一个布尔条件 | 布尔条件 | `not (count == 0)` |

赋值使用与目标相同类型的表达式：例如 `set(delay, 100ms)` 设置时间，`set(count, 100)` 设置数字。时间的相等比较使用 `==`、`!=`；需要按大小设置阈值时，用 `number` 变量保存采用同一单位的数值。

运算顺序从高到低为：括号；一元 `+`、`-`、`not`；`*`、`/`、`%`；`+`、`-`；大小比较；相等比较；`and`；`or`。同一优先级的二元运算从左向右计算。较长的条件建议主动加括号。

<a id="section-arrays"></a>

## 数组

数组保存一组同类型的值，元素类型为 `number` 或 `state`。用方括号初始化，用从零开始的索引读取或修改元素，用 `.length` 读取当前长度。

```weave
number[] values = [2, 4, 8];
state[] gates = [on, off];
number index = 0;

F1:down when index >= 0 and index < values.length =>
    set(values[index], values[index] + 1);
F2:down => toggle(gates[0]);
```

`values[0]` 是第一项。索引是非负数字，小数向下取整，例如 `values[1.9]` 读取第二项；取整后的索引必须小于当前长度。

共享数组或索引可能被其他任务修改时，应在动作内部再次检查范围，并让检查与访问连续执行。`when` 检查的是匹配时的状态，排队期间其他任务仍可能改变它们。

数组可以增长和缩短：

```weave
number[] values = [];
number last = 0;

F1:down => append(values, 10);
F2:down =>
    if values.length > 0 then
        pop(values, last)
    end;
F3:down => clear(values);
```

`append` 在末尾追加一项；`pop` 取走末项并写入同类型的标量变量；`clear` 清空数组。示例在动作内部检查长度，并紧接着执行 `pop`，避免其他任务在检查与操作之间清空数组。

数组的长度随内容变化。写入一个已有元素使用 `set(values[index], value)`，追加新元素使用 `append`。[数组动作的具体行为](actions.md)

<a id="section-readable-builtin-values"></a>

## 可以读取的内置值

| 名称 | 类型 | 用途 |
| --- | --- | --- |
| `TAP_DURATION` | `duration` | 读取配置的按住时间 |
| `ACTION_GAP` | `duration` | 读取配置的动作间隔 |
| `MOUSE_IDLE_TIMEOUT` | `duration` | 读取鼠标空闲判定时间 |
| `RAND01` | `number` | 每次读取取得一个大于等于 0、小于 1 的随机数 |
| `PAUSE` | `state` | 读取普通规则的运行开关；`on` 为启用，`off` 为暂停，初始值为 `on` |

这些值供表达式读取。用户变量通过 `set` 和 `toggle` 修改；`PAUSE` 通过专门的[暂停规则](rules.md)修改。

```weave
RAND_SEED = 42;
number sample = 0;

F6:down => set(sample, RAND01) wait(50ms + 100ms * sample) tap(B);
```

每次读取 `RAND01` 都会推进随机序列。需要在几个地方复用同一个随机值时，先像上面这样保存到变量。重新启动程序会从种子的序列起点开始；暂停和取消动作保留随机序列的位置。多个动作并行取样时，实际执行顺序会影响各自取得的值。

<a id="section-strings"></a>

## 字符串

目标路径和 `exec` 命令使用双引号字符串。可用转义为 `\\`、`\"`、`\n`、`\r`、`\t`；路径中的一个反斜杠写成 `\\`。

```weave
TARGET = "C:\\Tools\\Editor.exe";

F6:down => exec("\"C:\\Tools\\Helper.exe\" --mode quick");
```

`exec` 的运行权限和参数处理见[动作与流程控制](actions.md)。接下来阅读[输入映射与规则](rules.md)，把变量和条件用到真实输入中。
