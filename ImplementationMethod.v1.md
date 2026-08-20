# InputWeaver 实现方法 v1

## 目标

本实现把 `.krm` 源码编译成按事件索引的只读规则、可复用的表达式程序和可复用的动作程序。低级输入回调只完成同步匹配、任务容量预留和消费决定；所有具有持续时间的动作由一个协作式任务调度线程异步执行。

核心复用单位不是内部伪事件，而是不可变动作程序。每次规则触发创建独立 `TaskInstance`，共享程序代码并持有自己的执行位置、循环局部槽、等待状态和取消代际。

## 组件

```text
Source File
    -> Compiler
        -> RuleIndex
        -> PredicatePrograms
        -> ActionPrograms
        -> RuntimeLayout

Windows Input Hook
    -> InputNormalizer
    -> ForceStopRecognizer
    -> RuleDispatcher
        -> MappingRuntime
        -> RuleIndex
        -> TaskScheduler

TaskScheduler
    -> ExpressionEvaluator
    -> RuntimeState
    -> InputInjector
    -> ProcessLauncher
```

### `Compiler`

编译器负责词法分析、语法分析、名称绑定、类型检查、表达式编译、动作程序编译、规则排序和隐藏运行状态分配。编译结果在运行期只读，用户源码不会在输入回调或任务执行期间重新解析。

### `RuleIndex`

`RuleIndex` 按规范化输入事件索引有序规则列表。列表严格保留源码顺序；相同事件和相同条件的规则也保留为独立记录。

### `RuleDispatcher`

`RuleDispatcher` 在低级钩子线程同步运行。它读取一次事件状态、扫描规则、收集全部应该触发的程序、一次性预留任务容量、提交映射隐藏状态，并在返回钩子前决定当前物理事件消费或放行。

### `TaskScheduler`

`TaskScheduler` 使用一个工作线程运行多个逻辑任务。它维护就绪队列、定时等待队列和固定容量的任务实例池，不为每个宏创建操作系统线程。

### `RuntimeState`

`RuntimeState` 保存用户变量、内蕴状态、物理控制状态、取消代际和表达式读取版本。用户变量具有编译期确定的类型和槽位；特殊内蕴值通过类型化句柄直接访问对应运行时字段。

### `MappingRuntime`

`MappingRuntime` 保存 `:=` 创建的隐藏活跃状态、锁存目标和输出所有权。它在普通用户规则之前处理已经活跃映射的重复与松开生命周期。

### `InputInjector`

`InputInjector` 把内部输入动作转换为带自身标签的 `SendInput` 批次，并维护已经实际发送的合成按下状态。自身标签分类发生在规则分派之前，因此程序输出不会重新触发用户规则。

### `ProcessLauncher`

`ProcessLauncher` 在任务线程上用 `CreateProcessW` 启动 `exec` 子进程。它直接使用编译产物中的命令行字符串和配置文件目录，不引入命令解释器、参数重写、进程等待或子进程所有权。

## 编译数据模型

### 事件键

```cpp
struct EventKey {
    DeviceKind device;
    ControlCode control;
    Transition transition;
};
```

`EventKey` 只描述规范化键盘或鼠标事件，不包含内部控制流。内部跳转始终属于动作程序位置，不进入事件索引。

### 规则

```cpp
enum class Delivery {
    Observe,
    Consume,
};

enum class MatchFlow {
    Stop,
    Continue,
};

enum class RuleKind {
    Event,
    MappingDown,
};

struct Rule {
    PredicateId predicate;
    ProgramId program;
    MappingId mapping;
    Delivery delivery;
    MatchFlow flow;
    RuleKind kind;
    SourceSpan source;
};
```

四种箭头直接编译为 `Delivery` 与 `MatchFlow` 的组合。没有动作的规则使用空 `ProgramId`，但仍保留消费和匹配流程字段。普通事件规则使用空 `MappingId`；`MappingDown` 规则通过 `MappingId` 引用目标、隐藏状态槽和映射动作程序。

普通事件规则只为源码明确声明的一个 `EventKey` 生成记录，不为 `:down` 自动生成 `:repeat` 或 `:up` 规则。消费型 `:up` 与其他单事件规则使用完全相同的数据结构。

`:=` 声明在源控制的 `:down` 列表中生成 `MappingDown` 规则。它的固定行为是消费并停止；条件和源码位置与普通规则相同。

### 表达式程序

表达式编译为有类型的只读节点或紧凑栈指令。变量名称在编译时解析成 `ValueRef`，运行时不进行字符串查找。

