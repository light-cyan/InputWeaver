<a id="section-command-line"></a>

# 命令行

[English](../en/command-line.md)

[文档首页](README.md)

命令行适合用外部编辑器编写源码，或从脚本编译和启动程序。下面的产品命令使用 PowerShell 写法，在包含产品 EXE 的目录中执行。

<a id="section-validate-source"></a>

## 校验源码

```powershell
.\InputWeaverCompiler.exe validate .\demo.weave
```

成功时显示 `Source is valid.`。失败时显示文件、行列位置和错误说明。校验只检查源码；需要运行时再编译为 `.weavec`。

<a id="section-compile"></a>

## 编译

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave .\demo.weavec
```

省略输出路径时，在源文件旁生成同名 `.weavec`：

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave
```

编译成功才会发布新的目标文件。保存源码后要再次编译，执行器才会运行新的内容。

<a id="section-compiled-file-version-mismatch"></a>

### 编译文件版本不匹配

出现 `artifact magic or format version differs from the current WEAVEC format` 时，先确认选中的是编译后的 `.weavec` 文件，再使用与执行器配套的编译器从 `.weave` 源码重新生成文件。

更新发行包时，先停止正在运行的宿主和执行器，再使用同一发行包中的产品文件。在该目录中重新编译并运行：

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave .\demo.weavec
.\InputWeaver.exe --program .\demo.weavec
```

通过界面管理的程序也可以重新导入当前 `.weave` 源码，导入成功会重新生成编译文件。需要沿用原配置时，将当前源码另存为与原条目同名的 `.weave` 文件，再导入并在名称冲突时选择覆盖。当前源码保存在[程序库](tui.md#section-backing-up-and-moving-the-program-library)中，操作步骤见[新建和导入](tui.md#section-creating-and-importing-programs)。

如果重新生成后仍无法读取编译文件，检查实际运行的 EXE、编译输出路径和 `--program` 路径是否对应，并从完整发行包重新取得配套产品文件。

<a id="section-run"></a>

## 运行

```powershell
.\InputWeaver.exe --program .\demo.weavec
```

执行器读取编译文件中的 `TARGET`。目标应用尚未启动时显示等待信息；应用启动并取得前台后，符合条件的输入开始触发规则。

<a id="section-override-the-target"></a>

### 临时覆盖目标

```powershell
.\InputWeaver.exe --program .\demo.weavec --target notepad.exe
.\InputWeaver.exe --program .\demo.weavec --target "C:\Tools\My Editor.exe"
.\InputWeaver.exe --program .\demo.weavec --target-global
```

`--target` 和 `--target-global` 选择本次运行的目标，并覆盖源码中的 `TARGET`；一次运行选择其中一种。文件名或路径包含空格时使用引号。[目标选择和运行状态](running.md)

<a id="section-exclude-an-application"></a>

### 排除一个应用

```powershell
.\InputWeaver.exe --program .\demo.weavec --target-global --exclude InputWeaverTUI.exe
```

排除项接受可执行文件名或绝对路径。排除进程处于前台时，普通规则和映射不响应输入，输入放行，也停止新的按键和鼠标输出；[退出规则仍优先处理](running.md#section-excluding-an-application)。通过界面启动时，会自动设置上述 TUI 排除项。

<a id="section-dry-run"></a>

## 模拟运行

```powershell
.\InputWeaver.exe --program .\demo.weavec --dry-run
```

模拟运行仍进行规则匹配、变量修改、等待和退出判断，但物理输入始终放行，键盘和鼠标输出只计算模拟结果。想同时查看图形化调试信息，可以使用界面的 `T` 和 `S` 选项。[调试与排错](debugging.md)

<a id="section-allow-external-processes"></a>

## 允许启动外部进程

```powershell
.\InputWeaver.exe --program .\demo.weavec --allow-exec
```

包含 `exec` 的编译程序需要本次启动的授权。模拟运行包含 `exec` 的程序时，同时指定两个选项：

```powershell
.\InputWeaver.exe --program .\demo.weavec --dry-run --allow-exec
```

此时只模拟启动成功，外部程序不会真正启动。

<a id="section-save-logs"></a>

## 保存日志

```powershell
.\InputWeaver.exe --program .\demo.weavec --log .\run.jsonl
.\InputWeaver.exe --program .\demo.weavec --log .\run.jsonl --trace-input
```

第一条保存运行日志，第二条额外记录物理输入和输出轨迹。`--trace-input` 和 `--log` 配合使用。每次启动会覆盖指定日志文件；需要保留多次运行时，为它们使用不同文件名。[日志字段与常见问题](debugging.md)

<a id="section-stop-and-get-help"></a>

## 停止与帮助

默认使用物理 `Ctrl+Shift+F12` 停止程序。源码中写有 `exit` 规则时，使用该程序定义的退出方式。[退出规则](rules.md)

```powershell
.\InputWeaver.exe --help
```

编译器在不传参数时显示三个命令的用法：`validate`、`compile`、`dump`。

<a id="section-inspect-compiled-content"></a>

## 查看编译内容

```powershell
.\InputWeaverCompiler.exe dump .\demo.weave
```

`dump` 检查并编译源文件，然后打印编译内容的文本视图，适合检查规则和控制引用。它的输入是 `.weave` 源文件。
