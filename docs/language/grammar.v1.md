# Weave 语法说明 v1

## 文档定位

本文档定义 Weave v1 的词法、类型、规则匹配、动作流和运行语义。`.weave` 源码在启动阶段完成解析、类型检查和编译；输入处理阶段只使用编译后的只读规则数据。

`development/phase-2/CompiledProgramDesign.md` 定义 Weave v1 编译后只读程序的完整内部表示和验证不变量。

## 设计原则

- 语言直接描述输入事件、状态条件、完整映射和动作任务。
- 状态使用方括号，事件使用冒号，动作使用函数调用外观。
- 规则按源码顺序匹配，箭头同时表达原始事件是否消费以及匹配是否继续。
- 连续动作按书写顺序执行，动作之间可以使用空白提高可读性；`|` 和 `gap()` 都表示等待一次 `ACTION_GAP` 指定的时间。
- `if`、`repeat` 和 `while` 是动作项，可以出现在动作流的任意位置。
- 自身注入和第三方注入的输入默认都不触发用户规则。
- `:=` 提供由运行时负责配对的完整生命周期映射；逐事件规则只处理明确声明的单个事件。

## 示例

```weave
TARGET = "game.exe";
TAP_DURATION = 30ms;
ACTION_GAP = 10ms;

state combat = off;
number burstCount = 3.5;
duration fireGap = 80ms;

CapsLock := Esc;
A := B when combat[on];
A := C when combat[off];

F1:down ~> toggle(combat);

F2:down when (LCtrl[held] or RCtrl[held]) and combat[on] =>
    press(LShift) tap(A) | tap(B) release(LShift);

F3:down =>
    repeat burstCount do
        tap(A) | tap(B)
    end;

Mouse.Middle:down ~>
    while Mouse.Middle[held] do
        tap(Mouse.Left) |
    end;

pause Pause:down ~> off;
```

## 词法规则

### 字符集

`.weave` 源文件使用 UTF-8 编码。关键字、标识符、字符串字面量和所有语法标点只接受 ASCII；注释可以包含有效的非 ASCII UTF-8 文本。

### 空白

空格、制表符和换行只分隔词法元素，不结束指令。只要相邻字符仍能被词法分析器正确分成不同记号，动作流可以自由换行或写在同一行。

### 指令结束

所有顶层完整指令都以分号 `;` 结束。`if`、`repeat` 和 `while` 以 `end` 结束自身结构，但它们只是外层动作流中的一个动作项，不单独使用分号。

```weave
F1:down => if combat[on] then tap(A) else tap(B) end;
F2:down => repeat burstCount do tap(A) | end tap(B);
F3:down => while F3[held] do tap(A) | end;
```

### 注释

```weave
// 中文行注释

/*
多行注释
*/
```

`//` 注释延伸到下一个换行符之前；换行符结束注释并继续作为普通空白处理，文件末尾也可以结束行注释。`/* ... */` 注释可以跨越换行，但不能嵌套。

### 标识符和关键字

关键字和标识符区分大小写。用户标识符由 ASCII 字母、数字和下划线组成，首字符不能是数字。用户标识符不能与关键字、按键名、内蕴配置量或内蕴状态重名。

内蕴配置量使用全大写名称，关键字和动作名使用小写名称，按键名使用规范名称。

### 数值和时间

```weave
0
10
-2
1.5
100ms
1s
1.5s
1min
```

无单位整数或小数属于 `number`。带 `ms`、`s` 或 `min` 的值属于 `duration`。`number` 的底层表示是有限的 C++ `double`；NaN 和正负无穷不属于合法运行值。`duration` 使用非负 64 位纳秒表示；不能精确表示为整数纳秒或超出表示范围的字面量属于编译错误。

### 字符串

字符串字面量使用双引号：

```weave
TARGET = "C:\\Games\\Example\\game.exe";
F8:down ~> exec("tool.exe --profile compact");
```

语言没有用户可声明的字符串变量。字符串字面量只出现在明确接受字符串的内蕴配置量和动作参数中。

String literals are cooked ASCII byte strings. The complete escape set is `\\`, `\"`, `\n`, `\r`, and `\t`; each escape decodes to one backslash, double quote, line feed, carriage return, or horizontal tab. An unknown escape, a non-ASCII source character, an embedded NUL, or a line break before the closing quote is a compile error.

