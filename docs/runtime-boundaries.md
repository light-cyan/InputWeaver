# 运行边界参考

本文记录当前 Windows 执行器对目标选择、程序激活、事件分派、任务调度、输出和诊断日志实施的固定边界。这里的数值是当前可执行文件内置容量，不是 `.weave` 配置项；执行器会在安装输入钩子前拒绝已知需求超过容量的编译程序，并在运行中以放行输入、取消违规工作和释放已持有输出的方式处理瞬时资源不足。

## 目标与排除选择

编译程序中的 `TARGET` 是默认目标。`--target <exe-name-or-absolute-path>` 把本次运行覆盖为目标进程模式，`--target-global` 把本次运行覆盖为全局模式，两个命令行覆盖选项互斥。编译程序与命令行都没有提供可用目标时，执行器在安装输入钩子前报错并退出。

`--target` 与 `--exclude` 使用同一个 Windows 进程定位器。可执行文件基本名选择器按基本名匹配；绝对路径选择器在规范化后按完整映像路径匹配。两类比较都使用 Windows 序号、不区分大小写的比较方式。只有一个匹配实例时直接选中；多个匹配实例中位于前台的实例优先。

可执行目标模式先创建并激活程序运行时、启动任务和输出线程并安装低级输入钩子，再定位匹配进程。首次启动尚未找到 Target 与 Target 退出后的等待使用同一个未绑定状态；Exclude 在前台窗口变化时重新定位。排除选择命中前台进程时，执行器先应用排除边界，再判断 Target 资格。

Target 未绑定时，低级钩子继续监听，Debug 继续捕获，物理退出规则继续求值，PAUSE 规则、普通映射和普通规则不分派，物理输入直接放行。Target 退出时，执行器取消目标相关任务和映射并释放输出，然后清空绑定并继续定位；后续匹配进程绑定到同一个运行时，用户变量、数组和 PAUSE 状态继续保留。

排除选择命中前台进程时，运行时路由拒绝输入分派和新的输出，低级钩子最终放行物理输入；Windows 注入边界会再次拒绝新的 `down` 或 `again` 输出。安全清理产生的释放不受排除限制。这个边界独立于目标类型，因此 Global 运行也可以排除一个前台进程。托盘宿主为它启动的每个执行器自动传入 `InputWeaverTUI.exe`。

## 激活容量

| 程序需求 | 固定容量 |
| --- | ---: |
| 激活控制数 | 4096 |
| `state` 槽数 | 4096 |
| `number` 槽数 | 4096 |
| `duration` 槽数 | 4096 |
| 数组数 | 4096 |
| 数组已分配页和页目录总量 | 64 MiB |
| 映射槽数 | 4096 |
| 单个事件的退出规则数 | 64 |
| 单个事件的 PAUSE 规则数 | 64 |
| 单个事件的普通规则数 | 256 |
| 单个事件的条件求值指令步数 | 4096 |
| 单个事件的映射操作数 | 64 |
| 表达式栈深度 | 1024 |
| 单个任务的 `repeat` 栈帧数 | 256 |
| 单个任务可持有的输出控制数 | 256 |
| 同时存在的任务槽数 | 256 |
| 事件事务队列项数 | 1024 |
| 运行时内部诊断记录数 | 256 |

The mouse accumulator stores at most 1024 completed cycles for one normalized input report. Each active source and task owns its cycle state and completion selections. Exceeding the per-report cycle capacity raises `TransactionCapacity` and requests fatal cancellation before that report can commit rule work; the physical report is forwarded. Mouse observation and pointer output are checked against the runtime ports during activation.

编译产物保存由程序结构精确推导出的需求。激活错误输出中的 `code`、`subject`、`required` 和 `available` 分别表示失败类别、具体限制维度、该维度的需求和该维度的执行器容量；无效的调度器配置与无效的输出速率配置使用独立错误类别。激活失败不会安装输入钩子，也不会开始规则分派。

任务槽和事务队列也限制运行时瞬时占用。一个事件无法完整预留它需要的任务槽或队列空间时，整个事件事务不会部分提交，本次物理输入直接放行，并产生 `TransactionCapacity` 诊断。

## 任务与输出预算

