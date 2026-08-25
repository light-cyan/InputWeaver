# Phase 10：TUI 部分提案

## 状态

本文件只记录当前输入调试界面想法，不是实现计划。TUI 必须等待 Phase 9 App 接口确定。

## 入口提案

用户在程序列表选择一个程序并进入 Debug。普通执行器数量、当前调试程序和 PID 分开显示；PID 不是选择依据。

## 页面提案

- `Programs`：选择程序并提交运行或调试命令。
- `Debug`：显示输入事件、当前按键、规则执行和运行异常。
- `Details`：显示所选记录的完整字段。

页面名称和导航方式尚未确定。

## Debug 区域提案

```text
EVENTS                              STATE
↓ A  PHY  DROP                     [A DOWN PHY DROP]
↓ B  ECHO PASS                     [A DOWN PHY DROP] [B DOWN ECHO PASS]
↑ B  ECHO PASS                     [A DOWN PHY DROP]

PHY physical candidate | ECHO current instance | EXT external | INIT sampled
PASS forwarded | DROP suppressed
```

事件区保存独立的按下和松开记录。状态区由 `DebugClient` 根据事件流生成，释放立即移出按下状态。

Each event row displays `DebugInputEvent::captureTimeNanoseconds` together with the control, transition, origin, disposition, `repeatedDown`, and `unmatchedUp` classification.

规则执行条目使用两行：第一行显示触发事件、命中规则和结束结果；第二行显示完整编译动作程序并定位当前步骤。`DebugClient` 同时保留最近三个步骤，供快速执行时展示轨迹。

The first rule-execution row displays `DebugRuleExecution::matchedTimeNanoseconds` together with the trigger event, matched event rule, condition program, and execution result.

The second rule-execution row displays the complete compiled action program, highlights `currentInstructionIndex`, and renders the three entries in `recentInstructionIndices` with distinct color intensities while preserving their oldest-to-newest order.

`INIT` 事件没有处理结果。`ECHO` 表示当前实例生成的输入再次被当前实例观察到。

## 视觉提案

- `PHY`、`ECHO`、`EXT`、`INIT` 使用不同文本标签和颜色。
- `PASS`、`DROP` 同时使用文本，不只依赖颜色。
- 新事件、重复和释放可以短暂闪动，但动画不改变真实状态。
- 窄窗口可以把事件区和状态区上下排列。

## 待设计问题

- App 与 TUI 的最终接口。
- 程序管理和普通进程管理放在哪个页面。
- 进入、离开和停止 Debug 的精确行为。
- 高频事件的过滤、暂停、分组和详情查看。
- 最小终端尺寸、ASCII 模式、无颜色模式和动画时间。
- TUI 自身按键是否会出现在调试事件中，以及如何向用户解释。

这些问题解决后再编写 Phase 10 `Plan.md`。