The decoded value of a `TARGET` executable selector or an `exec` command must contain at least one byte. The compiler does not trim a non-empty decoded value; platform-specific target and executable resolution owns any further usability checks.

## 状态、事件和动作

### 状态查询

```weave
LCtrl[held]
LShift[idle]
combat[on]
combat[off]
PAUSE[on]
PAUSE[off]
```

键盘键和鼠标按钮支持 `[held]` 与 `[idle]`。`state` 类型值支持 `[on]` 与 `[off]`。状态查询没有副作用。

运行时先把当前物理事件反映到自己的物理状态，再求值该事件的条件。因此当前控制的 `:down` 条件可以观察到 `[held]`，当前控制的 `:up` 条件可以观察到 `[idle]`。

### 事件

```weave
F6:down
F6:up
F6:repeat
Mouse.Middle:down
Mouse.Middle:up
```

`:down` 是第一次物理按下边缘，`:repeat` 是保持按下期间产生的重复事件，`:up` 是物理松开边缘。每条事件规则恰好有一个触发事件，额外限制写在 `when` 条件中。

逐事件规则只消费或观察它明确声明的单个事件。消费 `:down` 不会自动消费对应的 `:repeat` 和 `:up`；消费 `:up` 也是合法规则。

### 动作

```weave
press(A)
release(A)
tap(Space)
wait(100ms)
toggle(combat)
set(count, count + 1)
exec("tool.exe")
gap()
```

动作调用中的括号只表示参数边界。动作是否产生输入、修改状态或暂停任务由动作类型决定。

## 内蕴配置量和状态

### `TARGET`

以下三种写法任选一种：

```weave
TARGET = "game.exe";
TARGET = "C:\\Games\\Example\\game.exe";
TARGET = GLOBAL;
```

`TARGET` 指定唯一目标程序。字符串可以是可执行文件名或绝对路径，`GLOBAL` 明确请求全局规则。命令行显式目标可以覆盖文件中的 `TARGET`；如果两处都没有目标，程序拒绝启动映射。

An executable `TARGET` string must be non-empty after escape decoding. Its non-empty decoded bytes are preserved without compiler-host path classification and are resolved by the selected platform during activation.

### `TAP_DURATION`

```weave
TAP_DURATION = 30ms;
```

`TAP_DURATION` 是点击保持时间，类型为 `duration`。没有显式赋值时默认是 `30ms`，有效范围是闭区间 `0ms` 到 `1min`。`tap(A)` 依次执行 A 按下、等待 `TAP_DURATION` 指定的时间、A 松开。取消发生在等待期间时，运行时立即完成必要的配对松开。

### `ACTION_GAP`

```weave
ACTION_GAP = 10ms;
```

`ACTION_GAP` 是通用动作间隔时间，类型为 `duration`。没有显式赋值时默认是 `10ms`，有效范围是闭区间 `0ms` 到 `1min`。执行到 `|` 或 `gap()` 时，任务读取 `ACTION_GAP` 并等待它指定的时间。没有写出这两种间隔动作时，运行时立即开始下一个动作，不等待 `ACTION_GAP` 指定的时间；控制结构边界和循环迭代也遵守相同规则。`1min` 上限只约束 `TAP_DURATION` 和 `ACTION_GAP` 这两个通用配置，显式 `wait(duration)` 可以使用更长的合法 `duration`。

### `PAUSE`

`PAUSE` 是初始值为 `on` 的内蕴 `state`。`PAUSE[on]` 时可以执行普通完整映射和事件规则；`PAUSE[off]` 时绕过普通映射与规则并放行物理输入。

```weave
pause Pause:down ~> off;
pause F12:down when LCtrl[held] and LShift[held] => toggle;
pause F11:down => on;
```

PAUSE 控制使用专用顶层语句，而不是普通动作流：

```ebnf
pause-rule   = "pause", event, [ "when", boolean-expression ], pause-arrow, pause-effect, ";" ;
pause-arrow  = "=>" | "~>" ;
pause-effect = "on" | "off" | "toggle" ;
```

