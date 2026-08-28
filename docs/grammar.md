# Weave 正式语法说明

## 文档状态

本文档是当前 Weave 编译器唯一的正式语言定义，规定编译器接受的源码语法、名称绑定、类型系统、规则匹配、动作流和可观察运行语义。`.weave` 源文件必须先编译为持久化的 `.weavec` 程序，执行器不会在运行期间重新解析源码。

## 完整示例

```weave
TARGET = "game.exe";
TAP_DURATION = 30ms;
ACTION_GAP = 10ms;

state combat = off;
number count = 0;
duration fireGap = 80ms;

A -> B when combat[on];

A:down when count < 5 ~> tap(B) | set(count, count + 1);
A:down when count >= 5 ~> tap(C) | set(count, count - 1);

F1:down =>
    if combat[on] then
        repeat 3 do tap(Mouse.Left) | end
    else
        toggle(combat)
    end;

pause Pause:down ~> toggle;
exit F12:down when LCtrl[held] and LShift[held];
```

## 词法规则

### 源文本

源文件必须是有效的 UTF-8。语言记号、标识符、控制名和字符串内容只接受 ASCII；有效的非 ASCII 文本只能出现在注释中。字符串内外都不接受嵌入的 NUL。

空格、制表符、回车和换行用于分隔记号，但不会结束语句。每个完整的顶层项目都以分号 `;` 结束。

行注释以 `//` 开始，在下一个换行之前或文件末尾结束。块注释以 `/*` 开始、以 `*/` 结束，可以跨行但不能嵌套。

### 名称和保留字

所有名称区分大小写。标识符以 ASCII 字母开头，后续字符可以是 ASCII 字母、十进制数字或 `_`。用户变量不能使用语言关键字、内蕴名称、扫描码限定符或无前缀的命名控制名。

保留字和内蕴名称是 `TARGET`、`TAP_DURATION`、`ACTION_GAP`、`PAUSE`、`GLOBAL`、`state`、`number`、`duration`、`exit`、`pause`、`when`、`on`、`off`、`toggle`、`down`、`repeat`、`up`、`and`、`or`、`not`、`press`、`release`、`tap`、`wait`、`gap`、`set`、`exec`、`if`、`then`、`else`、`end`、`do`、`while`、`held`、`idle`、`E0` 和 `E1`。

### 字面量

数字字面量是十进制整数或十进制小数，例如 `0`、`12` 和 `3.5`。不接受科学计数法、省略整数部分的小数或省略小数部分的小数。负数通过一元 `-` 表示；`number` 声明也允许把这个符号直接写在初值前。

时间字面量由非负数字字面量紧接 `ms`、`s` 或 `min` 组成，例如 `30ms`、`1.5s` 和 `2min`。结果必须能够精确表示为非负的 64 位有符号整数纳秒。

十六进制整数只允许作为原始控制构造器的参数，使用小写 `0x` 前缀，后面至少有一个十六进制数字。

字符串字面量使用双引号。完整的转义集合是 `\\`、`\"`、`\n`、`\r` 和 `\t`。字符串中不能出现物理换行或非 ASCII 源字符。

## 形式语法

下列语法使用 ISO 风格 EBNF：逗号表示连接，`|` 表示备选，方括号表示可选序列，花括号表示重复零次或多次。除词法产生式另有规定外，记号之间可以出现空白和注释。

