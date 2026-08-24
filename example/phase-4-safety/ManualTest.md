# 第四阶段 Windows 键盘安全验收

本验收使用 `example\phase-4-safety\phase4-safety.weave` 检查进程启动授权、前台资格切换、映射释放、启动持键、任务进度预算、陈旧任务取消和物理强制停止。主运行与启动持键运行分别保留 JSONL 和控制台记录；再次运行同一模式会覆盖对应文件。

## 1. 构建并编译验收程序

在仓库根目录打开 `cmd.exe`，运行：

```bat
script\build_compiler_tests.bat
script\build.bat
example\phase-4-safety\compile.bat
```

三条命令都应以退出码 `0` 完成，最后一条命令应显示 `Source is valid.` 并生成 `example\phase-4-safety\phase4-safety.weavec`。

## 2. 准备目标

只打开一个 Windows 记事本进程，把输入法切换为英文直接输入，关闭 Caps Lock，把文本光标放在空白文档中，并确保记事本与 InputWeaver 处于相同完整性级别。

## 3. 验证默认拒绝进程启动

保持记事本已打开，运行以下命令，不提供 `--allow-exec`：

```bat
bin\InputWeaver.exe --program example\phase-4-safety\phase4-safety.weavec --log example\phase-4-safety\denied.jsonl
```

执行器应报告编译程序激活失败并立即退出；此时输入钩子尚未安装，按 F9 不会由 InputWeaver 启动新进程。

## 4. 授权并启动验收运行

运行验收记录命令：

```bat
example\phase-4-safety\run.bat main
```

命令应立即显示 `Starting InputWeaver; live executor output follows.`；找到唯一目标后还会显示 `Loaded`、`Attached to PID` 和 `Compiled program is active`。看到 `Compiled program is active` 后切回原来的记事本。命令同时显示执行器实时输出并将其保存在 `example\phase-4-safety\acceptance.txt`，JSONL 保存在 `example\phase-4-safety\acceptance.jsonl`。本验收不启用全量输入追踪，只记录程序已配置的键盘控件、物理 F12、运行时诊断和注入结果，普通鼠标移动不会写入验收日志。

## 5. 验证映射与前台丢失清理

先短按 F6，记事本应输入一个 `a`。随后按住 F6，并在保持 F6 的同时用 Alt+Tab 切换到另一个窗口；切换完成后松开 F6。字符重复必须停止，其他窗口中不得出现持续的 `a`，返回记事本后再次短按 F6 应重新输入 `a`。

再次按住 F6，切离记事本并在仍保持 F6 时返回记事本。返回后不得因为仍处于按住状态而重新建立映射；松开 F6，再进行一次新的短按，才应再次输入 `a`。

按 F7 后立刻在 250 毫秒内切离记事本。记事本可能已经收到第一个 `b`，但等待中的第二个 `c` 不得在其他窗口出现，也不得在返回记事本后由旧任务补发。

## 6. 验证非挂起任务预算

在记事本前台按住 F8 约一秒后松开。这个规则只修改内部数值，不生成文本；执行器必须保持键盘响应，并在日志中记录 `TaskBudgetExceeded` 且 `detail=1`。预算取消后，短按 F6 仍应正常输入 `a`。

## 7. 验证显式进程启动授权

按 F9，应由 `exec("notepad.exe")` 发起一次新的记事本启动。确认启动行为后关闭新出现的记事本窗口或标签，只保留原验收目标。

## 8. 验证启动前持键和预置强制停止

先用物理 `Ctrl+Shift+F12` 停止当前运行。回到控制台，运行带五秒延迟的启动持键记录命令：

```bat
example\phase-4-safety\run.bat startup-held
```

按下 Enter 后立即切到原记事本，并在五秒延迟结束前按住 F6、左 Ctrl 和右 Shift。执行器启动后不得因为 F6 已经按下而建立 A 映射输出；先松开 F6，再重新短按 F6，才应建立映射。此时 Ctrl 与 Shift 仍保持按下，因此映射出的 A 会作为 `Ctrl+Shift+A` 交给系统，记事本可能全选当前文本，这是新映射确实生效的预期副作用；如飞书等常驻软件占用 `Ctrl+Shift+A`，应在本次运行前退出该软件或修改其快捷键。继续保持 Ctrl 与 Shift，并按 F12，执行器应立即停止，证明启动前已按下的修饰键仍可构成物理强制停止组合。控制台记录保存在 `example\phase-4-safety\startup-held.txt`，JSONL 保存在 `example\phase-4-safety\startup-held.jsonl`。

## 9. 保留证据并提交核验

人工操作者完成第 5 至第 8 节并确认可见行为后，只需保留四份证据并告知实现或验收负责人，不需要运行日志校验脚本，也不要再次运行任一模式覆盖本轮证据。

实现或验收负责人运行 `example\phase-4-safety\verify.bat`。校验要求执行器退出码为 `0`，检查最终控制台指标中的 `injection_failures=0`、`runtime_diagnostic_drops=0`、三类外层日志丢弃计数均为 `0`、`jsonl_truncated=false`、可解析的 `max_hook_us` 与 `scheduler_backoffs`，并逐行解析两份 JSONL。主日志必须证明至少三次前台丢失与返回、对应的可逆取消、至少五次完整 A 映射生命周期、F8 指令预算取消、F9 启动路径没有失败诊断、至少一次 F7 任务在 B 输出后、C 输出前因前台丢失被取消、丢失边界之后没有同代 C 输出，以及物理强制停止；启动持键日志必须证明第一次物理 F6 松开之前没有 A 输出，之后的新按键可以形成完整映射，并由启动前已按下的修饰键完成物理强制停止。

校验脚本负责机器可判定的安全事实；仍需人工确认第 5 至第 8 节描述的窗口与可见文本行为，包括字符是否出现在错误窗口、重复是否及时停止以及 F9 是否实际显示新记事本。任何注入失败、释放缺失、陈旧代次输出、安全诊断丢弃、日志截断、非零退出或无法物理停止都属于验收失败。

验收结束后保留 `example\phase-4-safety\acceptance.jsonl`、`example\phase-4-safety\acceptance.txt`、`example\phase-4-safety\startup-held.jsonl` 和 `example\phase-4-safety\startup-held.txt` 作为同一轮证据。