`=>` 消费物理事件，`~>` 放行物理事件；两种形式都会停止处理该事件，本语法不接受继续型箭头。PAUSE 语句不能包含普通动作、间隔、等待或控制结构。`set(PAUSE, ...)` 和 `toggle(PAUSE)` 都是非法普通动作；布尔条件仍可读取 `PAUSE[on]` 或 `PAUSE[off]`，这种读取不会形成 PAUSE 控制语句。

编译器把 PAUSE 语句存入独立于映射和普通规则的 PAUSE 控制索引。运行时只对物理候选输入查询该索引；查询发生在物理状态更新、强制停止识别以及适用的目标和指针路由检查之后，但在读取当前 `PAUSE` 值之前。第一条匹配的 PAUSE 语句同步应用效果，不创建任务，然后按照箭头消费或放行事件；没有 PAUSE 语句匹配时，只有 `PAUSE[on]` 才继续普通分派。

效果真正改变 `PAUSE` 值时，运行时递增取消代际、拒绝过时代际发布、唤醒调度器、丢弃旧代际的就绪和定时任务、清除活跃映射并释放程序输出所有权。已经为 `on` 时再次应用 `on`，或已经为 `off` 时再次应用 `off`，仍然按照箭头决定事件是否消费，但不会产生新的取消转换。

### 强制停止

运行时保留一个不经过目标检查、`PAUSE` 或用户规则的物理强制停止组合。默认组合是任意一侧 `Ctrl`、任意一侧 `Shift` 与 `F12`；它只接受非注入的物理候选输入。组合完成后，运行时停止接受任务、取消全部任务、释放程序输出、清除活跃映射、卸载输入钩子并退出。该组合属于主程序设置而不是 Weave 语法，主程序可以提供其他组合配置。

## 完整映射

### 基础映射

```weave
A := B;
Mouse.Middle := F10;
F9 := Mouse.Middle;
```

`:=` 把源控制的按下、重复和松开生命周期映射到目标控制。运行时负责记录活跃映射、维持输出所有权并保证目标松开。

### 条件映射

```weave
A := B when combat[on];
A := C when combat[off];
```

映射条件只在源控制第一次按下时求值。匹配的映射和目标控制随后被锁存在内部状态中；条件在源控制按住期间发生变化，不改变本次活跃映射。

映射声明在源 `:down` 的源码顺序规则列表中表现为消费并停止匹配的规则。多个映射条件重叠时，首先满足条件的声明生效。

活跃映射的源 `:repeat` 和 `:up` 先由映射生命周期处理，再扫描同一事件的普通用户规则。用户规则可以观察或附加动作，但不能阻止运行时重复或释放已锁存的目标。活跃映射已经决定该物理事件被消费。

目标失效、`PAUSE` 切换、致命停止或程序退出时，运行时释放已经实际按下的映射目标并清除内部状态。

## 事件规则与匹配

### 基本形式

```weave
F1:down => tap(A);
F2:up ~> tap(B);
F3:repeat =>;
```

事件规则可以没有动作。空的消费规则只消费对应事件，空的观察规则只控制匹配流程。

### 条件

```weave
C:down when (LCtrl[held] or RCtrl[held]) and combat[on] => tap(Numpad8);
```

`when` 在物理事件到达时求值，只接受没有副作用的布尔表达式。条件中不能出现事件或动作。

### 四种箭头

| 箭头 | 原始物理事件 | 条件匹配后的规则扫描 |
|---|---|---|
| `=>` | 消费 | 停止 |
| `=>>` | 消费 | 继续 |
| `~>` | 放行 | 停止 |
| `~>>` | 放行 | 继续 |

条件不成立时总是继续扫描。匹配停止只影响后续用户规则，不撤销已经匹配的任务，也不截断活跃 `:=` 映射的内部生命周期处理。

如果一次事件匹配了多个继续型规则，任务按源码顺序创建。只要其中至少有一条匹配的消费规则，最终物理事件就被消费；后续观察规则不能撤销消费决定。

```weave
A:down ~>> set(count, count + 1);
A:down when combat[on] => tap(B);
A:down ~> tap(C);
```

同一物理事件的全部 `when` 条件读取同一个事件开始状态。前面匹配规则创建的异步任务尚未执行，因此其 `set`、`toggle` 和其他动作不会改变本次事件后续规则的条件；这些变化只能影响以后到达的事件。

