# 动作与流程控制

[文档首页](README.md) · [上一篇：输入映射与规则](rules.md) · [下一篇：鼠标与计量器](mouse.md)

规则箭头后面是一串动作。动作按书写顺序执行，用来按键、等待、修改变量、移动鼠标或启动程序。

## 依次执行动作

```weave
TARGET = "notepad.exe";

F6:down => tap(H) wait(200ms) tap(I);
```

按 F6 后先按一下 H，松开 H 后等待 200 毫秒，再按一下 I。相邻动作可以写在一行，也可以换行；动作之间的实际间隔由 `tap`、`wait`、`gap()` 和 `|` 决定。

## 按下、松开和点击

| 动作 | 效果 |
| --- | --- |
| `press(A)` | 按住输出 A，由当前动作任务负责释放 |
| `release(A)` | 释放当前任务按住的 A |
| `tap(A)` | 按下 A，保持 `TAP_DURATION`，然后松开 |

下面的程序在一次任务中完成 Ctrl+C：

```weave
F6:down =>
    press(LCtrl)
    tap(C)
    release(LCtrl);
```

`press` 和 `release` 应放在同一条规则的动作中。每次规则触发都会建立自己的任务，另一条规则里的 `release` 不能释放前一个任务持有的键。释放当前任务未持有的控制会报告错误并结束当前任务，后续动作不再执行。需要让来源键和目标键一起按住、一起松开时，直接使用[完整映射](rules.md)，例如 `A -> B;`。

任务完成、取消或出错时，会释放自己仍然按住的输出。多个任务或映射同时持有同一个输出键时，最后一个持有者释放后，这个键才真正松开。因此多个重叠的 `tap` 可能表现为一次较长的按住。

这些动作也接受鼠标按钮，例如 `tap(Mouse.Left)`。

## 等待和默认间隔

```weave
TAP_DURATION = 30ms;
ACTION_GAP = 100ms;

F6:down => tap(A) | tap(B) gap() tap(C);
```

`|` 与 `gap()` 等价，这里分别在 A、B 松开后等待 100 毫秒。`wait` 可以使用时间变量或表达式：

```weave
duration delay = 80ms;

F6:down => tap(A) wait(delay * 2) tap(B);
```

等待可以被暂停、目标切换或停止打断。`0ms` 会立即继续；需要周期性执行时，在循环中加入一个正的等待时间。

## 修改变量和数组

| 动作 | 用途 |
| --- | --- |
| `set(count, count + 1)` | 修改同类型的标量变量 |
| `set(values[index], 10)` | 修改已有数组元素 |
| `toggle(enabled)` | 在 `on` 和 `off` 之间切换 |
| `toggle(gates[index])` | 切换 `state` 数组中的已有元素 |
| `append(values, 10)` | 向数组末尾追加一个同类型元素 |
| `pop(values, last)` | 取走末项，写入同类型的标量变量 |
| `clear(values)` | 清空数组 |

```weave
number count = 0;
number[] history = [];
number last = 0;

F6:down =>
    set(count, count + 1)
    append(history, count);

F7:down =>
    if history.length > 0 then
        pop(history, last)
    end;
```

数组索引需要位于当前长度内，`pop` 时数组需要有元素。每个修改动作单独完成自己的读写；其他任务可以在等待或循环让出执行机会之后改变共享变量。

## 按条件选择动作

`if` 在动作执行到这里时判断条件，满足条件执行 `then` 后面的部分，否则执行 `else` 后面的部分。最后用 `end` 结束这个选择。

```weave
number count = 0;

F6:down =>
    set(count, count + 1)
    if count >= 3 then
        tap(C)
        set(count, 0)
    else
        tap(B)
    end;
```

这个程序每按三次 F6，依次输出 B、B、C。这里的 `if` 能读到前一个 `set` 刚写入的值。只需要满足条件时执行动作，可以省略 `else`。

## 重复固定次数

```weave
F6:down =>
    repeat 3 do
        tap(B)
        wait(100ms)
    end;
```

`repeat` 在进入循环时读取一次次数。正的小数向下取整，例如 `3.8` 执行三次；零和负数执行零次。循环次数也可以来自 `number` 变量或表达式。

## 满足条件时持续执行

下面的程序在按住 F6 时重复点击鼠标左键：

```weave
F6:down =>
    while F6 == held do
        tap(Mouse.Left)
        wait(100ms)
    end;
```

`while` 在每一轮开始时重新判断条件。松开 F6 后，当前这一轮可以先完成，下一轮检查时退出循环；暂停、目标失效或停止会直接取消当前任务。

`if`、`repeat` 和 `while` 都可以嵌套，也可以和普通动作交替书写。内层结构使用自己的 `end`，整条事件规则最后用一个分号收尾。

## 多次触发和并行任务

一条规则上一次的动作还在等待时，再次触发会创建新任务。这些任务共享变量，却分别保存自己的等待、循环进度和按住的输出。

如果一个宏完成前只应运行一次，可以用状态变量加门控：

```weave
state busy = off;

F6:down when busy == off =>
    set(busy, on)
    repeat 3 do tap(B) wait(100ms) end
    set(busy, off);

F7:down => set(busy, off);
```

这里的 `busy` 是用户变量。任务被取消时，尚未执行的 `set(busy, off)` 会一起取消，所以例子提供 F7 手动复位。特别密集的输入也可能在前一个任务写入 `busy` 前完成匹配；需要了解严格执行顺序时，参阅[运行行为与限制](running.md)。

## 启动外部程序

```weave
F6:down => exec("notepad.exe");
```

使用 `exec` 的程序在每次运行时需要授权：界面中打开 NEXT RUN 的 `P` 选项，或在命令行加入 `--allow-exec`。

命令可以包含参数和带空格的路径，例如 `exec("\"C:\\Tools\\My Helper.exe\" --mode quick")`。字符串的转义方式见[语言基础](language.md)。

进程启动成功后继续后面的动作，已启动的外部程序有自己的生命周期。Windows 执行器直接解析并启动可执行文件；使用命令解释器的管道、重定向等功能时，应明确启动相应解释器。可执行文件的查找顺序、相对路径和子进程工作目录见 [Windows 执行器](windows.md#外部进程的查找和工作目录)。

在模拟运行中，`exec` 仍需要授权，但只模拟启动成功。[命令行选项](command-line.md)

## 鼠标动作

`move_by(dx, dy)` 相对移动指针，`move_to(x, y)` 移动到屏幕坐标，`scroll(amount)` 和 `scroll_horizontal(amount)` 滚动，`restart(name)` 重置计量器。这些动作的坐标、单位和例子见[鼠标与计量器](mouse.md)。
