# UniversalKeyRemapper 语法设计草案 v0

## 文档状态

本文档是 `.krm` 语言的讨论草案，用于在实现前确定词法、语法、运行语义和编译模型。语法尚未进入兼容性承诺阶段，后续讨论可以直接修改本文件。

## 设计目标

- 语言直接描述“输入事件、状态条件、持续映射和动作程序”，不发展成无边界的通用脚本语言。
- 状态、事件和动作使用不同的外观，使用户能够直接辨认一个语法元素属于哪一类。
- 常用映射保持简短，复杂条件、分支和循环仍然具有明确且可编译的语义。
- 源文件必须完整通过词法、语法、类型和规则冲突检查，程序才可以安装输入钩子。
- 文本只在启动阶段解析和编译；钩子回调只查询编译后的只读规则数据，不解析文本、不执行文件访问、不动态分配内存。
- 自身注入的输入永远不能成为规则的新触发源。
- 目标进程缺失时必须报错，不允许因为遗漏配置而意外启用全局映射。

## 基本示例

```krm
TARGET = "game.exe";
TAP_DURATION = 30ms;
ACTION_GAP = 10ms;

state combat = off;
duration fireGap = 80ms;
number recoil = 1.5;

CapsLock := Esc;
Mouse.Middle := F10;

Pause:down => toggle(PAUSE);
F1:down => toggle(combat);

A := B when combat[on];
A := C when combat[off];

C:down when LCtrl[held] and LShift[idle] =>
    tap(Numpad8),
    wait(100ms),
    tap(A);

Mouse.Left:down => while Mouse.Left[held] do
    tap(Mouse.Left)
end;
```

## 词法规则

### 文件编码

`.krm` 文件的字符集为 ASCII。关键字、标识符、字符串和注释都只能包含 ASCII 字符；超出 ASCII 的字节属于词法错误。

### 空白

空格、制表符和换行只用于分隔词法元素，不结束语句。语句可以自由跨行。

### 语句结束

所有完整指令都以分号 `;` 结束，换行与指令结束无关。`if`、`repeat` 和 `while` 使用 `end` 结束自身结构，但它们在语法上是一个动作项，包含它们的外层事件规则仍以分号结束；三种结构都可以完全写在同一物理行内。

### 注释

```krm
// line comment

/*
block comment
*/
```

多行注释暂不允许嵌套。

### 标识符

关键字和标识符区分大小写。内置配置量使用全大写名称，关键字和动作名使用小写名称，按键名使用规范的首字母大写形式。字符串内容保持原样。

用户标识符由 ASCII 字母、数字和下划线组成，但首字符不能是数字。用户标识符不能与关键字、按键名、内置配置量或内置状态重名。

### 数值和单位

```krm
0
10
-2
1.5
100ms
1s
1.5s
1min
```

无单位整数或小数属于 `number`。带 `ms`、`s` 或 `min` 的值属于 `duration`。编译器把时间统一转换成内部时间单位，并检查范围和溢出。

### 字符串字面量

字符串字面量使用双引号：

```krm
TARGET = "C:\\Games\\Example\\game.exe";
```

外部命令也使用字符串字面量：

```krm
F8:down => exec("heatSymbolDisplay.exe --compact");
```

语言不提供用户可声明的 `string` 变量类型。字符串字面量只允许出现在明确接受字符串的内蕴配置量和动作参数中，例如 `TARGET` 与 `exec`。`exec` 是否通过命令解释器执行以及引号、转义和参数规则需要在外部命令功能实现前单独固定。

## 三类基础语法元素

### 状态

状态统一使用方括号：

```krm
LCtrl[held]
LShift[idle]
combat[on]
combat[off]
```

物理按键和鼠标按钮支持 `[held]` 与 `[idle]`。`state` 类型变量支持 `[on]` 与 `[off]`。错误的状态和值组合属于编译错误，例如 `A[on]` 或 `combat[held]`。

状态只是查询，不会产生输入或修改变量。

### 事件

事件统一使用冒号：

```krm
F6:down
F6:up
F6:repeat
Mouse.Middle:down
Mouse.Middle:up
```

`:down` 表示第一次物理按下边缘，`:up` 表示物理松开边缘，`:repeat` 表示按键处于按住状态时产生的重复事件。自身注入和第三方注入事件默认都不参与规则触发。