规则可以具有相同事件和相同条件。源码顺序、箭头的继续或停止语义共同决定哪些规则能够匹配，不执行静态歧义选择。

## 动作流

### 相邻动作

连续动作之间只需要能够被词法分析器识别，通常使用空白分开以保持可读性：

```weave
F1:down => press(LCtrl) tap(C) release(LCtrl);
```

这三个动作按源码顺序执行。动作之间没有 `|` 或 `gap()`，因此运行时不会等待 `ACTION_GAP` 指定的时间。

### 间隔动作

`|` 在动作流中是 `gap()` 的简写，两者都等待一次 `ACTION_GAP` 指定的时间。开头、结尾和连续 `|` 都合法：

```weave
F2:down => | tap(A) tap(B) ||| tap(C) |;
```

这段动作流依次等待一次 `ACTION_GAP` 指定的时间、执行 `tap(A)` 和 `tap(B)`、等待三次 `ACTION_GAP` 指定的时间、执行 `tap(C)`，最后再等待一次 `ACTION_GAP` 指定的时间。下面的写法与它完全等价：

```weave
F2:down => gap() tap(A) tap(B) gap() gap() gap() tap(C) gap();
```

逗号只用于分隔函数实参，动作流的间隔统一使用 `|` 或 `gap()`：

```weave
F3:down => set(count, count + 1) | tap(A);
```

### 空动作流

事件箭头之后可以直接写分号：

```weave
A:down =>;
A:up =>;
B:down ~>;
C:down =>>;
```

空动作流不创建任务，但箭头的消费和匹配流程语义仍然生效。

### 基础输入动作

`press(A)` 发送目标按下并登记程序对该输出控制的所有权。`release(A)` 释放程序拥有的目标控制；对程序没有持有的控制执行 `release` 是运行时动作失败。`tap(A)` 发送目标按下，等待 `TAP_DURATION` 指定的时间，再发送配对松开；取消时仍保证必要的松开。

多个任务或映射同时持有同一输出控制时，运行时按照所有权计数维持目标按下状态，最后一个所有者释放时才发送目标松开。

所有权合并同时定义本版的输出冲突策略：同一控制的全局所有权从零变为非零时才发送按下，从非零变为零时才发送松开。重叠的 `press` 或 `tap` 只增加各自所有权，不重复发送按下；一个已经被其他任务持有的控制上执行 `tap` 时，临时所有权会保持 `TAP_DURATION` 指定的时间，但不会打断现有持有状态来制造新的按下和松开边缘。这样不会提前释放其他任务的控制，但重叠点击可能在目标程序看来合并成一次保持。

任务正常结束、取消或失败时，运行时释放该任务仍持有的全部输出所有权。遗漏显式 `release` 不会让合成按下状态在任务结束后残留。

### 等待和任务切换

`|`、`gap()` 与 `wait(duration)` 都使当前任务进入可取消等待。相邻的非等待动作连续执行，任务遇到等待、`tap` 的持续阶段或循环回跳时允许其他任务运行。

循环回跳会把执行权交还调度器。这个调度让步不是等待动作，不会等待 `ACTION_GAP` 指定的时间，也不承诺经过任何可观察时长。

### 外部进程动作

The decoded command passed to `exec` must be non-empty. The compiler preserves every byte of a non-empty command, including whitespace, and leaves executable-token and path resolution to the platform launcher.

`exec(command)` asks the platform process launcher to resolve and start the command's executable without an implicit command interpreter. The launcher may inspect the executable token for resolution, but it preserves the authored command and arguments when invoking the platform process API. Native quoting and explicit shell invocation follow the selected platform's rules.

The launcher sets the child working directory to the directory containing the resolved executable. The final child working directory is therefore selected from the resolved executable path rather than from the InputWeaver executable directory, the `.weave` source directory, or the InputWeaver process working directory. Resolving a relative executable token can still use the InputWeaver process working directory as described below. Failure to resolve either the executable or its containing directory is a launch failure.