```cpp
struct ValueRef {
    ValueDomain domain;
    ValueType type;
    std::uint32_t index;
};
```

`ValueDomain` 区分用户槽、内蕴值、物理状态和任务局部槽。表达式求值器由事件条件、`if`、`repeat`、`while` 和 `set` 共用。

### 动作程序

动作程序是不可变指令数组。基础指令集合保持有限：

```text
Press
Release
Tap
Wait
Gap
Set
Toggle
Exec
Jump
JumpIfFalse
RepeatInit
RepeatNext
Yield
End
```

动作调用按源码顺序编译为基础指令，源码中的空白不生成指令。每个 `|` 和每次 `gap()` 调用都编译为一条 `Gap` 指令；空动作流只生成 `End`。

### 任务实例

```cpp
struct TaskInstance {
    ProgramId program;
    std::uint32_t position;
    LocalFrame locals;
    std::uint64_t cancellationGeneration;
    TaskStatus status;
    WakeTime wakeTime;
};
```

`LocalFrame` 的大小由动作程序在编译时计算。每个 `repeat` 位置分配一个局部整数 `index` 和一个局部 `number limit`；嵌套循环使用不同槽位。同一规则的多个任务实例共享动作程序，但永远不共享循环槽或执行位置。

任务实例、局部帧和调度节点从启动时建立的固定容量池分配。输入钩子不进行堆分配。

## 编译流程

### 词法和语法

源码读取器验证 UTF-8。词法分析器只允许关键字、标识符、字符串字面量、控制名称、多字符箭头和标点使用 ASCII，同时允许注释正文包含有效的非 ASCII UTF-8 文本。`//` 在换行或文件末尾结束，`/* ... */` 可以跨行但不嵌套。`=>>` 与 `~>>` 必须在较短箭头之前进行最长匹配。

动作流解析器按顺序读取动作调用、控制结构和 `|`；记号边界明确时允许动作紧邻书写，通常使用空白提高可读性。事件箭头后直接出现分号时生成空动作程序。`|` 编译为 `Gap` 指令，逗号只由函数实参解析器用作参数分隔。

### 名称和类型

用户变量声明建立唯一符号。重复声明、类型冲突和内蕴名称冲突终止编译。事件规则不执行重复或重叠拒绝，所有规则按源码顺序保留。

变量声明初值只接受与声明类型一致的字面量。运行时表达式只通过动作修改变量，不参与启动期声明求值。

`when`、`if` 和 `while` 要求布尔条件；`repeat` 要求有限 `number`；`wait`、`TAP_DURATION` 和 `ACTION_GAP` 要求 `duration`。表达式编译结果携带静态类型。省略配置时编译产物使用 `TAP_DURATION = 30ms` 和 `ACTION_GAP = 10ms`；两者的显式值都必须位于闭区间 `0ms` 到 `1min`。

### `if` 降低

```text
JumpIfFalse(condition, elsePosition)
thenProgram
Jump(endPosition)
elsePosition:
elseProgram
endPosition:
```

省略 `else` 时，条件为假直接跳到结束位置。

### `repeat` 降低

```text
RepeatInit(localSlot, limitExpression)
checkPosition:
JumpIfNotLess(localSlot.index, localSlot.limit, endPosition)
bodyProgram
RepeatNext(localSlot)
Yield
Jump(checkPosition)
endPosition:
```

`RepeatInit` 只求值一次 `limitExpression`，并把 `index` 置为零。`RepeatNext` 把整数索引加一。判断只使用 `index < limit`，不进行取整；`limit = 5.8` 时执行六次，`limit <= 0` 时执行零次。索引无法继续增长时进入致命停止。

循环回跳中的 `Yield` 只交还调度权，不创建定时等待，也不等待 `ACTION_GAP` 指定的时间。

### `while` 降低

```text
checkPosition:
JumpIfFalse(condition, endPosition)
bodyProgram
Yield
Jump(checkPosition)
endPosition:
```

`while` 每次回跳后重新求值条件。条件为假属于正常结束，程序位置继续进入循环后的动作。

### 完整映射降低

每个 `:=` 声明生成一个 `MappingDescriptor`、一个隐藏活跃 `state` 槽和一个 `MappingDown` 规则。固定目标存放在描述符中，条件只属于 `MappingDown`。

```text
source down + predicate
    -> reserve target press
    -> commit hidden active state
    -> consume and stop

source repeat + hidden active state
    -> reserve target repeat
    -> consume

source up + hidden active state
    -> reserve target release
    -> clear hidden active state
    -> consume
```

