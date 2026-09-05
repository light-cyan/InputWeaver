# 鼠标移动、坐标与滚轮：语法与语义设计

## 文档定位

本文保存在 `development/phase-16`，记录鼠标移动、坐标、滚轮、周期统计和事件快照的语法与可观察语义，作为后续各开发 phase 共用的语言设计输入。当前产品的正式语言定义以 [docs/grammar.md](../../docs/grammar.md) 为准。

本文使用中文说明设计，Weave 关键字、标识符、字段和动作名称使用英文。示例中的事件格式统一为“来源名 + 冒号 + 小写事件名”。

具名事件源具有两个读取视图：直接使用事件源名称读取正在累计的周期，在名称前加 `@` 读取本次规则触发上下文中选定的已完成周期。字段通过点号访问。第 8 节规定本来源与跨来源读取的快照语义；具体事件源名称仅用于示例。

## 1. 语法速览

| 写法 | 含义 |
| --- | --- |
| `Mouse:move` | 一次原始移动事件。 |
| `Mouse:wheel` | 一次垂直滚轮事件。 |
| `Mouse:horizontalwheel` | 一次水平滚轮事件。 |
| `Mouse.x`、`Mouse.y` | 读取鼠标当前的虚拟桌面坐标。 |
| `Mouse.dx`、`Mouse.dy` | 读取最近一次鼠标数值输入中的位移量，随新输入更新。 |
| `Mouse.wheel_x`、`Mouse.wheel_y` | 读取同一次最近输入中的滚轮量，随新输入更新。 |
| `Mouse.moving` | 读取鼠标当前是否处于有效持续移动段，取 `on` 或 `off`。 |
| `Mouse.idle_time` | 读取距上次有效物理移动经过的时间。 |
| `event travel = Mouse:move every 40;` | 声明 `travel`，每累计 40 像素路径长度形成一个周期事件。 |
| `event pulse = Mouse:move every 100ms;` | 声明 `pulse`，持续移动期间每完成 100 毫秒形成一个周期事件。 |
| `event verticalScroll = Mouse:wheel every 1;` | 声明 `verticalScroll`，垂直滚动每累计一个标准刻度形成一个周期事件。 |
| `event horizontalScroll = Mouse:horizontalwheel every 1;` | 声明 `horizontalScroll`，水平滚动每累计一个标准刻度形成一个周期事件。 |
| `travel:tick` | 订阅 `travel` 完成一个周期的事件。 |
| `travel.progress` | 读取事件源正在进行的周期进度。 |
| `travel.start_x`、`travel.start_y` | 读取 `travel` 正在累计的移动周期的起始位置。 |
| `travel.dx` | 读取 `travel` 正在累计的周期的横向净位移。 |
| `@travel.start_x`、`@travel.start_y` | 读取所选已完成移动周期的起始位置。 |
| `@travel.dx` | 读取为本次规则触发选定的 `travel` 已完成周期的横向净位移。 |
| `@travel.valid` | 判断本次规则触发上下文中是否存在可读取的 `travel` 已完成周期。 |
| `move_by(dx, dy)` | 按像素相对移动指针。 |
| `move_to(x, y)` | 移动到虚拟桌面绝对坐标。 |
| `scroll(amount)` | 发出指定刻度数的垂直滚动。 |
| `scroll_horizontal(amount)` | 发出指定刻度数的水平滚动。 |

移动距离使用无后缀的 `number`，例如 `40`、`8.5`。时间使用 `duration`，直接书写时以 `100ms` 这样的形式表达；时间变量直接引用自己的名字。滚轮数值的默认单位是标准刻度，例如 `1`、`0.25`。

## 2. 完整示例

```weave
TARGET = GLOBAL;
MOUSE_IDLE_TIMEOUT = 80ms;

number stride = 40;
duration interval = 100ms;
number lastDx = 0;
number lastDy = 0;

event travel = Mouse:move every stride;
event pulse = Mouse:move every interval;
event verticalScroll = Mouse:wheel every 1;

travel:tick when @travel.dx > 0
    ~>> set(lastDx, @travel.dx);

travel:tick
    ~> set(lastDy, @travel.dy);

pulse:tick ~> tap(F2);

verticalScroll:tick when @verticalScroll.wheel_y > 0
    ~> tap(PageUp);

verticalScroll:tick when @verticalScroll.wheel_y < 0
    ~> tap(PageDown);

F1:down when @travel.valid == on
    ~> wait(100ms) move_by(@travel.dx, @travel.dy);

F3:down => move_to(Mouse.x + 20, Mouse.y);
F4:down => set(stride, 80);
F5:down => set(interval, 200ms);
F6:down => move_by(40, -20);
F7:down => move_to(1200, 600);
F8:down => scroll(1);
F9:down => scroll_horizontal(-1);
```

`travel` 的两条规则订阅同一个事件源。第一条规则使用 `~>>`，匹配后继续检查后面的订阅规则；第二条规则仍然能够读取同一次触发的快照。修改 `stride` 或 `interval` 后，新值按照第 7 节的周期锁定规则生效。

