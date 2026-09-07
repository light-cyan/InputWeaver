<a id="section-inputweaver"></a>

# InputWeaver

[English](README.md) | **简体中文**

上下文感知的输入映射与宏引擎。

InputWeaver 使用 Weave 语言，将键盘和鼠标输入组织成映射、条件宏，以及由鼠标移动触发的动作。可以在内置编辑器中创建和调试程序，也可以通过命令行编译和运行。

[使用文档](docs/zh/README.md)

<a id="section-features"></a>

## 功能

- 为指定应用或全局环境映射键盘按键和鼠标按钮，保留按下、按住和松开的行为。
- 组合条件、变量、数组、等待和循环，让宏根据程序状态和物理输入状态执行动作。
- 计量鼠标移动距离、连续移动时长和滚轮输入，按可配置的周期触发动作。
- 使用语法高亮和自动校验编辑源码，在调试器中查看输入、变量、计量器和动作执行过程。
- 通过模拟运行检查规则匹配和动作计算，物理输入放行，输出仅作模拟。

<a id="section-a-small-weave-program"></a>

## 一个简单的 Weave 程序

```weave
TARGET = "notepad.exe";

A -> B;
F6:down => tap(H) | tap(I);
```

记事本是当前有效目标时，A 表现为 B，按下 F6 则依次点击 H 和 I。`TARGET` 选择目标应用，`->` 定义完整按键映射，`|` 在动作之间插入间隔。默认快捷键 `Ctrl+Shift+F12` 用于停止执行器。

<a id="section-getting-started"></a>

## 开始使用

当前执行器和界面运行于 Windows 10 或 Windows 11 x64。

使用打包产物时，将整个压缩包解压到可写目录，保持四个可执行文件和 `res` 文件夹在一起，然后启动 `InputWeaverHost.exe`。按照[第一个程序教程](docs/zh/getting-started.md)创建并运行程序。关闭窗口后，程序仍会在托盘后台运行；在 Program 页退出编辑后按 `X` 停止所选程序，或从托盘菜单选择 `Exit InputWeaver`，停止界面管理的全部程序。

使用命令行时，将上面的示例保存为 `demo.weave`，在可执行文件所在目录运行以下 PowerShell 命令：

```powershell
.\InputWeaverCompiler.exe compile .\demo.weave .\demo.weavec
.\InputWeaver.exe --program .\demo.weavec
```

编译器生成 `.weavec` 文件，由执行器独立加载。目标覆盖、模拟运行和日志选项见[命令行指南](docs/zh/command-line.md)。

<a id="section-build-from-source"></a>

## 从源码构建

在 Windows 上使用 MinGW-w64 工具链构建，确保 `g++` 和 `windres` 在 `PATH` 中。项目使用 C++20，采用 GCC 15.1.0 构建。Windows 产品使用 C++ 标准库和 Windows API。

在仓库根目录运行以下 PowerShell 命令：

```powershell
.\script\build_products.bat
.\bin\InputWeaverHost.exe
```

产品可执行文件和界面配色资源生成在 `bin/` 中。

| 命令 | 用途 |
| --- | --- |
| `script\verify_project.bat` | 构建产品和测试，运行测试与文档校验，分析源码、审计依赖并检查工作区差异 |
| `script\verify_docs.bat` | 构建编译器，检查双语文档结构、本地链接和代码示例 |
| `script\package_release.bat` | 构建产品、校验文档，并生成 Windows x64 便携包 |

验证需要 Git 和 PowerShell；打包还会使用 MinGW-w64 的 `objdump` 检查可执行文件依赖。发行包生成在 `bin/release/InputWeaver-windows-x64.zip`，包含中英文使用文档。