```ebnf
compilation-unit = { top-level-item } ;

top-level-item =
      target-setting
    | tap-duration-setting
    | action-gap-setting
    | state-declaration
    | number-declaration
    | duration-declaration
    | mapping
    | exit-rule
    | pause-rule
    | event-rule
    ;

target-setting       = "TARGET", "=", target-selector, ";" ;
target-selector      = string-literal | "GLOBAL" ;
tap-duration-setting = "TAP_DURATION", "=", duration-literal, ";" ;
action-gap-setting   = "ACTION_GAP", "=", duration-literal, ";" ;

state-declaration    = "state", identifier, "=", state-literal, ";" ;
number-declaration   = "number", identifier, "=", signed-number-literal, ";" ;
duration-declaration = "duration", identifier, "=", duration-literal, ";" ;

mapping = control-reference, "->", control-reference, [ condition ], ";" ;

exit-rule = "exit", event, [ condition ], ";" ;

pause-rule   = "pause", event, [ condition ], pause-arrow, pause-effect, ";" ;
pause-arrow  = "=>" | "~>" ;
pause-effect = "on" | "off" | "toggle" ;

event-rule       = event, [ condition ], rule-arrow, action-flow, ";" ;
event            = control-reference, ":", event-transition ;
event-transition = "down" | "repeat" | "up" ;
condition        = "when", expression ;
rule-arrow       = "=>" | "=>>" | "~>" | "~>>" ;

action-flow = { action-item } ;

action-item =
      input-action
    | wait-action
    | gap-action
    | set-action
    | toggle-action
    | exec-action
    | gap-marker
    | if-action
    | repeat-action
    | while-action
    ;

input-action      = input-action-name, "(", control-reference, ")" ;
input-action-name = "press" | "release" | "tap" ;
wait-action       = "wait", "(", expression, ")" ;
gap-action        = "gap", "(", ")" ;
set-action        = "set", "(", value-reference, ",", expression, ")" ;
toggle-action     = "toggle", "(", value-reference, ")" ;
exec-action       = "exec", "(", string-literal, ")" ;
gap-marker        = "|" ;

if-action =
    "if", expression, "then", action-flow,
    [ "else", action-flow ],
    "end" ;

repeat-action = "repeat", expression, "do", action-flow, "end" ;
while-action  = "while", expression, "do", action-flow, "end" ;

expression                = or-expression ;
or-expression             = and-expression, { "or", and-expression } ;
and-expression            = equality-expression, { "and", equality-expression } ;
equality-expression       = relational-expression, { equality-operator, relational-expression } ;
relational-expression     = additive-expression, { relational-operator, additive-expression } ;
additive-expression       = multiplicative-expression, { additive-operator, multiplicative-expression } ;
multiplicative-expression = unary-expression, { multiplicative-operator, unary-expression } ;
unary-expression          = primary-expression | unary-operator, unary-expression ;

equality-operator       = "==" | "!=" ;
relational-operator     = "<" | "<=" | ">" | ">=" ;
additive-operator       = "+" | "-" ;
multiplicative-operator = "*" | "/" | "%" ;
unary-operator          = "+" | "-" | "not" ;

primary-expression =
      state-literal
    | number-literal
    | duration-literal
    | value-reference
    | control-state-query
    | value-state-query
    | "(", expression, ")"
    ;

control-state-query = control-reference, "[", physical-state-test, "]" ;
value-state-query   = value-reference, "[", logical-state-test, "]" ;
physical-state-test = "held" | "idle" ;
logical-state-test  = "on" | "off" ;

value-reference = identifier | builtin-value ;
builtin-value   = "TAP_DURATION" | "ACTION_GAP" | "PAUSE" ;
state-literal   = "on" | "off" ;

control-reference = raw-control | named-control ;
named-control      = control-segment, { ".", control-segment } ;
control-segment    = ASCII-letter, { ASCII-letter | digit | "_" } ;

raw-control =
      hid-usage
    | windows-virtual-key
    | windows-scan-code
    | linux-key
    | macos-key-code
    ;

hid-usage           = "HID.Usage", "(", unsigned-code, ",", unsigned-code, ")" ;
windows-virtual-key = "Windows.VirtualKey", "(", unsigned-code, ")" ;
windows-scan-code   = "Windows.ScanCode", "(", unsigned-code, [ ",", scan-prefix ], ")" ;
linux-key           = "Linux.Key", "(", unsigned-code, ")" ;
macos-key-code      = "MacOS.KeyCode", "(", unsigned-code, ")" ;
scan-prefix         = "E0" | "E1" ;
unsigned-code       = decimal-integer | hexadecimal-integer ;
hexadecimal-integer = "0x", hex-digit, { hex-digit } ;

signed-number-literal = [ "-" ], number-literal ;
number-literal        = decimal-integer, [ ".", decimal-integer ] ;
duration-literal      = number-literal, duration-unit ;
duration-unit         = "ms" | "s" | "min" ;

identifier      = ASCII-letter, { ASCII-letter | digit | "_" } ;
decimal-integer = digit, { digit } ;
digit           = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
hex-digit       = digit | "A" | "B" | "C" | "D" | "E" | "F" | "a" | "b" | "c" | "d" | "e" | "f" ;
ASCII-letter    = ASCII-upper | ASCII-lower ;
ASCII-upper     = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" | "J" | "K" | "L" | "M" | "N" | "O" | "P" | "Q" | "R" | "S" | "T" | "U" | "V" | "W" | "X" | "Y" | "Z" ;
ASCII-lower     = "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" | "i" | "j" | "k" | "l" | "m" | "n" | "o" | "p" | "q" | "r" | "s" | "t" | "u" | "v" | "w" | "x" | "y" | "z" ;
string-literal  = ? 遵循上文转义规则的 ASCII 字符串记号 ? ;
```