F1 规则读取 F1 本次触发时 `travel` 最近完成的周期，等待 100ms 后仍使用这份快照。`@travel.valid == on` 用于确认已经存在一次完成记录。

## 3. 原始事件与周期事件

### 3.1 原始事件

原始事件对应一次输入报告，每次报告分别触发规则匹配。它们可以直接形成独立规则，在条件中通过 `Mouse.dx/dy` 和 `Mouse.wheel_x/wheel_y` 判断当前报告的数值，也可以读取具名事件源的实时状态或已完成周期。

```weave
Mouse:move when Mouse.dx > 0 ~> tap(F1);

Mouse:wheel when Mouse.wheel_y > 0 ~> tap(PageUp);

Mouse:horizontalwheel when Mouse.wheel_x < 0 ~> tap(ArrowLeft);
```

原始事件沿用现有规则箭头：`=>` 消费并停止扫描，`=>>` 消费并继续扫描，`~>` 放行并停止扫描，`~>>` 放行并继续扫描。消费决定针对当前原始输入，输出动作随后按照任务调度规则执行。

这次事件的条件读取包含当前报告的一致鼠标状态。动作中的 `Mouse.*` 则按动作求值时读取最新状态，届时数值可能已经由之后的输入更新；第 9.4 节规定具体读取语义。

### 3.2 周期事件

周期统计通过 `event` 声明，`every` 指定周期。统计达到一个周期边界时，形成该具名来源的 `:tick` 事件，规则通过来源名订阅。

```weave
event travel = Mouse:move every 40;

travel:tick when @travel.dx > 0
    ~> tap(F1);
```

周期事件接受 `~>` 和 `~>>` 两种观察箭头。此前参与累计的原始输入已经分别完成放行或消费决定，因此周期完成时的规则负责响应统计结果。原始输入的即时改写由原始事件规则承担。

一次原始输入可以同时推进多个统计实例。某条普通规则是否匹配、是否停止扫描，不改变这些实例对同一次合格输入的统计。

## 4. `event` 类型与实例身份

`event` 声明一个程序级、具名且持续存在的周期事件源。声明必须位于第一次使用之前，周期表达式依赖的变量也必须先声明。

```weave
number stride = 40;

event fine = Mouse:move every stride;
event coarse = Mouse:move every 80;

fine:tick ~> tap(F1);
coarse:tick ~> tap(F2);
```

事件源拥有自己的周期表达式、当前采用的周期长度、累计状态和相位。`fine` 与 `coarse` 独立运行。两个分别声明的事件源即使表达式相同，也分别拥有自己的身份和状态。

多条规则订阅同一个具名事件源时，共享这个源形成的周期边界和触发数据。规则按照源码顺序、条件和箭头进行匹配。

每个周期统计实例都由具名 `event` 声明建立。声明名称同时用于规则订阅、实时字段读取以及 Debug STATE 和事件记录中的来源展示。

`event` 的身份由声明确定。`travel` 与 `@travel` 是同一个事件源的两个读取视图；前者查询当前周期，后者查询已完成周期。订阅 `travel:tick` 属于事件匹配。

## 5. 距离、位移与滚轮累计

### 5.1 路径长度和净位移

`event travel = Mouse:move every 40;` 累计观测到的移动路径长度。每段输入位移为 `(dx, dy)`，这段路径长度为 `sqrt(dx * dx + dy * dy)`。

净位移是所有横向、纵向位移分别相加的结果；路径长度是各段长度相加的结果。二者表达不同的信息。

| 输入过程 | `dx` | `dy` | `distance` |
| --- | ---: | ---: | ---: |
| 向右 10 像素 | 10 | 0 | 10 |
| 再向左 10 像素，回到起点 | 0 | 0 | 20 |

第二行的 `dx` 和 `dy` 都是零，但已经经过了 20 像素路径。

### 5.2 `every` 的边界和余量

每完成一个周期形成一个事件，跨过边界后的余量继续属于下一个周期。例如 `every 8` 接收累计距离 27：

```text
Completed boundaries: 8, 16, 24
Current progress: 3
Remaining distance: 5
```

这对应三个周期事件，每个周期分别触发订阅规则，余量留在当前进度中。

周期快照中的 `dx`、`dy`、`distance` 只描述该周期内的移动。周期边界对应的位置既是该周期的结束位置，也是连续的下一个周期的起始位置；余量从这个起点继续累计。

普通停止移动保留距离进度。例如先走 7 像素，停止后再走 1 像素，可以完成一个 `every 8` 周期。

### 5.3 周期起始位置

移动来源提供 `start_x` 和 `start_y` 两个只读 `number` 字段，表示该周期的起始坐标。初始周期的起点是第一段计入该周期的移动的开始位置；统计重置后，新的统计段重新建立起点。连续周期的起点接续上一周期的结束位置。

`travel.start_x`、`travel.start_y` 属于正在累计的周期，在该周期内保持固定；`travel.x`、`travel.y` 随该周期纳入的新移动更新。周期完成后，`@travel.start_x`、`@travel.start_y` 与结束坐标、净位移等字段一起固定在完成视图中。