每条普通事件规则恰好有一个触发事件。其他限制写入 `when` 后面的条件表达式，不再使用 `+` 连接事件和状态。

### 动作

产生效果的操作统一使用函数调用外观：

```krm
press(A)
release(A)
tap(Space)
wait(100ms)
toggle(combat)
set(recoil, recoil + 0.1)
exec("program.exe")
```

动作调用中的括号表示参数边界。它不同于旧设计中为每个按键强制添加的按键元组括号。

## 内蕴配置量

内蕴配置量由运行时和编译器预先定义，不需要用户声明。用户可以在配置中赋值一次，重复赋值属于编译错误。

### `TARGET`

```krm
TARGET = "game.exe";
TARGET = "C:\\Games\\Example\\game.exe";
TARGET = GLOBAL;
```

`TARGET` 指定唯一目标程序。字符串可以是可执行文件名或绝对路径。`GLOBAL` 明确请求全局规则。命令行显式传入的目标可以覆盖文件中的 `TARGET`；如果命令行和文件都没有目标，程序拒绝运行。


### `TAP_DURATION`

```krm
TAP_DURATION = 30ms;
```

`TAP_DURATION` 是 `duration` 类型的内蕴配置量。`tap(A)` 按顺序执行 A 按下、等待 `TAP_DURATION`、A 松开。具体默认值在兼容性测试后固定，但必须大于零。

### `ACTION_GAP`

```krm
ACTION_GAP = 10ms;
```

`ACTION_GAP` 是 `duration` 类型的内蕴配置量。动作列表中每一个序列逗号都精确插入一次 `ACTION_GAP`。

```krm
tap(A), tap(B);
```

等价时间线是：执行完整 `tap(A)`、等待一次 `ACTION_GAP`、执行完整 `tap(B)`。

```krm
tap(A), wait(100ms), tap(B);
```

这条序列包含两处逗号，因此时间线是：执行 `tap(A)`、等待 `ACTION_GAP`、执行 `wait(100ms)`、等待 `ACTION_GAP`、执行 `tap(B)`。显式 `wait` 不替换逗号的内蕴间隔。

函数参数之间的逗号不是动作序列逗号，不插入 `ACTION_GAP`：

```krm
set(value, value + 1)
```

### `PAUSE`

```krm
Pause:down => toggle(PAUSE);
```

`PAUSE` 是运行时预先声明的内蕴 `state` 类型量，初始值为 `on`。它不是按键，也不绑定固定的物理控制；配置通过普通事件规则定义自己的停止按键或组合条件。

`PAUSE` 可以使用普通状态量的状态查询和修改动作：

```krm
PAUSE[on]
PAUSE[off]
toggle(PAUSE)
set(PAUSE, on)
set(PAUSE, off)
```

`PAUSE[on]` 时运行时正常查询并执行映射表；`PAUSE[off]` 时不查询映射表，所有物理输入直接放行。修改 `PAUSE` 的规则就是普通规则，可以使用 `when`、动作序列、`wait`、`if` 和循环，并采用与其他普通规则完全相同的检查和顺序执行语义。

`PAUSE` 每次发生状态切换时，运行时立即取消全部在途和排队中的映射任务，包括等待、循环和尚未完成的连续模拟输入，并释放程序拥有的全部合成按键和鼠标按钮。触发本次切换的动作程序也属于被取消的任务，切换动作之后尚未执行的动作不再继续。

## 持续映射

### 基础形式

```krm
A := B;
Mouse.Middle := F10;
F9 := Mouse.Middle;
```

`:=` 表示完整生命周期赋值：源控制的按下、重复和松开被映射成目标控制的对应操作。源事件在映射动作成功接受后被吞掉。

`A := B` 的基础语义是：A 第一次按下时发送 B 按下；A 重复时按照重复策略维持或重复 B；A 松开时发送 B 松开。程序必须记录自己持有的 B，并在目标失效、暂停、取消或退出时保证释放。

### 条件映射

```krm
A := B when combat[on];
A := C when combat[off];
```

映射条件在源控制第一次按下时求值并锁存。即使条件在源控制按住期间发生变化，松开时也必须释放当初实际按下的目标控制，不能重新选择目标。

如果动作队列、目标检查或输出状态检查失败，源输入必须原样放行，不能只吞掉源而不产生目标。