## 配置量和变量

每个内蕴配置量最多赋值一次。

| 配置量 | 接受的值 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `TARGET` | 非空字符串或 `GLOBAL` | 未指定 | 按可执行文件名或绝对路径选择目标，或者选择全局分派；命令行目标可以覆盖它。 |
| `TAP_DURATION` | `0ms..1min` 的时间字面量 | `30ms` | `tap` 使用的保持时间。 |
| `ACTION_GAP` | `0ms..1min` 的时间字面量 | `10ms` | `|` 和 `gap()` 使用的等待时间。 |

编译时不强制要求 `TARGET`，但执行时必须存在编译目标或命令行目标覆盖。

用户可以声明 `state`、`number` 和 `duration` 三种变量。声明必须使用同类型字面量初始化，而且必须位于第一次使用之前。

| 类型 | 表示 | 初值示例 |
| --- | --- | --- |
| `state` | `on` 或 `off` | `state combat = off;` |
| `number` | 有限 binary64 数值 | `number count = -2.5;` |
| `duration` | 非负 64 位有符号整数纳秒 | `duration delay = 80ms;` |

`PAUSE` 是初始值为 `on` 的内蕴 `state`。`TAP_DURATION` 和 `ACTION_GAP` 是内蕴 `duration`。内蕴值可以读取，但不能作为 `set` 或 `toggle` 的目标。

## 表达式和类型

运算符从高到低的优先级是括号，一元 `+`、一元 `-` 和 `not`，`*`、`/` 和 `%`，二元 `+` 和 `-`，`<`、`<=`、`>` 和 `>=`，`==` 和 `!=`，`and`，最后是 `or`。二元运算符从左向右结合。

| 运算 | 操作数类型 | 结果 |
| --- | --- | --- |
| 一元 `+`、`-` | `number` | `number` |
| `not` | 布尔值 | 布尔值 |
| `+`、`-`、`*`、`/`、`%` | `number`、`number` | `number` |
| `+`、`-` | `duration`、`duration` | `duration` |
| `*` | `duration`、`number` 或 `number`、`duration` | `duration` |
| `/` | `duration`、`number` | `duration` |
| `<`、`<=`、`>`、`>=` | `number`、`number` | 布尔值 |
| `==`、`!=` | 两个类型相同的 `state`、`number` 或 `duration` 值 | 布尔值 |
| `and`、`or` | 布尔值、布尔值 | 布尔值 |

布尔值是表达式结果类型，不是用户可以声明的变量类型。`control[held]`、`control[idle]`、`stateValue[on]` 和 `stateValue[off]` 都产生布尔值。

`and` 和 `or` 从左到右短路求值。只有左操作数是编译期常量并能证明右操作数不可到达时，右侧的编译期常量故障才会被抑制。

数字运算结果必须保持有限，除数或模数不能为零。时间减法以 `0ms` 为下限；时间缩放把小数纳秒向零截断，并把负结果限制为 `0ms`；溢出属于错误。

## 控制名称

控制名称区分大小写。每个简短键盘名也接受对应的 `Keyboard.` 前缀，例如 `A` 和 `Keyboard.A` 表示同一个控制。