例如 `event travel = Mouse:move every 24;` 从 `(100, 200)` 向右累计移动 27：

| 视图 | `start_x`、`start_y` | `x`、`y` | `distance` |
| --- | --- | --- | ---: |
| 已完成周期 `@travel` | `(100, 200)` | `(124, 200)` | 24 |
| 正在累计的周期 `travel` | `(124, 200)` | `(127, 200)` | 3 |

此时鼠标当前位置是 `(127, 200)`，已完成周期的结束位置是 `(124, 200)`。起点是每个周期自己的属性；在同一统计坐标基准下，净位移满足 `dx = x - start_x`、`dy = y - start_y`。

```weave
event travel = Mouse:move every 24;

F1:down when @travel.valid == on
    ~> move_to(@travel.start_x, @travel.start_y);
```

这条规则请求回到 F1 本次触发时选定的已完成周期的起点。

### 5.4 用坐标和位移表达条件

屏幕坐标向右为 X 正方向，向下为 Y 正方向。`x`、`y` 表示绝对位置，`dx`、`dy` 表示位移。移动方向、主次轴和斜率条件由用户使用位移数值表达，位置区域则使用绝对坐标表达。

```weave
event travel = Mouse:move every 40;

travel:tick when @travel.dx > 0 and @travel.dy < 0
    ~> tap(F1);
```

这里表示每经过 40 像素路径后，检查这个周期是否同时具有向右和向上的净位移。`when` 过滤已完成周期的响应，路径累计仍然包含各个方向的移动。

### 5.5 滚轮

垂直滚轮的正值表示向上，负值表示向下；水平滚轮的正值表示向右，负值表示向左。`1` 表示一个标准刻度，允许 `0.25` 这样的细分量。

```weave
event verticalScroll = Mouse:wheel every 1;
event horizontalScroll = Mouse:horizontalwheel every 1;

verticalScroll:tick when @verticalScroll.wheel_y > 0 ~> tap(PageUp);
verticalScroll:tick when @verticalScroll.wheel_y < 0 ~> tap(PageDown);
horizontalScroll:tick when @horizontalScroll.wheel_x > 0 ~> tap(ArrowRight);
```

累计保留符号，反向滚动抵消当前余量。例如 `+0.25` 后收到 `-0.10`，剩余累计为 `+0.15`。当累计值达到 `+period` 或 `-period` 时形成对应方向的周期事件，并扣除这个带符号的完整周期。

例如 `every 1` 收到净累计 `+2.25` 时，形成两个向上的周期事件并留下 `+0.25`；停止滚动保留余量。

## 6. 持续移动与时间周期

### 6.1 内蕴配置量

```weave
MOUSE_IDLE_TIMEOUT = 80ms;
```

`MOUSE_IDLE_TIMEOUT` 是 `duration` 类型的内蕴配置量，默认值为 `80ms`。顶层最多配置一次，接受正的时间字面量，运行期间只读；表达式可以读取它。

连续达到这个时间没有新的有效移动，就结束当前持续移动段。零位移报告不延长持续移动段。停止间隔是连续性判定值，和 `every` 指定的触发周期各自独立。

`Mouse.moving` 使用同一个停止间隔判断全局物理移动状态；`Mouse.idle_time` 给出距离上次有效物理移动的实际间隔，具体定义见第 9.4 节。

### 6.2 时间起点与周期边界

```weave
event pulse = Mouse:move every 100ms;
pulse:tick ~> tap(F1);
```

第一次有效移动建立当前连续移动段的起点 `t0`。后续有效移动在停止间隔以内到达，就属于同一连续段，周期边界依次为 `t0 + 100ms`、`t0 + 200ms`、`t0 + 300ms`。

周期事件在新的移动输入确认已跨过边界时产生。如果确认第一条边界的输入在 `t0 + 103ms` 才到达，事件可以此时送达，下一条逻辑边界仍然是 `t0 + 200ms`。周期相位由移动段起点和各周期长度确定。

停止检测负责结束连续段。时间周期的推进和触发由有效移动确认，单纯经过静止时间不产生移动周期事件。STATE 中的已确认进度也依据有效移动更新。

停止移动后，时间周期的未完成进度清零；重新移动建立新的 `t0`。因此移动 60ms、停止、再移动 40ms，不等同于持续移动 100ms。

同一次输入跨过多个时间边界时，形成对应数量的周期事件。跨边界的数据归属、首次移动的数据归属和停止阈值恰好相等时的处理，属于第 13 节列出的待细化语义。

## 7. 动态周期表达式

`every` 接受周期表达式。移动来源的 `number` 表达式按距离解释，`duration` 表达式按持续移动时间解释；滚轮的数值周期按刻度解释。

```weave
number stride = 40;
duration interval = 100ms;

event travel = Mouse:move every stride;
event coarse = Mouse:move every stride * 2;
event pulse = Mouse:move every interval;

F1:down => set(stride, 80);
F2:down => set(interval, 200ms);
```