隐藏状态在输入事件批次成功预留后、钩子返回前同步提交，不能等待异步 `press` 指令执行。这样快速按下和松开仍会按队列顺序生成目标按下与松开。

条件映射的多个声明各有隐藏状态，但同一源在任意时刻至多有一个活跃描述符。运行时可以把这些布尔槽压缩为一个活跃映射编号，只要对编译语义保持等价。

## 输入分派

### 钩子处理顺序

低级键盘和鼠标钩子按照以下固定顺序处理事件：

1. 验证钩子代码并规范化键盘或鼠标数据。
2. 根据 injected 标志和自身标签分类输入来源。
3. 自身注入和第三方注入事件直接交给下一个钩子，不更新物理状态，不查询用户规则。
4. 对物理事件更新运行时物理 `[held]` 或 `[idle]` 状态。
5. 检查停止请求、`PAUSE`、目标身份、前台窗口和鼠标路由约束，并捕获当前取消代际。
6. 为活跃 `:=` 映射生成本事件的内部生命周期动作和初始消费决定。
7. 获取稳定的事件状态版本并按源码顺序扫描 `RuleIndex[EventKey]`。
8. 收集匹配任务、映射提交和最终消费决定。
9. 一次性预留本事件需要的全部任务实例和队列位置。
10. 重新验证取消代际和 `PAUSE`；发生变化时放弃预留、放行当前事件并结束本次分派。
11. 提交隐藏映射状态并以捕获的代际一次性发布任务批次。
12. 根据最终消费决定返回非零或调用下一个钩子。

物理状态在条件求值前更新，因此当前 `:down` 可以观察 `[held]`，当前 `:up` 可以观察 `[idle]`。

### 规则扫描

规则扫描使用以下逻辑：

```text
consumed = mappingLifecycleConsumed

for rule in RuleIndex[event]:
    if not evaluate(rule.predicate, eventSnapshot):
        continue

    if rule.kind is MappingDown:
        selectedMappings.push(rule.mapping)

    if rule.program is not empty:
        selectedPrograms.push(rule.program)

    if rule.delivery is Consume:
        consumed = true

    if rule.flow is Stop:
        break
```

空程序不创建任务，但仍修改 `consumed` 或停止扫描。后续观察规则不能把 `consumed` 改回放行。

活跃映射生命周期在普通规则之前完成选择，因此普通停止规则不能阻止已锁存目标的重复或松开。生命周期处理之后仍扫描普通规则，使用户能够为源 `:repeat` 或 `:up` 附加观察动作。

### 同一事件状态

一个物理事件的整个规则扫描使用同一个逻辑状态版本。选中的任务在扫描和批次预留完成后才发布，因此前面规则中的 `set` 或 `toggle` 不会影响本事件后续规则的条件。

用户运行值存储为可原子读取的固定槽，调度线程是唯一写入者。写入时递增全局状态版本；钩子在扫描前后验证版本一致，版本变化时使用预分配暂存区重新扫描。稳定快照长期无法获得时，当前事件放行并进入致命停止，避免在低级钩子中无限重试。

### 批次提交

同一事件选择的多个任务按源码顺序形成一个不可拆分提交批次。调度器必须先确认全部任务实例和局部帧容量，再发布其中任何一个任务。

预留失败不产生部分任务，不提交隐藏映射状态，当前物理事件放行，并请求致命停止。

## 协作式任务调度

### 单工作线程

调度器只有一个动作工作线程。所有用户变量修改、任务状态变更和动作程序位置推进都在该线程串行完成，因此任务之间没有变量写入数据竞争。

钩子线程只发布完整任务批次。调度线程按照批次到达顺序和批次内部源码顺序把任务放入就绪队列。

### 执行片段

一个就绪任务连续执行相邻且不会暂停的指令，直到发生以下任一情况：

- 执行 `Gap` 或 `Wait` 并进入定时等待。
- `Tap` 已发送按下，并处于等待 `TAP_DURATION` 指定时间的阶段。
- 执行循环回跳 `Yield`。
- 程序到达 `End`。
- 取消代际失效。

执行完 `Press`、`Release`、`Set`、`Toggle` 或控制跳转后，任务立即执行下一条指令。循环即使没有 `|`、`gap()` 或 `wait(...)`，也会在每轮结束时交还一次调度权，使其他任务和停止动作能够运行；这个调度让步不代表等待了某段确定时间。

### 定时等待

`Gap` 读取 `ACTION_GAP` 并等待它指定的时间，`Wait` 等待其表达式求值所得的时间，`Tap` 在按下和松开之间读取 `TAP_DURATION` 并等待它指定的时间。调度器把唤醒时间放入最小堆或等价有序结构，不使用阻塞睡眠占住工作线程。