命名键盘控制目录如下：

```text
A ... Z
F1 ... F24
Esc Enter Space Tab Backspace Delete Insert Home End PageUp PageDown
ArrowLeft ArrowRight ArrowUp ArrowDown
LCtrl RCtrl LShift RShift LAlt RAlt
Pause CapsLock NumLock ScrollLock
Digit0 ... Digit9
Numpad0 ... Numpad9
NumpadAdd NumpadSubtract NumpadMultiply NumpadDivide NumpadDecimal
```

命名鼠标控制是 `Mouse.Left`、`Mouse.Right`、`Mouse.Middle`、`Mouse.X1` 和 `Mouse.X2`。

命名消费类控制是 `Consumer.PlayPause`、`Consumer.ScanNextTrack`、`Consumer.ScanPreviousTrack`、`Consumer.Stop`、`Consumer.Mute`、`Consumer.VolumeUp` 和 `Consumer.VolumeDown`。

平台专有命名控制是 `Windows.Keyboard.IMEOn`、`Linux.Keyboard.Compose` 和 `MacOS.Keyboard.Fn`。

原始控制使用下列固定存储范围：

| 构造器 | 接受范围 |
| --- | --- |
| `HID.Usage(page, usage)` | `page` 为 `1..0xFFFF`，`usage` 为 `0..0xFFFF`。 |
| `Windows.VirtualKey(code)` | `code` 为 `0..0xFF`。 |
| `Windows.ScanCode(code)` | `code` 为 `0..0xFF`。 |
| `Windows.ScanCode(code, E0)` | `code` 为 `0..0xFF`，限定符为 `E0`。 |
| `Windows.ScanCode(code, E1)` | `code` 为 `0..0xFF`，限定符为 `E1`。 |
| `Linux.Key(code)` | `code` 为 `0..0x2FF`。 |
| `MacOS.KeyCode(code)` | `code` 为 `0..0xFFFF`。 |

编译器只验证控制身份和数值范围，不保证所选运行时后端能够观察、查询或注入该控制。后端支持情况在编译程序激活时检查。

## 事件和状态查询

事件转换是 `down`、`repeat` 或 `up`。`down` 表示第一次物理按下边缘，`repeat` 表示保持按下期间的重复按下，`up` 表示物理释放边缘。

运行时先更新物理状态，再计算当前事件的条件。因此当前控制在 `down` 条件中已经是 `held`，在 `up` 条件中已经是 `idle`。

注入输入不进入用户规则匹配，也不更新物理状态。可执行文件目标不满足资格时，物理输入直接放行而不分派用户规则；`GLOBAL` 取消可执行文件目标限制。

## 完整映射

`source -> target;` 映射完整的按下、重复和释放生命周期。可选的 `when` 条件只在来源控制的 `down` 边缘求值。第一个匹配的映射会锁存到释放，即使条件随后发生变化也不会改换目标。

映射按照源码顺序检查，在来源 `down` 上等价于消费并在匹配后停止的规则。活跃映射的重复和释放处理早于同一事件的普通事件规则；普通规则可以附加动作，但不能撤销映射生命周期或消费决定。

## 事件规则

事件规则包含可选的布尔 `when` 条件、箭头和动作流。动作流可以为空。

| 箭头 | 物理输入 | 匹配后的扫描 |
| --- | --- | --- |
| `=>` | 消费 | 停止 |
| `=>>` | 消费 | 继续 |
| `~>` | 放行 | 停止 |
| `~>>` | 放行 | 继续 |

条件为假时总是继续扫描。多个继续型规则匹配时，任务按照源码顺序创建。只要任意匹配规则要求消费，最终决定就是消费；后续观察型规则不能撤销消费。

同一个物理事件的全部条件读取相同的事件开始状态。动作在匹配完成后才进入异步任务，因此前一条匹配规则创建的 `set` 或 `toggle` 不会影响同一事件的后续规则条件。

多条规则可以使用相同事件和相同条件。哪些规则实际匹配只由源码顺序和箭头决定；编译器不会静态选择唯一规则。