每个周期开始时求值一次，并将结果固定为当前周期长度。有效周期长度必须大于零，表达式类型在编译时确定。

| 操作 | 结果 |
| --- | --- |
| 当前周期为 80，进度为 35 | 仍需完成 45。 |
| 此时把变量改为 20 | 当前周期继续使用 80，已有进度保持 35。 |
| 当前周期完成 | 后续新开启的周期读取已经提交的新值 20。 |

周期表达式依赖的变量在规则动作实际提交修改后才具有新值。已经开启的周期保持其固定值，包括周期事件自身创建的异步动作后来修改变量的情况。同一次输入产生多个周期时，周期表达式和规则条件采用该次输入确定的共享变量快照。

显式重新开始某个事件源的统计周期，可以同时放弃已有进度、建立新相位并读取新周期值；对应动作的具体语法在第 13 节列为待细化项。

## 8. 已完成周期视图与 `@` 修饰符

### 8.1 修饰来源，再访问字段

`@` 放在事件源名称前，选择这个事件源的已完成周期视图；后面的点号用于访问字段。例如 `@travel.dx` 表示选定的 `travel` 已完成周期的横向净位移。

| 写法 | 读取对象 | 随时间变化的方式 |
| --- | --- | --- |
| `travel.dx` | `travel` 当前正在累计的周期。 | 每次表达式求值读取当时的实时状态。 |
| `@travel.dx` | 本次规则触发上下文中选定的 `travel` 已完成周期。 | 条件和该次规则任务的完整动作流读取同一个固定值。 |

```weave
event travel = Mouse:move every 40;

travel:tick when @travel.dx > 0
    ~> wait(100ms) move_by(@travel.dx, @travel.dy);
```

`travel` 与 `@travel` 使用同一个声明名称。字段名跟在来源后面，条件和动作都明确写出所读取的事件源。

### 8.2 本来源与跨来源的选择规则

其他来源的规则允许读取 `@travel`。选取哪一个已完成周期，由本次规则的触发上下文确定。

| 读取位置 | `@travel` 选取的周期 |
| --- | --- |
| `travel:tick` 的条件和动作 | 触发这次规则的那个 `travel` 已完成周期。 |
| `pulse:tick`、键盘事件、鼠标原始事件等其他来源的规则 | 该次事件开始匹配时，`travel` 最近完成的周期。 |
| 同一个事件匹配的多条规则 | 对同一个来源选取相同的完成周期。 |

周期一旦完成，就成为可供后续事件读取的完成记录。某条订阅规则的 `when` 是否为真、是否执行动作，都不影响完成记录的产生。

```weave
event travel = Mouse:move every 40;
event pulse = Mouse:move every 100ms;

F1:down when @travel.valid == on
    ~> wait(100ms) move_by(@travel.dx, @travel.dy);

pulse:tick when @travel.valid == on and @travel.dx > 0
    ~> tap(F2);
```

F1 规则使用 F1 本次触发时选中的 `travel` 周期。`pulse` 规则则使用本次 `pulse:tick` 触发上下文中最近完成的 `travel` 周期；其中 `@pulse` 仍对应触发这条规则的 `pulse` 周期。

### 8.3 条件、等待和动作共享同一次选择

完成周期视图在触发事件开始匹配时确定。任务中的所有访问，包括第一次发生在 `wait` 之后的访问，以及嵌套 `if`、`repeat`、`while` 中的访问，都使用这次选择。

| 时刻 | 行为 | 对 F1 任务中 `@travel` 的影响 |
| --- | --- | --- |
| 0ms | `travel` 完成周期 A，`dx=40`。 | 成为此时最近的完成记录。 |
| 10ms | F1 触发，条件成立，任务开始等待 100ms。 | 选定 A。 |
| 30ms | `travel` 完成周期 B，`dx=-40`。 | F1 任务仍保持 A。 |
| 50ms | 另一个键盘事件触发新规则。 | 新规则选定 B，F1 任务仍保持 A。 |
| 110ms | F1 任务恢复并读取 `@travel.dx`。 | 读到 40。 |

`@travel` 在一个任务中保持固定；新的事件触发则可以选择更新的完成周期。`travel` 的实时字段始终按各次求值时的当前累计状态读取。

### 8.4 尚无完成周期：`valid`

`@travel.valid` 是只读的 `state` 字段，取 `on` 或 `off`。选取到了一个已完成周期时为 `on`；截至本次触发尚无可选周期时为 `off`。在 `travel:tick` 自己的规则中，它总是 `on`。

`valid` 也属于本次触发固定的视图。一个触发时尚无完成周期的任务，即使等待期间出现了首次完成，自己的 `@travel.valid` 仍为 `off`；后续新触发的规则可以得到有效视图。

访问有效视图的数值字段，可以先使用短路条件进行保护：

```weave
event travel = Mouse:move every 40;

F1:down when @travel.valid == on and @travel.dx > 0
    ~> move_by(@travel.dx, @travel.dy);
```

当 `valid` 为 `off` 时，读取该视图的其他字段会使表达式求值失败。在规则条件中，此规则不匹配并产生诊断；在动作流中，当前任务失败。短路求值未访问的数据字段不会引发这一错误。