The child inherits the environment visible to InputWeaver at launch. A successful launch completes the `exec` action immediately, and the task continues without waiting for process exit or retaining child-process ownership. Cancellation before launch prevents process creation; cancellation, `PAUSE`, force stop, and application shutdown do not terminate a child that was already created. A launch failure records the platform error and ends the current task while other tasks continue.

#### Windows executable lookup and working directory

On Windows, executable lookup and the child working directory are separate decisions. Lookup first produces an absolute executable path; the directory containing that resolved path then becomes the child working directory. Relative paths used by the child are interpreted from that directory unless the child changes its own working directory.

A path-qualified executable token contains `\`, `/`, or `:`. An absolute token is used at its absolute location. A relative path-qualified token is resolved against the current working directory of the InputWeaver process. Path-qualified tokens must identify an existing file and do not receive an implicit extension.

A bare executable token keeps its authored extension. A bare token without an extension receives `.exe` unless it ends with `.`. InputWeaver searches for the resulting name in this order: the InputWeaver executable directory, the InputWeaver process working directory, the Windows system directory, the legacy Windows `System` directory, the Windows directory, and the directories listed by `PATH` in their authored order.

- `exec("C:\\Tools\\helper.exe config.json")` resolves the executable directly and starts it with `C:\\Tools` as its working directory.
- `exec(".\\tools\\helper.exe config.json")` resolves `.\\tools\\helper.exe` against the InputWeaver process working directory, then starts it in the directory containing the resolved `helper.exe`.
- `exec("helper.exe config.json")` uses the bare-name search. If it resolves to `D:\\SDK\\bin\\helper.exe`, the child working directory is `D:\\SDK\\bin` regardless of which search location supplied that path.

## 变量和类型

### `state`

```weave
state combat = off;
state autoFire = on;
```

`state` 只有 `on` 和 `off`。它通过方括号查询，通过 `set` 或 `toggle` 修改。

### `number`

```weave
number count = 3.5;
number recoil = 1.25;
```

`number` 使用有限的 C++ `double`。算术结果不能是 NaN 或正负无穷。

### `duration`

```weave
duration fireGap = 80ms;
duration timeout = 1min;
```

`duration` 是非负时间值。任何运算产生的负时间都饱和到 `0ms`。

### 声明和修改

用户变量必须先声明后使用。变量重复声明、与内蕴名称重名或使用同名不同类型都属于编译错误。内蕴配置量在同一配置中也只能赋值一次。

v1 的变量声明初值只接受与声明类型一致的字面量：`state` 使用 `on` 或 `off`，`number` 使用无单位数值，`duration` 使用带单位时间值。

`set` 和 `toggle` 按普通动作流顺序修改用户变量。变量更新是原子的，并对之后的事件和之后执行的控制表达式可见。`PAUSE` 只能通过专用顶层 PAUSE 语句修改。

`TAP_DURATION`、`ACTION_GAP` 和 `PAUSE` 都不能作为普通 `set` 或 `toggle` 的目标；用户变量仍是可写的普通运行值。

## 表达式

### 运算符

```text
+  -  *  /  %
<  <=  >  >=
==  !=
and  or  not
(  )
```

从高到低的优先级是括号、一元 `+ - not`、`* / %`、`+ -`、关系比较、相等比较、`and`、`or`。混合逻辑条件可以使用括号明确意图。

`and` 和 `or` 使用从左到右的短路求值；左操作数已经决定结果时不求值右操作数。

Compile-time constant-fault diagnostics follow the same reachability rule. A constant fault in the right operand is accepted only when a compile-time constant left operand proves that the right operand is unreachable (known false for `and` or known true for `or`); a right operand that is definitely or possibly evaluated must remain fault-free.

### 类型规则

- `number` 支持普通算术和数值比较。
- `duration + duration` 产生 `duration`。
- `duration - duration` 产生以 `0ms` 为下限的 `duration`。
- `duration * number`、`number * duration` 和 `duration / number` 产生 `duration`。
- 两个 `duration` 之间不能执行乘法或除法。
- `state` 不参与算术，通过 `[on]`、`[off]` 或相等性判断形成条件。
- `and`、`or` 和 `not` 只接受布尔条件。
- `number` 不隐式转换成 `duration`，`state` 不隐式转换成数字。

表达式直接出现在 `when`、`set`、`if`、`repeat` 和 `while` 中，不嵌入字符串，也不使用额外的计算动作。

## 条件分支

```weave
F1:down =>
    tap(A)
    if combat[on] then
        tap(B) | tap(C)
    else
        tap(D)
    end
    tap(E);