等待完成后任务重新进入就绪队列。取消事件唤醒调度线程并使所有旧代际等待立即失效。

### 外部进程启动

`Exec` 在任务线程调用 `CreateProcessW`，把编译产物中的单个命令字符串原样作为 `lpCommandLine`，并把 `.krm` 文件所在目录作为 `lpCurrentDirectory`。`lpApplicationName` 为空，因此可执行文件路径和参数边界遵循 Windows 命令行规则；编译器和运行时不执行 shell 展开。需要命令解释器语义的源码明确把 `cmd.exe /d /s /c` 写入命令字符串。

创建成功后立即关闭运行时持有的进程和主线程句柄，`Exec` 接着执行下一条指令。子进程独立存活，任务取消、`PAUSE`、强制停止和主程序退出都不终止它。创建失败时记录 Win32 错误，释放当前任务拥有的输出并结束当前任务，不停止其他任务或主程序。语言不等待或报告子进程退出状态。

## 运行值和表达式

### 用户值

编译器为每个用户变量分配固定槽：`state` 使用布尔表示，`number` 使用有限 `double`，`duration` 使用非负内部时长表示。声明初值在运行时启动前完成初始化。

### 内蕴值

`PAUSE`、`TAP_DURATION`、`ACTION_GAP`、目标状态和物理控制状态通过专用 `ValueRef` 域访问实际运行时字段，不复制成用户变量。表达式求值器通过统一的类型化读取接口访问所有值。

### 求值时机

- `when` 使用事件开始的稳定状态版本。
- `if` 在任务执行到分支位置时读取当前运行值。
- `repeat` 在进入循环时读取一次限制表达式并保存到任务局部槽。
- `while` 在每轮开始时读取当前运行值。
- `set` 在执行动作时求值右侧表达式并原子发布新值。
- `toggle` 在执行动作时读取并翻转目标 `state`。

## `PAUSE` 和取消代际

运行时维护单调递增的 `cancellationGeneration`。每个任务保存创建时的代际。

`PAUSE` 的值真正发生切换时执行以下事务：

1. 原子更新 `PAUSE`。
2. 递增 `cancellationGeneration` 和运行状态版本。
3. 禁止新规则分派或按照新 `PAUSE` 状态恢复分派。
4. 唤醒任务调度线程。
5. 丢弃所有旧代际就绪任务和等待任务。
6. 释放程序实际持有的全部合成键盘键和鼠标按钮。
7. 清除活跃映射和待提交映射状态。

触发 `PAUSE` 切换的任务携带旧代际，因此 `set(PAUSE, off)` 完成后不会执行其后续指令。以后重新进入 `PAUSE[on]` 时只接受新任务，旧任务不会恢复。

目标失效、强制停止和程序退出可以复用同一取消代际机制，并根据原因决定是否继续接受新事件或结束进程。

## 强制停止识别

`ForceStopRecognizer` 接收已经规范化且被判定为物理候选的控制事件，并在目标检查、`PAUSE` 和 `RuleDispatcher` 之前运行。默认配置识别任意一侧 `Ctrl`、任意一侧 `Shift` 与 `F12`。识别器拥有独立物理状态，不读取用户变量，也不创建用户任务；完成组合的事件由识别器接管，不进入用户规则。

组合触发后，应用运行时禁止新任务和新映射，递增取消代际，唤醒调度线程，释放全部程序输出，清除活跃映射，卸载钩子并按正常强制停止退出。组合配置属于主程序配置层，编译后的 `.krm` 无法覆盖或禁用识别器。

## 输出所有权

运行时只为成功发送的合成按下登记所有权。每个输出控制维护所有者集合或等价引用计数；多个任务或映射同时持有同一控制时，只在第一个所有者出现时发送按下，只在最后一个所有者消失时发送松开。

这个零到非零、非零到零的转换就是输出冲突策略。重叠 `Press` 或 `Tap` 为各自任务取得独立所有权，但不会在已经按下的控制上再次注入按下。`Tap` 的临时所有权保持 `TAP_DURATION` 指定的时间；如果其他所有者仍然存在，就不注入松开。运行时不通过临时释放其他所有者来强制制造点击边缘，因此重叠点击可以合并为一次较长保持，但不会破坏已有持有状态。

`Tap` 使用独立的配对状态。取消发生在点击持续阶段时，调度器跳过剩余等待并立即安排必要松开。

`release` 只能释放当前任务或规则语义拥有的控制。取消、目标失效和致命停止通过系统清理路径释放全部实际持有状态，不依赖用户动作程序继续运行。