### 8.5 已完成周期字段

下表中的 `source` 指已经声明的具名事件源，实际代码使用对应名称，例如 `@travel.dx` 或 `@verticalScroll.wheel_y`。

| 字段 | 类型 | 所属来源 | 含义 |
| --- | --- | --- | --- |
| `@source.valid` | `state` | 所有周期来源 | 本次触发是否选到了有效的已完成周期。 |
| `@source.start_x`、`@source.start_y` | `number` | 移动 | 该已完成周期的起始坐标。 |
| `@source.x`、`@source.y` | `number` | 移动、滚轮 | 该已完成周期结束边界对应的坐标。 |
| `@source.dx`、`@source.dy` | `number` | 移动 | 该已完成周期中的净位移，右、下为正。 |
| `@source.distance` | `number` | 移动 | 该已完成周期中的路径长度。 |
| `@source.wheel_x`、`@source.wheel_y` | `number` | 滚轮 | 该已完成周期中的带符号滚动量；另一轴为零。 |
| `@source.period` | `number` 或 `duration` | 所有周期来源 | 该已完成周期实际采用的周期长度。 |

字段只读，类型和可用性由被引用的事件源声明决定。例如键盘规则可以读取移动源的 `@travel.dx`，而读取滚轮源的 `@verticalScroll.dx` 属于字段类型错误。

### 8.6 可访问范围与类型约束

完成周期字段可以出现在映射、普通事件、PAUSE 和退出规则的条件，以及普通事件规则的完整动作流中。映射条件在来源 `down` 上求值时，按该次触发选取完成周期；已有映射仍遵循原有的锁存语义。

完成周期字段以规则触发上下文为前提。顶层初值、配置量和 `every` 周期表达式中的完成周期访问属于作用域错误。来源未声明、来源不是 `event`、字段不可用于该来源、向只读字段赋值，也都在编译时给出诊断。

`@travel` 用于选择视图，数值或状态表达式通过字段取得对应类型的值，例如 `@travel.dx * 2`、`-@travel.dy` 和 `@travel.valid == on`。

### 8.7 同一次输入完成多个周期

一次原始输入跨过多个 `travel` 周期边界时，每个 `travel:tick` 分别对应自己的完成快照。即使后面的周期也已经完成，前一个周期触发的任务仍读取前一个周期的 `@travel`。

对于其他来源，完成周期视图选取同一次原始输入更新完所有来源之后，该来源最近完成的周期。同一次原始输入触发的各条规则读取其他来源时，采用同一份完成记录选择；自身来源则始终优先使用实际触发它的那个周期。

例如一次输入使 `fine` 完成第 1、2、3 周期，并使 `coarse` 完成第 1 周期：三个 `fine:tick` 中的 `@fine` 分别指向第 1、2、3 周期，`@coarse` 都指向第 1 周期；`coarse:tick` 中的 `@coarse` 指向自己的第 1 周期，`@fine` 指向本次输入完成的第 3 周期。

## 9. 中间统计量与相位

### 9.1 实时字段

事件源名称后的字段表示正在进行的统计状态；名称前带 `@` 时，读取第 8 节定义的已完成周期视图。例如 `travel.dx` 读取 `travel` 当前周期的净位移，`@travel.dx` 读取为本次规则触发选定的完成周期的净位移。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `source.period` | `number` 或 `duration` | 当前周期固定使用的长度。 |
| `source.progress` | 与 `period` 一致 | 当前周期的进度；滚轮进度保留方向符号。 |
| `source.remaining` | 与 `period` 一致 | 达到下一个边界还差的非负量；滚轮为 `period - abs(progress)`。 |
| `source.start_x`、`source.start_y` | `number`，移动来源 | 正在累计的周期的起始坐标，在本周期内保持固定。 |
| `source.x`、`source.y` | `number` | 此来源最近一次纳入统计的坐标。 |
| `source.dx`、`source.dy` | `number`，移动来源 | 当前周期的净位移。 |
| `source.distance` | `number`，移动来源 | 当前周期的累计路径长度。 |
| `source.moving` | `state`，移动来源 | 当前是否处于有效持续移动段，取 `on` 或 `off`。 |
| `source.wheel_x`、`source.wheel_y` | `number`，滚轮来源 | 当前周期的带符号滚轮余量。 |

这些字段只读。一次条件求值或一次动作的参数求值使用一致的状态视图；任务经过等待后再次读取事件源字段，可以得到后续更新的状态。

```weave
number currentProgress = 0;
number capturedDx = 0;

event travel = Mouse:move every 40;

F1:down => set(currentProgress, travel.progress);

travel:tick
    ~> wait(100ms) set(capturedDx, @travel.dx);
```

`currentProgress` 保存 F1 动作求值时的实时进度，`capturedDx` 保存触发对应任务时的周期净位移。

### 9.2 多个周期

每个事件源分别记录相位。相同输入可以同时推进多个源，例如两个从同一起点开始的距离源：