| 运行预算 | 固定边界 |
| --- | ---: |
| 单个任务在有效挂起前执行的动作指令数 | 65536 |
| 单个任务在有效挂起前发布的普通输出数 | 4096 |
| 持续就绪调度量子数 | 16 |
| 达到持续就绪阈值后的退避时间 | 1 毫秒 |
| 普通输出速率 | 每 1 秒 2048 个 `down` 或 `again` 转换 |
| Windows 待注入输出队列 | 256 项 |

动作任务在一个调度片段中连续运行到完成、取消、正时长挂起或循环让步，片段执行期间不调度其他任务。求值为 `0ms` 的 `wait`、配置为 `0ms` 的 `gap()`、`|` 和 `tap` 不建立定时状态、不结束当前片段，也不重置任务预算；零时长 `tap` 在当前片段中连续完成按下和释放。

任务指令或输出预算耗尽时，执行器取消当前违规任务并释放该任务持有的输出。一次真正等待到未来单调时钟时刻的正时长挂起会开始新的预算区间；`yield` 和循环回边会让出调度器，但不会重置预算。

持续就绪任务达到 16 个调度量子后，工作线程最多退避 1 毫秒。新输入、目标资格变化、取消、关闭和退出规则会提前唤醒工作线程，控制台的 `scheduler_backoffs` 记录实际发生的退避次数。

普通输出速率只统计 `down` 和 `again` 转换。达到速率边界时，产生超额输出的任务或映射被取消并清理所有权；输出释放不占用普通输出额度，因此速率耗尽不会阻止必要的键抬起。

Pointer requests also count toward the shared ordinary-output rate and the producing task's output budget. They use the same sequence and cancellation generation as control outputs, with a pointer-operation payload instead of held-control ownership.

## 无注入模拟

`--dry-run` 保留程序激活、输入状态、规则匹配、任务调度、变量修改、PAUSE、退出规则、调试事件和诊断路径，但最终始终放行物理输入，不调用 `SendInput`。运行时仍计算原本的抑制决定，因此 debug `DROP`、钩子诊断中的 `suppressed` 和运行指标中的 `suppressed` 表示正常模式下将被抑制，而不是系统输入实际被阻止。

动作输出仍经过运行时所有权、代次、目标和容量检查；到达 Windows 注入边界后作为成功模拟完成。`exec` 仍要求 `--allow-exec` 通过激活权限检查，但不会解析可执行文件或调用 `CreateProcess`，并作为成功动作继续后续指令。

## 输入线程状态一致性

低级输入钩子在退出规则、PAUSE 规则和普通规则求值前等待取得所需的共享状态锁，再针对同一个 PAUSE、用户变量和数组快照完成本次事件分派。后台任务正在更新共享状态时，物理事件等待该内存临界区结束，不会仅因锁竞争而被直接放行。所有路径先取得 PAUSE 锁、再取得变量和数组锁；后台任务在发布 Debug 状态变化前释放这些锁。编译程序没有 PAUSE 规则时，运行时不执行 PAUSE 控制表查询或取得对应锁，内蕴 `PAUSE` 保持为 `on`。

任务唤醒是非阻塞通知。输入分派、目标资格变化、取消和关闭只发布唤醒状态，不等待任务线程完成；任务线程会消费已经累计的工作和状态变化。

## 取消代次与输出所有权

运行程序使用单调递增的取消代次标记任务、映射和待发布输出。PAUSE 状态变化、目标丢失、重新加载、退出规则、关闭和致命运行故障会推进代次并使旧工作失效。

旧代次的 `down` 和 `again` 输出在进入输出队列前及实际注入前都会被拒绝，取消清理产生的释放仍可通过。这个规则保证目标失去资格或任务取消后不会恢复旧动作，同时允许执行器释放自己已经按下的控制。

目标限定运行在前台资格丢失后取消任务、清除映射并释放输出。目标恢复时，在丢失期间持续按住的来源必须先出现物理释放，之后的新按下才能建立新的映射生命周期。

## 诊断日志容量

| 日志层 | 固定容量 |
| --- | ---: |
| 钩子记录环 | 4096 条 |
| 输出注入记录环 | 512 条 |
| Windows 会话运行时记录环 | 512 条 |
| `ProgramRuntime` 内部诊断队列 | 256 条 |
| 单个 JSONL 文件 | 8 MiB |

