<a id="section-mouse-and-meters"></a>

# 鼠标与计量器

[English](../en/mouse.md)

[文档首页](README.md) · [上一篇：动作与流程控制](actions.md)

鼠标按钮使用和键盘相同的规则。鼠标移动和滚轮另外提供位置、位移、滚动量以及计量器，可以用来编写按距离或连续移动时长触发的动作。

<a id="section-mouse-buttons"></a>

## 鼠标按钮

```weave
Mouse.X1 -> LCtrl;
Mouse.X2:down => tap(Mouse.Left);
```

第一条把侧键 X1 映射成左 Ctrl，第二条在按下侧键 X2 时点击左键。按钮名称为 `Mouse.Left`、`Mouse.Right`、`Mouse.Middle`、`Mouse.X1` 和 `Mouse.X2`。每次物理按钮按下报告触发 `down`，释放报告触发 `up`；键盘在已经按住时再次收到按下报告才产生 `again`。[完整规则说明](rules.md)

<a id="section-moving-the-pointer-and-scrolling"></a>

## 移动指针和滚动

```weave
F6:down => move_by(100, 0);
F7:down => move_to(500, 300);
F8:down => scroll(1);
F9:down => scroll_horizontal(-1);
```

| 动作 | 单位与方向 |
| --- | --- |
| `move_by(dx, dy)` | 从动作实际输出时的指针位置相对移动；像素为单位，右和下为正 |
| `move_to(x, y)` | 移到 Windows 虚拟桌面的绝对像素坐标 |
| `scroll(amount)` | 垂直滚动；一格滚轮为 1，向上为正 |
| `scroll_horizontal(amount)` | 水平滚动；一格滚轮为 1，向右为正 |

参数可以是数字表达式。以下坐标落点与输出资格说明适用于 Windows 执行器。主显示器左侧或上方的显示器可以有负坐标。落在桌面外、显示器间隙或光标限制区域外的目标，会调整到可以到达的显示器像素；绝对坐标按最近的像素取整。[Windows 输入与输出](windows.md)

相对移动的小数像素会累计，例如连续两次 `move_by(0.5, 0)` 可以积成一个像素。小数滚动量也会累计，垂直和水平两个方向分别计算。暂停、目标失效等取消操作会清理累计余量；绝对移动会清理相对移动的小数余量。

应用限定运行还会检查输出位置是否属于目标：移动检查起点和终点，滚轮检查输出时的指针位置。实际输入结果也会受到目标应用自身的滚动和鼠标处理方式影响。

<a id="section-responding-to-physical-movement-and-scrolling"></a>

## 响应物理移动和滚轮

```weave
number last_dx = 0;
number last_wheel = 0;

Mouse:move ~> set(last_dx, Mouse.dx);
Mouse:wheel ~> set(last_wheel, Mouse.wheel_y);
```

| 事件 | 何时触发 |
| --- | --- |
| `Mouse:move` | 收到物理鼠标移动报告 |
| `Mouse:wheel` | 收到物理垂直滚轮报告 |
| `Mouse:horizontalwheel` | 收到物理水平滚轮报告 |

这些事件支持 `=>`、`=>>`、`~>`、`~>>` 四种箭头。选用接管型箭头时，对应的原始鼠标报告由程序处理；选用放行型箭头时，应用也会收到原始报告。

<a id="section-reading-current-mouse-state"></a>

## 读取当前鼠标状态

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `Mouse.x`、`Mouse.y` | `number` | 当前观察到的屏幕像素坐标 |
| `Mouse.dx`、`Mouse.dy` | `number` | 最近一次数值鼠标报告中的物理位移 |
| `Mouse.wheel_x`、`Mouse.wheel_y` | `number` | 最近一次数值鼠标报告中的水平、垂直滚动量，以格为单位 |
| `Mouse.moving` | `state` | 最近是否持续发生物理移动 |
| `Mouse.idle_time` | `duration` | 距上一次物理移动的时间 |

每次移动或滚轮报告会一起更新位移和滚轮字段，并把与这次报告无关的分量设为零。键盘和鼠标按钮事件保留这些字段。

`MOUSE_IDLE_TIMEOUT` 默认为 `80ms`。达到这段时间仍没有物理移动时，`Mouse.moving` 变成 `off`，`Mouse.idle_time` 继续增长。可以在程序顶层设置为其他正时间值，例如 `MOUSE_IDLE_TIMEOUT = 120ms;`。

<a id="section-acting-after-each-distance-interval"></a>

## 每移动一段距离执行一次

```weave
meter path = Mouse:move every 24;
number steps = 0;

path:tick ~> set(steps, steps + 1);
```

`meter` 声明一个计量器。这里的 `path` 累计物理鼠标走过的路径，每满 24 像素产生一次 `path:tick`，`steps` 随之加一。

距离是每段路程长度的累加。向右 12 像素再向左 12 像素，也会累计到 24 像素；普通停顿会保留尚未满一段的余量。一次较大的移动可以跨过多段，并依次产生多次 tick。

计量器的 tick 使用 `~>` 或 `~>>`。它是统计完成事件；是否接管原始移动，由 `Mouse:move` 规则的箭头决定。