| 累计距离 | `every 8` | `every 16` |
| --- | --- | --- |
| 8 | 完成第 1 周期 | 当前进度 8。 |
| 16 | 完成第 2 周期 | 完成第 1 周期。 |
| 24 | 完成第 3 周期 | 当前进度 8。 |

统计边界由输入和周期定义决定，动作开始、等待、完成的时刻不移动这些边界。某次输入的规则条件读取实时源字段时，各来源都已反映该次输入的更新；动作中的实时字段按动作求值时的状态读取。已完成周期视图按第 8.7 节选定并保持固定。

### 9.3 生命周期

| 情况 | 当前累计周期 | 最近完成记录与任务视图 |
| --- | --- | --- |
| `when` 为假 | 周期继续前进，本条规则不执行动作。 | 已完成的周期仍更新该源的最近完成记录。 |
| 一条订阅规则停止扫描 | 结束本次该事件源的订阅扫描，其他统计源的更新保持有效。 | 已形成的完成记录保持有效。 |
| 动作等待或任务交错执行 | 统计独立推进。 | 最近完成记录可以更新，各任务已选定的视图保持不变。 |
| 普通停止移动或滚动 | 距离和滚轮余量保留，持续移动时间周期结束并清零。 | 最近完成记录保留。 |
| 程序激活、重新加载、PAUSE 转换、目标资格丢失 | 清除相应周期进度，恢复后按新输入建立起点。 | 相应来源的最近完成记录清空，后续新触发中的 `valid` 为 `off`，直到产生新的完成周期；已有任务的存续遵循相应取消语义。 |
| 显式重启一个事件源 | 该源放弃已有进度，采用新的周期值和相位。 | 清空该源的最近完成记录；仍在执行的任务保持自己已经选定的视图。 |
| Debug 停止、开始新捕获、降低刷新频率 | 统计进度和相位继续保持。 | 最近完成记录与各任务的固定视图保持原有语义。 |

### 9.4 鼠标实时状态

`Mouse.*` 提供全局鼠标状态，独立于各个 `event` 声明、统计周期和任务。下表中的字段只读，可用于规则条件和动作表达式。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `Mouse.x`、`Mouse.y` | `number` | 当前指针在虚拟桌面像素空间中的位置，支持负坐标。 |
| `Mouse.dx`、`Mouse.dy` | `number` | 最近一次鼠标数值输入的横向、纵向位移，单位为像素，右、下为正。 |
| `Mouse.wheel_x`、`Mouse.wheel_y` | `number` | 同一次最近输入的水平、垂直滚轮量，单位为标准刻度，右、上为正。 |
| `Mouse.moving` | `state` | 当前是否处于有效物理移动形成的持续移动段，取 `on` 或 `off`。 |
| `Mouse.idle_time` | `duration` | 距上次有效物理移动经过的时间，随时间增长。 |

#### 9.4.1 最近输入量

`Mouse.dx`、`Mouse.dy`、`Mouse.wheel_x`、`Mouse.wheel_y` 共同描述最近一次物理移动、垂直滚轮或水平滚轮报告。每次这样的新报告整体替换这四个数值，与该报告无关的分量取零。

| 最近收到的报告 | `dx` | `dy` | `wheel_x` | `wheel_y` |
| --- | ---: | ---: | ---: | ---: |
| 向右 5、向上 2 的移动 | 5 | -2 | 0 | 0 |
| 向上滚动 0.25 刻度 | 0 | 0 | 0 | 0.25 |
| 向左滚动 0.5 刻度 | 0 | 0 | -0.5 | 0 |

这些量是单次报告的增量，读取它们保持原值；直到下一次鼠标数值报告到达才替换。经过静止时间也保留最近报告的数值，因此移动已经停止时，`Mouse.dx/dy` 仍可能保留非零值。是否正在移动由 `Mouse.moving` 表达，周期累计量由具名事件源表达。

键盘或鼠标按键事件保持这四个数值。程序激活时它们初始化为零；PAUSE 转换、目标资格变化、事件源重启和 Debug 捕获启停各自遵循原有语义，鼠标最近输入记录继续按实际收到的物理输入更新。

#### 9.4.2 移动状态与空闲时间

每次非零的有效物理移动使 `Mouse.moving` 为 `on`，并将 `Mouse.idle_time` 置为 `0ms`。随后 `Mouse.idle_time` 从该次移动起随时间增长；达到 `MOUSE_IDLE_TIMEOUT` 时，`Mouse.moving` 变为 `off`，`Mouse.idle_time` 继续增长。

例如配置为 `80ms` 时，上次有效移动之后经过 40ms，状态为 `moving=on`、`idle_time=40ms`；经过 80ms，状态为 `moving=off`、`idle_time=80ms`。空闲时间从最后一次有效移动计时，包含用于判定停止的这段间隔。

零位移、滚轮、键盘和鼠标按键报告保持上次有效移动时点。程序激活时 `Mouse.moving` 为 `off`、`Mouse.idle_time` 为 `0ms`；首次有效移动之前，空闲时间从本次激活开始增长，移动状态保持 `off`。