## 事件规则

### 基础形式

```krm
F6:down => tap(F7);
```

### 带条件形式

```krm
C:down when LCtrl[held] and LShift[idle] => tap(Numpad8);
```

语义顺序固定为：接收一个物理事件、根据编译后的状态表达式检查 `when`、接受动作程序、最后决定是否吞掉源事件。条件表达式中不能再出现事件或动作。

### 消费规则与观察规则

`=>` 表示消费型事件规则。对于 `:down`，动作成功接受后，触发键的按下、重复和对应松开组成一个完整捕获对并被吞掉；如果动作未接受则全部放行。

松开事件不能在对应按下已经交给目标程序后单独吞掉，否则目标可能永远看不到松开并形成卡键。因此消费型 `:up =>` 规则属于编译错误。

需要观察松开事件并附加动作时，使用观察箭头 `~>`：

```krm
A:up ~> tap(B);
```

`~>` 永远放行源事件，只附加执行动作，不建立源捕获。`:down` 也可以使用观察规则：

```krm
A:down ~> tap(B);
```

这样区分以后，`:=` 表示完整映射，`=>` 表示消费触发，`~>` 表示观察触发。

## 动作序列

### 基础动作

```krm
press(A)
release(A)
tap(A)
wait(100ms)
toggle(combat)
set(value, expression)
exec("raw command")
```

`press` 发送按下并把控制加入程序拥有的合成状态。`release` 发送松开并移除对应所有权。`tap` 是具有完整按下和松开配对的不可拆分语义动作。`wait` 必须可由暂停、目标失效、紧急停止和程序退出中断。

对一个并非由程序持有的控制执行 `release` 默认属于运行时拒绝动作，避免合成松开破坏用户正在物理按住的控制。编译器可以对明显不配对的静态序列给出错误或警告。

### 序列逗号

```krm
press(LCtrl), tap(C), release(LCtrl);
```

动作按照文件顺序执行，每个序列逗号都插入一次 `ACTION_GAP`。动作不是并行执行；Windows 输入也按照确定顺序进入输入流。

### 控制结构作为动作项

一个完整的 `if`、`repeat` 或 `while` 结构在语法上等同于一个动作项，可以出现在动作序列的任意位置，并使用序列逗号与其他动作项连接：

```krm
F1:down => tap(A), if combat[on] then tap(B) else tap(C) end, tap(D);
F2:down => repeat count times do tap(A) end, tap(B);
F3:down => while F3[held] do tap(A) end, tap(B);
```

控制结构前后用于连接动作项的逗号各自插入一次 `ACTION_GAP`，控制结构内部动作列表的逗号也遵循相同规则。函数实参之间的逗号只分隔参数，例如 `set(count, 1)`，不产生 `ACTION_GAP`。控制结构不要求换行；换行、缩进和单行写法具有完全相同的语法与语义。

## 变量与类型

### 状态变量

```krm
state combat = off;
state autoFire = on;
```

`state` 只有 `on` 和 `off` 两个值。状态通过 `combat[on]`、`combat[off]` 查询，通过 `toggle(combat)` 或 `set(combat, on)` 修改。`PAUSE` 是由运行时预先声明的特殊 `state` 量。

### 数值变量

```krm
number recoil = 1.5;
number count = 0;
```

`number` 的底层表示固定为 C++ `double`。仅允许有限值；源代码、表达式和动作结果不得产生 NaN、正无穷或负无穷。要求整数的其他语境可以规定各自的显式转换规则；固定循环次数采用本文件“固定次数”一节定义的下限限制和向下取整规则。

### 时间变量

```krm
duration fireGap = 80ms;
duration longGap = 1.5s;
```

`duration` 保存非负时间，支持 `ms`、`s` 和 `min` 单位。任何运算产生的负时间都饱和为 `0ms`，不会产生负的 `duration`。`wait`、`TAP_DURATION` 和 `ACTION_GAP` 都要求 `duration`。

### 名称和重复声明

变量必须先声明后使用。任何用户变量、内蕴配置量或按键名的重复声明和重名都属于编译错误，不采用覆盖语义。

变量初始值必须能在编译时完成类型检查。是否允许使用其他变量初始化由后续常量初始化规则决定；基础版本建议只接受字面量。