`--log` 启动时覆盖指定 JSONL 文件。日志生产者不会等待磁盘写入；记录环满时丢弃新记录并增加相应丢弃计数。文件达到 8 MiB 后停止追加完整记录并设置 `jsonl_truncated=true`，规则执行和编译程序中的退出规则仍继续工作。

钩子记录中的 `duration_us` 是单次低级钩子处理耗时，控制台中的 `max_hook_us` 是本次会话观察到的最大值。这两个字段是测量值，不是触发取消的时间阈值；是否满足目标机器的响应要求需要结合保留日志进行复核。

## Debug 捕获容量

Debug 是运行时旁路观测通道，不改变规则分派和动作执行的容量。当前固定边界如下：

带 Debug 会话令牌启动时，程序 Session 在等待 Target 之前建立 Debug pipe、激活运行时并安装输入钩子。捕获在钩子就绪后建立可信快照，不依赖 Target 是否已经绑定；Target 出现、退出和重新绑定不会重建 Debug pipe、运行时或 Debug 捕获状态。

| Debug 边界 | 固定容量 |
| --- | ---: |
| 捕获开始时的按下控制快照 | 256 项 |
| 执行器 Debug 生产队列 | 4096 条 |
| DebugClient 最近输入事件 | 512 条 |
| DebugClient 当前按下控制 | 256 项 |
| DebugClient 已显示执行记录 | 256 条 |
| DebugClient 等待触发输入的执行记录 | 256 条 |
| DebugClient 运行时问题 | 128 条 |
| 数组快照数 | 4096 个 |
| 单个数组快照 | 精确长度及最多 8 个元素 |
| 单个协议帧载荷 | 16 MiB |
| 单段条件或动作源码文本 | 16 MiB |

捕获开始消息携带 `PAUSE`、全部用户 `state`、`number`、`duration` 值以及全部数组的有界快照；短数组携带全部元素，长数组携带前四项、后四项和精确长度，之后只发送实际变化。生产队列溢出时，执行器停止当前捕获并报告 `DebugStreamOverflow`，DebugClient 把派生状态标记为不可信并请求新的捕获代次；规则执行本身继续运行。

## 控制台统计

| 字段 | 含义 |
| --- | --- |
| `dispatched` | 进入运行时分派的输入事件数 |
| `suppressed` | 被运行时消费并阻止继续传递的输入事件数 |
| `tasks_started` | 已启动的动作任务数 |
| `tasks_completed` | 正常完成的动作任务数 |
| `hook_log_drops` | 钩子记录环丢弃数 |
| `injection_log_drops` | 输出注入记录环丢弃数 |
| `runtime_log_drops` | Windows 会话运行时记录环丢弃数 |
| `runtime_diagnostic_drops` | `ProgramRuntime` 内部诊断队列丢弃数 |
| `jsonl_bytes` | 已完整写入 JSONL 的字节数 |
| `jsonl_truncated` | JSONL 是否达到文件容量边界 |
| `max_hook_us` | 会话内最大低级钩子处理耗时 |
| `scheduler_backoffs` | 持续就绪任务触发调度退避的次数 |
| `queued_outputs` | 已进入 Windows 待注入队列的输出数 |
| `cancelled_outputs` | 注入前因代次、目标、关闭或熔断而取消的输出数 |
| `injection_failures` | Windows 输入注入失败次数 |
| `outside_target_forwarded` | 因目标资格不满足而直接放行的输入数 |
| `tasks_cancelled` | 会话内取消的任务数 |
| `transaction_rejections` | 因任务槽或事务队列不足而整体放行的事件事务数 |
| `output_transitions` | 运行时发布的输出转换数 |
| `current_array_bytes` | 会话停止时数组页和页目录占用的字节数 |
| `peak_array_bytes` | 会话内数组页和页目录占用的峰值字节数 |
| `rejected_array_growth` | 因容量或分配失败而拒绝的数组增长次数 |

会话停止行中的 `runtime_diagnostic_drops=0` 表示运行时内部诊断队列没有丢弃记录。该行可能在日志工作线程完全排空前输出；外层日志完整性应以随后出现的 `Diagnostic log stopped.` 行为准。最终行中的三类丢弃计数均为 `0` 且 `jsonl_truncated=false`，表示当前进程已经刷新并关闭的日志没有触发外层记录环丢弃或文件截断。