`Mouse.x/y` 反映当前实际指针位置，包括指针输出造成的位置变化。最近输入量、`Mouse.moving` 和 `Mouse.idle_time` 按物理输入更新，输出动作引起的位置变化本身不构成新的物理移动。

全局 `Mouse.moving` 观察物理移动，`travel.moving` 观察符合该事件源资格的移动；各个事件源的进度与相位重置遵循第 9.3 节。读取这些状态只进行查询，动作仍由相应规则的事件触发。

#### 9.4.3 条件与动作的读取时点

同一输入事件的全部规则条件读取一致的 `Mouse.*` 状态，其中包括该次鼠标数值报告的更新。每次动作参数求值读取当时最新的一致状态，同一次求值中的坐标对、增量组和动静状态各自对应一致的观察时点。

任务经过 `wait` 后再次读取 `Mouse.*`，得到恢复后求值时的最新值。即使是由 `Mouse:move` 触发的任务，也采用这一实时读取语义；原始事件的触发身份保持不变，字段读取得到的最近报告可以更新。

```weave
number sampledDx = 0;
duration sampledIdle = 0ms;

F1:down => wait(100ms) set(sampledDx, Mouse.dx);
F2:down when Mouse.moving == off ~> set(sampledIdle, Mouse.idle_time);
F3:down => move_to(Mouse.x + 20, Mouse.y);
```

F1 任务在等待完成后保存那时的最近输入位移。F2 条件判断触发时是否已经停止移动，动作随后保存动作求值时的空闲时间；期间若发生新的移动，保存的时间可以已被重置。

`Mouse.x/y` 表示此刻的指针位置，`travel.x/y` 表示该来源当前周期最近纳入统计的位置，`@travel.x/y` 表示所选已完成周期的结束位置。`Mouse.dx/dy` 是最近单次报告的位移，`travel.dx/dy` 是当前周期的净位移，`@travel.dx/dy` 是固定完成周期的净位移。三类读取各有明确的数据范围。

## 10. 指针输出动作

| 动作 | 参数 | 行为 |
| --- | --- | --- |
| `move_by(dx, dy)` | 两个 `number` | 在实际执行输出时，以指针当前位置为基准计算目标位置。 |
| `move_to(x, y)` | 两个 `number` | 以虚拟桌面绝对坐标指定目标位置。 |
| `scroll(amount)` | 一个 `number` | 按标准刻度滚动，正值向上、负值向下。 |
| `scroll_horizontal(amount)` | 一个 `number` | 按标准刻度滚动，正值向右、负值向左。 |

```weave
F1:down => move_by(40, -20);
F2:down => move_to(999999999, 999999999);
F3:down => scroll(0.25);
F4:down => scroll_horizontal(-1);
```

坐标采用虚拟桌面像素空间，支持多显示器中的负坐标。可表示的有限目标坐标超出桌面时遵循“尽可能执行”，移动到可到达的桌面边缘。`move_by` 的目标超出桌面时也遵循这一原则。第二条规则因此请求移动到右下方可到达的边缘。

参数表达式沿用现有 `number` 和 `duration` 的求值规则。指针动作不额外要求用户把坐标限制到 Windows 原生整数范围。

连续的小数输出量应保留累计效果。例如连续两次同向的 `0.5` 像素请求应能够累积为 1 像素移动，来自连续输入所触发的不同短任务时也保持这一效果。

这些动作发出一次性的指针输出，遵循动作流顺序。PAUSE、目标失效或程序停止会取消相关任务尚未执行的输出。

目标进程模式继续应用前台和指针目标资格检查：滚轮输出检查当前指针目标，移动输出检查起点和最终目标的资格；桌面边缘归一化与目标资格检查分别执行。`GLOBAL` 采用全局目标语义。Dry-run 保留匹配和输出计算，物理输入最终放行，输出在模拟边界完成。

## 11. Debug 展示

STATE 展示当前坐标，以及按 `event` 声明名称标识的各统计实例进度和最近完成记录。例如：

```text
[Mouse x=1842 y=517 dx=5 dy=-2 wheel_x=0 wheel_y=0 moving=on idle_time=4ms]
[travel progress=23/40 remaining=17 start=(1824,522) point=(1842,517) net=(18,-5)]
[@travel valid=on period=40 start=(1784,522) end=(1824,522) net=(40,0)]
[pulse progress=60/100ms moving=on]
[verticalScroll progress=+0.25/1 remaining=0.75]
```

第一行是当前指针位置、最近一次鼠标数值报告的增量，以及当前移动状态和空闲时间；这里最近报告是向右 5、向上 2 的移动，距该次有效移动经过了 4ms。`travel` 行显示当前周期从 `(1824, 522)` 开始，已经累计 23 像素路径，净位移为向右 18、向上 5；`@travel` 行显示此时最近完成的一个 40 像素周期，其起点为 `(1784, 522)`，终点为 `(1824, 522)`。`pulse` 行显示本次周期已由有效输入确认持续移动 60ms。`verticalScroll` 行显示尚未形成完整刻度的正向余量。