```

`else` 可以省略。`if` 在任务实际执行到该位置时求值，因此等待之后的 `if` 可以观察等待期间发生的变量和物理状态变化。

完整的 `if ... end` 是一个动作项，可以在它和前后动作之间使用空白，也可以使用 `|` 或 `gap()` 明确等待一次 `ACTION_GAP` 指定的时间。

## 固定次数循环

```weave
number count = 5.8;

F2:down =>
    repeat count do
        tap(A) | tap(B)
    end
    tap(C);
```

`repeat` 后的表达式必须产生有限 `number`。任务执行到循环入口时只求值一次，并把结果保存为任务局部 `limit`。任务局部整数 `index` 从 `0` 开始；每次迭代前只检查 `index < limit`，完成循环体后把 `index` 增加 `1`。

因此 `5.8` 执行六次，零和负数执行零次。循环体或其他任务对原变量的后续修改不改变已经保存的 `limit`。计数器无法继续增长时属于致命运行错误。

循环进入下一次迭代时不会自动等待。需要等待 `ACTION_GAP` 指定的时间时，在循环体中写 `|` 或 `gap()`：

```weave
F3:down => repeat count do tap(A) | end;
```

## 条件循环

```weave
Mouse.Middle:down ~>
    while Mouse.Middle[held] do
        if combat[on] then
            tap(Mouse.Left) |
        else
            tap(Space)
        end
    end;
```

`while` 后的表达式必须产生布尔条件，并在每次迭代开始前重新求值。条件为假时循环正常结束，任务继续执行循环之后的动作。

循环回跳只把执行权交还调度器，不代表经过任何确定时间。目标失效、`PAUSE` 切换、致命停止或程序退出会取消整个任务，而不是正常退出当前循环。

## 控制结构的组合

`if`、`repeat` 和 `while` 可以互相嵌套，并且都可以作为普通动作项直接连接：

```weave
F4:down =>
    if combat[on] then
        repeat count do
            if recoil > 1 then
                tap(ArrowDown)
            else
                tap(ArrowUp)
            end |
            set(recoil, recoil - 0.1)
        end
    else
        while F4[held] do
            tap(Space) |
        end
    end
    tap(Enter);
```

控制结构内部和外部都遵守同一动作流规则。运行时只有在遇到明确写出的 `|` 或 `gap()` 时，才等待 `ACTION_GAP` 指定的时间。

## 异步任务语义

每条具有非空动作流的匹配规则创建一个独立任务实例。任务实例共享编译后的只读动作程序，但拥有自己的执行位置、循环计数槽、等待状态和取消代际，因此同一规则可以同时存在多个互不覆盖的运行实例。

同一物理事件产生的多个任务按源码顺序提交。不同任务可以在等待和循环回跳处交错执行；每个动作内部保持自身规定的原子性和输入配对。

`PAUSE` 切换和其他全局取消使用递增代际。任务保存创建时的代际；代际不再相等时，该任务永久取消，即使 `PAUSE` 后来重新变为 `on` 也不会恢复旧任务。

## 规则作用范围和输入来源

当 `TARGET` 指定程序时，只有目标身份有效并拥有当前前台窗口时才查询规则。低级输入观察仍发生在当前桌面范围，但非目标输入不进入用户规则并直接放行。`GLOBAL` 明确取消前台目标限制。

运行时为自身输出添加内部标签。自身注入和第三方注入事件不进入用户规则，也不修改物理输入状态；它们继续进入系统输入流并交给目标程序处理。

## 控制名称

### 键盘

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
Digit0 ... Digit9
Numpad0 ... Numpad9
NumpadAdd
NumpadSubtract
NumpadMultiply
NumpadDivide
NumpadDecimal
```

左右修饰键不提供合并名称；需要任意一侧时在条件中明确连接左右状态。

### 鼠标

```text
Mouse.Left
Mouse.Right
Mouse.Middle
Mouse.X1
Mouse.X2
```

鼠标移动、滚轮和连续坐标输入不属于本版按钮语法。