### 变量修改时序

`set` 和 `toggle` 是动作，按照它们在动作程序中的位置执行。如果它们位于 `wait` 之后，变量只在等待完成后改变。变量修改必须是原子的，之后到达的输入事件只能观察修改前或修改后的完整值。

## 表达式

### 直接表达式

表达式直接属于 `.krm`：

```krm
C:down when count >= 3 and combat[on] => set(count, count + 1);
F2:down => set(fireGap, fireGap + 10ms);
```

### 推荐运算符

```text
+  -  *  /  %
<  <=  >  >=
==  !=
and  or  not
(  )
```

`/` 表示除法，不使用反斜杠。暂不使用 `^`，避免幂与异或含义冲突。暂不提供 `?:` 三元表达式，条件分支由 `if/else` 表达。

### 类型规则

- `number` 可以进行普通算术和数值比较。
- `duration + duration` 产生 `duration`；`duration - duration` 产生的结果以 `0ms` 为下限饱和。
- `duration * number`、`number * duration` 和 `duration / number` 产生 `duration`。
- 两个 `duration` 之间不允许执行乘法或除法，`duration / duration` 和 `duration * duration` 都是类型错误。
- `state` 不能参加算术，只能使用 `[on]`、`[off]` 或相等性判断。
- `and`、`or`、`not` 只接受布尔条件。
- 不允许隐式地把 `number` 当作 `duration`，也不允许把 `state` 当作数字。

### 表达式编译模型

源代码中的表达式由独立的表达式解析器模块实现，而不需要在表面语法中嵌入另一种文本格式。主解析器在 `when`、`set`、`if`、`repeat` 和 `while` 的表达式位置调用表达式解析器，得到有类型的内部表示。运行时使用已经完成解析和类型检查的数据，不重新解析源文本。

## 条件分支

### 语法

```krm
F1:down => if combat[on] then
    tap(A),
    tap(B)
else
    tap(C)
end;
```

`else` 可以省略：

```krm
F1:down => if count > 0 then
    set(count, count - 1),
    tap(A)
end;
```

`if`、`then`、`else`、`end` 是控制结构，不是产生输入的动作，因此不要求写成动作函数。分支内部仍然是逗号分隔的动作列表，每个逗号照常插入 `ACTION_GAP`。

### 求值时机

`when` 在输入事件触发规则时求值。`if` 在动作程序实际执行到该位置时求值。因此位于 `wait` 之后的 `if` 可以观察等待期间发生的状态或变量变化。

如果 `if` 位于动作程序开头且条件不可能在执行前变化，编译器可以把它展开成隐藏规则；如果它位于等待之后，编译器应生成动作程序中的条件跳转。两种实现必须保持相同的语言语义。

## 循环

循环只提供结构化、可取消的重复，不提供无条件无限循环、递归、`break`、`continue` 或用户自定义跳转。

### 固定次数

```krm
number count = 5.8;

F2:down => repeat count times do
    tap(A),
    tap(B)
end;
```

`times` 前的表达式必须产生有限的 `number`。执行到 `repeat` 时只求值一次：先把负值按 `0` 处理，再向下取整，数学上等价于 `floor(max(0, value))`，因此上例执行 `5` 次；这里的 `floor` 和 `max` 只用于说明语义，不声明同名语言函数。本次循环开始后，原变量的后续变化不改变已经确定的次数。实际次数还受到实现规定的安全上限约束。两次迭代之间自动插入一次 `ACTION_GAP`，等价于把每次迭代的动作列表用一个序列逗号连接。

### 条件循环

```krm
Mouse.Middle:down => while Mouse.Middle[held] do
    tap(Mouse.Left)
end;
```

`while` 后的表达式必须产生布尔条件。每次迭代开始前重新检查条件；一次迭代完成后，在下一次条件检查前自动等待一次 `ACTION_GAP`。循环体内部的序列逗号仍各自产生一次 `ACTION_GAP`。

条件循环直接使用 `while`，不添加 `repeat` 或独立的迭代间隔语法。同一条触发规则同时最多运行一个循环实例；该实例未结束时再次触发，默认忽略新触发。

### 取消语义

目标失去有效性、目标失去前台、程序暂停、紧急停止、程序退出或循环条件变假时，循环进入取消流程。等待必须立即可中断；正在执行的不可拆分 `tap` 应完成其配对松开，已经由 `press` 持有的控制必须在取消清理中释放。