STATE 中的完成视图展示此时最新的记录。任务详情展示该任务触发时选定的记录，因此正在等待的任务可以持有比 STATE 更早的 `@travel`。尚无完成记录时显示 `valid=off`，完成周期的数据字段留空。

EVENTS 展示原始事件与已形成的周期事件，包含来源名称、触发数据和可关联的事件身份。ACTION EXECUTIONS 关联规则任务与触发事件，并展示该任务选定的各来源完成周期；跨来源读取时，可以区分“哪个事件触发了任务”和“任务读取了哪个来源的哪个完成周期”。同一条原始输入形成多个周期事件时，每个周期有独立的语义事件身份，并保留共同的原始输入关联。

STATE 展示最新的一致状态。显示刷新和 Debug 捕获的启停不改变累计、规则匹配和相位。重新开始捕获时，STATE 展示事件源此时的实际进度。

## 12. 语法增量 EBNF

下列产生式与 [现有语言定义](../../docs/grammar.md) 中的 `expression`、`condition`、`identifier`、`duration-literal`、`rule-arrow` 和 `action-flow` 共同使用。来源与周期类型的有效组合，以及字段可用性，由前文的语义规则确定。

```ebnf
mouse-idle-timeout-setting =
    "MOUSE_IDLE_TIMEOUT", "=", duration-literal, ";" ;

raw-mouse-event = "Mouse", ":", mouse-transition ;
mouse-transition = "move" | "wheel" | "horizontalwheel" ;

mouse-field-reference = "Mouse", ".", mouse-field-name ;
mouse-field-name =
    "x" | "y" | "dx" | "dy"
    | "wheel_x" | "wheel_y" | "moving" | "idle_time" ;

periodic-mouse-source = raw-mouse-event, "every", expression ;

event-declaration =
    "event", identifier, "=", periodic-mouse-source, ";" ;

named-periodic-event = identifier, ":", "tick" ;
observe-arrow = "~>" | "~>>" ;

raw-mouse-rule =
    raw-mouse-event, [ condition ],
    rule-arrow, action-flow, ";" ;

periodic-mouse-rule =
    named-periodic-event, [ condition ],
    observe-arrow, action-flow, ";" ;

completed-event-reference = "@", identifier ;
completed-field-reference =
    completed-event-reference, ".", completed-field-name ;
completed-field-name =
    "valid" | "start_x" | "start_y"
    | "x" | "y" | "dx" | "dy" | "distance"
    | "wheel_x" | "wheel_y" | "period" ;

event-field-reference = identifier, ".", event-field-name ;
event-field-name =
    "start_x" | "start_y"
    | "x" | "y" | "dx" | "dy" | "distance"
    | "wheel_x" | "wheel_y" | "period"
    | "progress" | "remaining" | "moving" ;

pointer-action =
    "move_by", "(", expression, ",", expression, ")"
    | "move_to", "(", expression, ",", expression, ")"
    | "scroll", "(", expression, ")"
    | "scroll_horizontal", "(", expression, ")" ;
```

顶层项目扩展加入 `mouse-idle-timeout-setting`、`event-declaration`、`raw-mouse-rule` 和 `periodic-mouse-rule`；动作项扩展加入 `pointer-action`；主表达式扩展加入 `mouse-field-reference`、具名事件源字段、`completed-field-reference` 和 `MOUSE_IDLE_TIMEOUT`。事件源字段中的 `identifier` 必须绑定到已经声明的 `event`，鼠标字段则引用内蕴的 `Mouse` 状态；字段类型与只读约束按第 9.4 节确定。

`@` 与来源名先构成完成周期引用，再通过点号选取字段；整个字段访问作为一个主表达式参与运算，例如 `-@travel.dx`、`@travel.dx * 2` 和 `@travel.dx > 0`。完成视图字段访问的作用域按第 8.6 节确定。

## 13. 待细化的语法与语义

下列条目保留为后续语言讨论项。

| 事项 | 需要明确的约定 |
| --- | --- |
| 周期表达式 | 可引用的值、首次输入前的 `period` 值、动态非正值与求值错误之后的来源状态及恢复行为。 |
| 起点初始化 | 首次有效移动前，以及统计重置后尚未建立新起点时，当前周期坐标字段的读取语义。 |
| 显式重启 | 动作写法、进度和相位重置的时刻，以及已有任务快照的保留关系。 |
| 事件身份 | 名称占用规则，以及 `event` 声明之后可进行的操作。 |
| 时间边界 | 首次位移归属、恰好达到空闲阈值的输入，以及同次跨越多个时间边界时各快照的数据归属。 |
| 多源同时触发 | 同次输入完成多个来源周期时，各来源规则的触发顺序。 |
| 坐标边界 | 多显示器间隙、桌面可达边缘、周期边界坐标及小数坐标的可观察结果。 |
| 连续输出 | 不同任务的小数输出累计、任务取消后的余量，以及 Dry-run 连续相对移动的结果。 |
| 目标资格 | 指针离开目标、前台变化和目标丢失时，输入资格与事件源相位的变化。 |
