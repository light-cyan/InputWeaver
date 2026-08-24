# Notepad 手工验收

这个示例把 `notepad-showcase.weave` 绑定到 Windows 记事本 `notepad.exe`，覆盖完整映射、逐事件规则、条件、继续匹配、用户状态、数值与时间运算、`if`、`repeat`、`while`、`press`、`release`、`tap`、`wait`、动作间隔、`PAUSE`、原始 Windows 控制编号、鼠标来源和 `exec`。

## 1. 构建并编译

在仓库根目录打开 `cmd.exe`，依次运行：

```bat
script\build_compiler_tests.bat
script\build.bat
example\notepad-showcase\compile.bat
```

最后一条命令应显示已将编译结果写入 `example\notepad-showcase\notepad-showcase.weavec`。仓库中已经包含一次成功编译的该文件，但验收时仍请重新编译，以确认源文件到产物的路径可用。

## 2. 准备记事本

关闭现有记事本窗口，然后只打开一个新的记事本窗口。把记事本输入法切换为英文直接输入、关闭 Caps Lock，先松开所有键，特别是 F1-F12、Ctrl 和 Shift，并把文本光标放在空白文档中；否则字母结果会受当前键盘布局或输入法组合状态影响。

请让记事本和 InputWeaver 处于相同完整性级别，通常两者都以普通用户运行。笔记本电脑如把功能键默认用作媒体键，请使用 Fn 组合或系统的功能键锁，使系统真正收到 F1-F12。

如果必须保留多个记事本进程，启动器会列出候选 PID；此时聚焦要验收的那个窗口，启动器会自动选择唯一的前台候选。

## 3. 启动编译程序

回到仓库根目录的 `cmd.exe`，运行：

```bat
example\notepad-showcase\run.bat
```

看到 `Compiled program is active` 后切回刚才的记事本。切换期间的输入会直接放行；规则只在所选记事本处于前台时响应。此次运行会覆盖并重新写入 `example\notepad-showcase\acceptance.jsonl`。

## 4. 按顺序验收

每一步完成后稍等约 0.2 秒，再进行下一步；这样肉眼更容易区分带等待的任务。

1. 按 F1：应输入 `1`，并把 F11 循环间隔从 120ms 改为 140ms。
2. 按 F3：应输入 `xy`；第一条规则使用 `~>>` 继续匹配，第二条规则消费同一物理事件。
3. 按 F4：应输入 `z`；它来自 `Windows.VirtualKey(0x5A)`。
4. 按 F5：应输入 `22`；它来自 `if` 内嵌的两次 `repeat`。
5. 按 F6：应输入 `a`；按住 F6 时可能出现键盘重复的 `a`，松开后必须停止，这是完整生命周期映射。
6. 按 F8：应输入大写 `INPUTWEAVER`；Shift 必须在动作结束后正确松开。
7. 按 F9：应输入 `bbb`。
8. 按 F10：应输入 `4`，同时把 `burstCount` 从 3 增加到 4；再次按 F9 应输入 `bbbb`。
9. 按住 F11 大约 0.7 秒再松开：应按约 140ms 的循环间隔输入若干个 `c`，松开后必须停止继续输入。
10. 将鼠标指针放在记事本文本区域并按一下中键：应插入一个换行；按下和松开都不应触发记事本自身的中键行为。
11. 按 F7，等待 0.2 秒，再按 F6：不应输入 `a`；再次按 F7，等待 0.2 秒，再按 F6：应恢复输入 `a`。
12. 按 F12，再按 F6：不应输入 `a`；再次按 F12，再按 F6：应恢复输入 `a`。F12 的 PAUSE 切换是同步执行的，不需要等待状态任务。
13. 可选并且最后执行：按 F2，应由 `exec("notepad.exe")` 启动一个新的记事本实例或标签页。

## 5. 停止并保留日志

在任意窗口按物理组合键 Ctrl+Shift+F12。控制台应打印 `Compiled-program session stopped`，并且 `injection_failures=0`、`runtime_diagnostic_drops=0`、`hook_log_drops=0`、`injection_log_drops=0`、`runtime_log_drops=0`、`jsonl_truncated=false`。

不要再次运行 `example\notepad-showcase\run.bat`，否则会覆盖本次日志。完成后告诉我“验收完成”，并附上肉眼观察到的不一致；我会读取 `example\notepad-showcase\acceptance.jsonl`，按照物理输入、运行时状态转换、输出控制编号、注入成功数和取消原因逐项核验。

## 预期输出控制编号

日志中的注入记录使用 Windows Virtual-Key 编号：`1=49`、`2=50`、`4=52`、`A=65`、`B=66`、`C=67`、`E=69`、`I=73`、`N=78`、`P=80`、`R=82`、`T=84`、`U=85`、`V=86`、`W=87`、`X=88`、`Y=89`、`Z=90`、`Enter=13`、`Left Shift=160`。每次 `tap` 正常情况下对应一条 `Down` 和一条 `Up` 注入记录，且两条记录的 `sent` 都应等于 `requested`。