循环条件变假时不再开始新迭代。如果条件在当前原子动作中间变假，完成该动作的安全配对后取消剩余动作。

### 编译模型

固定次数循环可以编译成计数器、条件跳转和隐藏计数槽。条件循环可以编译成条件检查、动作程序、可中断等待和回跳。隐藏变量由编译器生成，不能与用户变量重名，也不会暴露给源程序。

## 规则唯一性与冲突

### 重复规则

对控制名称、事件类型、箭头类型和规范化 `when` 表达式完全相同的规则进行重复声明属于编译错误，即使动作结果相同也不允许重复。

```krm
A:down when combat[on] => tap(B);
A:down when combat[on] => tap(C); // compile error
```

持续映射采用相同原则：

```krm
A := B when combat[on];
A := C when combat[on]; // compile error
```

### 条件重叠

同一触发事件或映射源可以具有不同且互斥的条件：

```krm
A := B when combat[on];
A := C when combat[off];
```

编译器应当拒绝能够证明会同时成立的映射条件，并对无法证明互斥的复杂条件给出诊断。运行时如果仍发现同一物理输入匹配多个消费型规则或多个持续映射，必须安全放行原输入并报告歧义，不能依赖文件顺序静默覆盖。

如果需要在同一触发条件下选择不同结果，应写成一条带 `if/else` 的规则，而不是重复声明相同触发条件。

## 按键和鼠标名称

### 字母、功能键和常用控制

```text
A ... Z
F1 ... F24
Esc
Enter
Space
Tab
Backspace
Delete
Insert
Home
End
PageUp
PageDown
ArrowLeft
ArrowRight
ArrowUp
ArrowDown
LCtrl
RCtrl
LShift
RShift
LAlt
RAlt
Pause
CapsLock
NumLock
ScrollLock
```

### 数字键

```text
Digit0 ... Digit9
Numpad0 ... Numpad9
NumpadAdd
NumpadSubtract
NumpadMultiply
NumpadDivide
NumpadDecimal
```

不用裸数字表示按键，避免与 `number` 字面量混淆。`Numpad8` 比 `Num8` 更明确。

### 鼠标

```text
Mouse.Left
Mouse.Right
Mouse.Middle
Mouse.X1
Mouse.X2
```

鼠标移动和滚轮需要额外参数和连续值语义，保留给对应章节后续设计，不在基础按钮名称中混入临时形式。

### 特殊按键

Caps Lock 和 Windows 键只有在 Windows 平台层能够稳定观察、抑制和注入时才进入可用表，并应记录其系统副作用。`Fn` 通常不作为独立的 Windows 虚拟键出现；如果平台层无法识别，编译器必须报告不支持，而不是接受后静默失效。

不定义 `Ctrl`、`Shift` 或 `Alt` 为左右按键的语法糖。需要匹配左右任一侧时显式写条件：

```krm
C:down when LCtrl[held] or RCtrl[held] => tap(A);
```

当条件同时包含其他逻辑时使用括号明确优先级：

```krm
C:down when (LCtrl[held] or RCtrl[held]) and combat[on] => tap(A);
```

## 运算优先级

从高到低暂定为：

```text
括号
一元 +  -  not
*  /  %
+  -
<  <=  >  >=
==  !=
and
or
```

不建议依赖复杂优先级表达意图；混合 `and` 与 `or` 时应使用括号。

## 仍需继续讨论的项目

- `TAP_DURATION` 和 `ACTION_GAP` 的最终默认值与允许范围。
- 映射源的重复事件如何传递给目标，尤其是不同程序对自动重复的处理差异。
- 多条复杂条件规则的静态互斥证明做到什么程度，以及哪些情况只做运行时歧义保护。
- 同一时刻由不同触发器启动的多个宏是全局串行、按规则并发还是按输出控制分组调度。
- `exec` 的安全边界、参数传递、工作目录、返回值和取消行为。
- 鼠标移动、滚轮、坐标、速度和屏幕单位的类型与语法。
- 是否增加命名动作程序，以及命名动作被多条规则调用时的实例和重入策略。
- 是否由运行时占用一个不经过映射表的物理组合键作为强制停止，以及选择哪个组合键。
