核心在于复用结构,以化简!!!

组成:
事件监听器
规则表
解释器[你可以换一个名字,本文先这样]
任务执行器[你可以换一个名字,本文先这样]
变量常量池[你可以换一个名字,本文先这样]

规则表是纯数据结构,也是源码编译的结果

如果PASUE[on]则事件接收器向解释器发送信息
解释器收到信息后给任务执行器发送信息
任务执行器执行任务

规则表的结构为Dict[事件,List[Tuple[条件,任务]]],所有语句都抽象为Tuple[事件,条件,任务]三元组
注意,三元组和字典只是一个表达形式,实际上就是触发事件然后遍历条件,运行第一个匹配项目
即
```
rule=RulerTable[事件A]
for predicate,task in rule:
    if predicate:
        run(task)
        break
```
这样
我们需要代码内部表达条件表示,使用一个简单的函数方法访问变量常量池来给出T or F
这个模块我们后续讨论记录下来

我决定删除条件事件一致的重复声明,和变量的重复赋值
重叠条件不再产生歧义，而是前面的规则优先
变量也是最初的赋值优先,后面再写不会覆盖

任务是一个动作序列

动作是异步执行的,运行每步都检查PAUSE的状态,如果PAUSE[off]就break,取消当前任务,并注意gap()和awit()的唤醒,以及是否需要防止卡键?

特殊内蕴变量不处于变量常量池,而是直接对应解释器实际内存,不是模拟池

使用一些内蕴的元素(类型,动作,变量,事件)来辅助全部功能的实现,这些在编译器和语法上不开放

`,`编译为动作gap()
连续的`,`不视作语法错误

```
A:down => press(B)
```
编译为
```
(A:down, null, [press(B)])
```

```
A := B when m[on]
```
编译为全套的
```
(A:down, m[on], [press(B)])
...
```
而不是一种独立的语法结构
但是这样带来的卡键问题是谁的责任?加内部状态可以解决吗?


```
A:down when (LCtrl[held] or RCtrl[held]) and combat[on] =>
    tap(A),
    if count > 1 then
        repeat count times do
            tap(B),
            set(fireGap, fireGap - 10ms)
        end
    else
        tap(C)
    end,
    tap(D);
```
编译为
```
1, (A:down, (LCtrl[held] or RCtrl[held]) and combat[on], [tap(A), gap(), emit(if_1_2)])
```
`emit`是内蕴的动作
功能是内部触发事件
不对用户开放,因为很容易写出死循环
因此,事件分为来自键盘鼠标的实际事件,还有程序内部主动发送的事件
同一个宏的分支循环结构依托事件的跳转和状态鉴定做判断,顺序执行则是内化在任务是序列中
以降低复杂性

if_1_2则是我选择的命名方案,其中1和2是我占位的源码中的行号和第几个字符数,用于唯一标注和表意

所以实际上,将if,repeat,while视作动作的实现方法是抽象为emit事件,然后在新事件的元组中继续消费剩下的动作

```
2, (if_1_2, count > 1, [emit(repeat_3_4_s)])
3, (repeat_3_4_s, null, [set(repeat_3_4_i, 0), set(repeat_3_4_t, floor), emit(repeat_3_4_c)]) // 初始化,准备进入循环
4, (repeat_3_4_c, repeat_3_4_i < floor, [set(repeat_3_4_i, repeat_3_4_i+1), tap(B), gap(), set(fireGap, fireGap - 10ms), emit(repeat_3_4_c)]) // 循环
5, (repeat_3_4_c, not (repeat_3_4_i < floor), [gap(), tap(D)]) // 结束循环
6, (if_1_2, not(count > 1), [tap(C), gap(), tap(D)])
```
repeat_3_4_s意思为repeat_3_4 start
repeat_3_4_i是repeat_3_4_s对应的迭代变量
repeat_3_4_c意思为repeat_3_4 continue

取消原来repeat循环体times向下取整的设计

```
Mouse.Middle:down =>
    while Mouse.Middle[held] do
        if combat[on] then
            tap(Mouse.Left),
            wait(fireGap)
        end,
        set(count, count + 1)
    end,
    tap(Enter);
```
编译为:
```
(Mouse.Middle:down, null, [emit(while_5_6_c)])
(while_5_6_c, Mouse.Middle[held], [emit(if_7_8)])
(while_5_6_c, not Mouse.Middle[held], [gap(), tap(Enter)])
(if_7_8, combat[on], [tap(Mouse.Left), wait(fireGap), gap(), set(count, count + 1), emit(while_5_6_c)])
(if_7_8, not combat[on], [gap(), set(count, count + 1), emit(while_5_6_c)])
```
while循环比repeat循环更好写一点,少一个start

```
F12:down when LCtrl[held] and LShift[held] ~>
    press(LAlt),
    wait(50ms),
    set(PAUSE, off),
    release(LAlt);
```
编译为:
```
(*F12:down, LCtrl[held] and LShift[held], [press(LAlt), gap(), wait(50ms), gap(), set(PAUSE, off), gap(), release(LAlt)])
```
*标注含义是在 事件监听器 中不会消费原始物理事件

```
state combat = off;
number count = 5.8;
duration longWait = 1min;
```
就是定义和初始化赋值变量
```
TARGET = "game.exe";
TAP_DURATION = 30ms;
ACTION_GAP = 10ms;
```
这样的量则仅仅是赋值


认为语法正确的
```
A:up => tap(B);
```
是可以的,不做语义检查,允许一些奇怪句子出现
不禁止消费型 `:up =>`
当然,我不是很确定,如果同时写了对应的`:down =>`能否可以通过
关键在于,物理按键处于按下状态时能否发送弹起?
需要区分好责任的边界,写错误的代码不是代码的责任


目前这套方案在遇到并行问题时有严重的问题,如果是拆解if while repeat,会出现异常的情况
反复重复唤起的结果是刷新计数,目标数字也会被动态修改
...... 一系列异常情况
但是我又不想增加额外的复杂度
比如说真的在动作序列中