## PAUSE 规则

PAUSE 规则的形式是 `pause event [when condition] arrow effect;`。箭头只能是表示消费的 `=>` 或表示放行的 `~>`，效果只能是 `on`、`off` 或 `toggle`。这里不接受继续型箭头和普通动作。

PAUSE 规则按照源码顺序在普通映射和事件规则之前求值。第一条匹配规则同步应用效果，并停止当前事件的后续分派。没有 PAUSE 规则匹配时，只有 `PAUSE` 为 `on` 才继续普通分派。

`PAUSE` 发生变化时，运行时取消已有任务、清除活跃映射并释放持有的输出。重复设置当前值仍然执行该规则的放行或消费决定，但不会产生新的取消转换。

## 退出规则

退出规则的形式是 `exit event [when condition];`，没有箭头和动作流。退出规则按照源码顺序在目标资格、PAUSE 规则、映射和普通事件规则之前求值。第一条匹配规则消费事件并请求有序停止。

源码没有显式退出规则时，编译器自动加入下列规则：

```weave
exit F12:down when (LCtrl[held] or RCtrl[held]) and (LShift[held] or RShift[held]);
```

## 动作

相邻动作项按照源码顺序执行，中间没有隐含等待。

| 动作 | 约束和效果 |
| --- | --- |
| `press(control)` | 取得输出所有权；所有权从零变为一时发送按下。 |
| `release(control)` | 释放当前任务持有的所有权；释放未持有的控制会使任务失败。 |
| `tap(control)` | 取得所有权，等待 `TAP_DURATION`，再释放所有权。 |
| `wait(expression)` | 要求 `duration` 表达式并进行可取消等待。 |
| `gap()` | 按照 `ACTION_GAP` 进行可取消等待。 |
| `|` | 与 `gap()` 完全等价；允许出现在开头、结尾或连续出现。 |
| `set(value, expression)` | 要求可写用户变量，表达式类型必须与目标相同。 |
| `toggle(value)` | 要求可写的用户 `state`。 |
| `exec("command")` | 要求非空命令并请求平台启动进程；运行时必须显式授权。 |

不同任务和映射共享输出所有权。重叠持有不会重复发送按下，只有最后一个所有者释放时才发送最终释放。任务正常结束、取消或失败时都会释放它仍然持有的全部输出。

`exec` 保留作者写入的非空命令字节。平台在不隐式使用命令解释器的情况下解析并启动可执行文件。成功启动后不等待子进程退出，也不继续持有子进程所有权。

## 动作控制流

`if condition then ... [else ...] end` 要求布尔条件，并在任务执行到该动作时求值。

`repeat limit do ... end` 要求有限的 `number`。上限只在进入循环时求值一次；整数索引从零开始，每次迭代在 `index < limit` 时运行。因此上限 `5.8` 会运行六次，零和负数运行零次。

`while condition do ... end` 要求布尔条件，并在每次迭代开始前重新求值。

`if`、`repeat` 和 `while` 都是动作项，可以嵌套，也可以放在普通动作之间。循环回跳会把执行权交还调度器，但不代表经过确定时间。只有 `wait`、`tap`、`gap()` 和 `|` 引入规定的等待。

## 任务和取消

每条匹配且动作流非空的事件规则创建一个独立任务。任务共享不可变的编译后动作程序，但各自持有指令位置、循环状态、等待、取消代次和输出所有权。

同一事件产生的任务按照源码顺序提交，并可在等待和循环让步处交错运行。目标失效、PAUSE 转换、致命停止、退出规则或应用关闭会取消相应任务并释放其输出。被取消的任务不会因为目标或 PAUSE 后来恢复资格而继续运行。

## 编译和验证

`InputWeaverCompiler.exe validate source.weave` 执行源码加载、词法分析、语法分析、名称绑定、类型检查、降低检查和编译程序验证，但不会写出 `.weavec`。诊断会在适用时给出源码路径、行、列、源码区间和关联位置。

`InputWeaverCompiler.exe compile source.weave output.weavec` 执行相同检查，并且只在没有错误时发布编译产物。