一次符合条件的物理鼠标报告先更新所有计量器，再匹配原始鼠标事件规则，随后按计量器声明顺序、各周期完成顺序匹配 tick 规则。完成这些匹配后，命中规则的动作才开始执行；任务之间仍可能因等待或循环而穿插执行。

<a id="section-measuring-continuous-movement-time"></a>

## 按连续移动时长计量

```weave
MOUSE_IDLE_TIMEOUT = 80ms;
meter pulse = Mouse:move every 100ms;
number pulses = 0;

pulse:tick ~> set(pulses, pulses + 1);
```

时间计量器在第一次有效移动时开始计时。后续移动之间的间隔小于空闲阈值时，属于同一段连续移动；后续报告会结算期间跨过的周期。鼠标静止时不会自行产生 tick。

停顿达到 `MOUSE_IDLE_TIMEOUT` 后，尚未完成的时间周期清空，下一次移动开启新的一段。因此它适合统计持续移动，普通的定时重复动作则使用 `repeat` 或 `while` 配合 `wait`。

<a id="section-measuring-scroll-amounts"></a>

## 按滚动量计量

```weave
meter vertical = Mouse:wheel every 1;
meter horizontal = Mouse:horizontalwheel every 0.25;
number wheel_steps = 0;

vertical:tick ~> set(wheel_steps, wheel_steps + 1);
horizontal:tick ~> tap(ArrowRight);
```

滚轮计量器的周期是正数字，以滚轮格数为单位。进度保留方向：先向上滚动半格，再向下滚动半格，会互相抵消。达到任一方向的完整周期时产生 tick，可以通过已完成周期的滚动量判断方向。

<a id="section-current-and-completed-intervals"></a>

## 当前周期与已完成周期

`path.field` 读取正在累计的当前周期，`@path.field` 读取这次规则选中的已完成周期。例如：

```weave
meter path = Mouse:move every 24;

path:tick ~> wait(100ms) move_to(@path.start_x, @path.start_y);
```

这条动作等待 100 毫秒后，把指针移回触发这次 tick 的那段路径起点。等待期间即使鼠标继续移动，`@path` 仍然指向这次任务最初选中的完成记录。

在 `path:tick` 中，`@path` 是触发它的那一段；在普通键盘或鼠标规则中，`@path` 是该事件匹配时最近完成的一段。其他计量器的 `@name` 也在匹配时选定。当前字段 `path.field` 和 `Mouse.field` 则在动作执行到表达式时读取当前状态。

还没有完成记录时，`@path.valid` 为 `off`。读取其他已完成字段前，先作保护：

```weave
meter path = Mouse:move every 24;

F6:down when @path.valid == on => move_to(@path.start_x, @path.start_y);
```

<a id="section-field-reference"></a>

### 字段速查

| 计量器视图 | 可读取的字段 |
| --- | --- |
| 当前移动周期 | `start_x`、`start_y`、`x`、`y`、`dx`、`dy`、`distance`、`moving` |
| 已完成移动周期 | `start_x`、`start_y`、`x`、`y`、`dx`、`dy`、`distance`、`valid` |
| 当前滚轮周期 | `x`、`y`、`wheel_x`、`wheel_y` |
| 已完成滚轮周期 | `x`、`y`、`wheel_x`、`wheel_y`、`valid` |

所有当前周期还有 `period`、`progress` 和 `remaining`，分别表示本周期阈值、已完成进度和剩余量；已完成周期有 `period`。距离和滚轮计量器的这些量是 `number`，时间计量器的是 `duration`。`moving`、`valid` 是 `state`，坐标、位移和距离是 `number`。

移动周期中的 `distance` 是路程，`dx`、`dy` 是累计物理位移，起点和终点是对应的屏幕坐标。接管移动或输出动作改变指针位置时，坐标差可能与累计物理位移不同。

<a id="section-changing-interval-length"></a>

## 改变周期长度

```weave
number stride = 24;
meter path = Mouse:move every stride;

F6:down => set(stride, 48);
F7:down => set(stride, 12);
```

周期表达式可以读取已经声明的变量、数组和算术表达式。每个周期开始时取一次值，本周期继续使用这个值。一个周期完成时会立即锁定下一周期的阈值，即使余量为零。因此在 tick 动作中修改 `stride` 时，紧接着的周期已经开启，新值要等后续周期再次取值时才生效。

周期必须大于零。动态表达式出错时会报告计量器问题，清理未完成进度，在下一次符合条件的输入到来时重新尝试；最近一次完成记录保留。

<a id="section-resetting-a-meter"></a>

## 重置计量器

```weave
meter path = Mouse:move every 24;

F6:down => restart(path);
```

`restart(path)` 清理这个计量器的当前周期和最近完成记录。已经创建的任务仍保留各自选中的 `@path`。

程序重新启动、暂停切换或失去目标资格时，计量器统计也会清理。Debug 捕获的开始和停止只改变观察视图，运行中的统计继续保持。

<a id="section-observing-meters-in-debug"></a>

## 在 Debug 中观察

METERS 区域显示当前进度、周期长度和坐标，下面的 `@` 行显示最近完成的一段。EVENTS 显示 tick 及其计数，连续的同名 tick 可以合并显示；ACTION EXECUTIONS 保留各次动作的触发关系。[调试与排错](debugging.md)