输入注入失败或部分发送时，注入器记录已经成功发送的前缀并生成对应清理动作。清理所需状态预先分配，避免在资源不足路径中再次依赖动态内存。

## 目标和输入来源

目标选择器在启动时解析为唯一进程身份并保留进程句柄，避免只依赖可复用 PID。规则分派前检查目标是否仍有效并拥有前台窗口；鼠标源或鼠标按钮输出还检查当前指针或捕获窗口的目标归属。

动作真正注入前再次检查目标身份和路由条件。检查只能缩小 Windows 前台和鼠标路由变化窗口，`SendInput` 本身不绑定目标 PID。

低级钩子首先使用 injected 标志区分物理候选和注入事件，再使用 32 位自身标签识别程序输出。自身和第三方注入事件都不进入用户规则。

## 致命运行错误

任务池耗尽、任务批次无法完整预留、局部帧容量不足、循环索引无法继续增长以及内部指令不变量破坏都属于致命运行错误，不作为普通规则失败恢复。

致命错误发生在输入回调时，当前物理事件放行。运行时随后执行受控停止：禁止新任务、递增取消代际、唤醒调度器、释放输出所有权、清除映射状态、卸载钩子并以非零状态退出。

受控停止所需事件、状态和清理记录在启动时预先建立。致命路径不记录无界日志，也不申请新的任务容量。

## 线程模型

```text
Hook Thread
    input normalization
    physical state update
    force-stop recognition
    target and PAUSE guard
    mapping lifecycle selection
    rule scan
    atomic task batch publication
    consume or forward return

Task Thread
    ready task execution
    timer management
    user value writes
    input injection
    child process creation
    cancellation cleanup

Diagnostic Thread
    bounded records
    file output
```

钩子线程不执行等待、输入注入、外部命令、文件访问、日志格式化或动态内存分配。任务线程不调用低级钩子返回接口。诊断线程只消费有界记录，不参与规则决策。

## 模块边界

```text
src/compiler/
    lexer
    parser
    symbols and types
    expression compiler
    action compiler
    rule compiler

src/core/
    event and control types
    rule index
    predicate program
    action program
    runtime values
    task instance

src/app/
    force-stop recognizer
    rule dispatcher
    task scheduler
    mapping runtime
    cancellation runtime
    application lifecycle

src/platform/windows/
    low-level hooks
    input normalization
    SendInput injector
    process launcher
    target locator and identity

src/diagnostics/
    bounded records
    JSONL writer
```

`compiler` 和 `core` 不包含 Win32 头文件。`platform/windows` 把系统事件转换为 `core` 类型，并提供输入注入、进程启动和目标检查的窄接口。规则匹配、循环执行和变量语义可以在不安装真实钩子的测试中验证。

## 验证重点

- `repeat 5.8` 创建六次迭代，限制表达式只求值一次。
- 同一循环规则的多个 `TaskInstance` 使用不同局部槽，互不重置计数。
- 非注释记号只接受 ASCII，UTF-8 注释允许非 ASCII，行注释在换行或文件末尾正确结束。
- 仅用空白分开的连续动作不会等待 `ACTION_GAP` 指定的时间；每个开头、结尾或连续 `|` 以及每次 `gap()` 调用都精确编译出一条 `Gap` 指令。
- `TAP_DURATION` 和 `ACTION_GAP` 的默认值及 `0ms` 到 `1min` 边界得到编译期验证。
- `=>`、`=>>`、`~>` 和 `~>>` 的消费与停止组合符合真值表。
- 同一事件的后续条件看不到本事件前面任务尚未执行的变量修改。
- 空动作规则不创建任务，但仍消费或停止匹配。
- 快速源按下和松开在异步执行前已经锁存 `:=` 状态，并按顺序生成目标按下和松开。
- 活跃映射的目标松开不会被普通停止规则截断。
- 多个源持有同一目标时，提前释放一个源不会提前发送目标松开。
- 已被持有的输出上执行 `Tap` 不制造破坏其他所有者的松开和重新按下边缘。
- `PAUSE` 切换取消所有旧代际任务、唤醒等待并释放程序输出。
- 物理强制停止组合在目标和用户规则之前生效，注入事件不能触发它。
- `Exec` 使用配置目录和原始 Windows 命令行，创建后不等待；取消任务不会终止已经成功创建的子进程。
- 循环没有时间等待时仍在回跳处让出，使其他任务和停止动作能够运行。
- 多规则事件的任务容量以批次预留；容量不足时没有部分发布，当前事件放行并进入致命停止。
