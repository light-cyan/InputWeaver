# InputWeaver 应用设计

## 定位

InputWeaver 应用是面向已编译程序的终端控制界面，产物名称为 `InputWeaverTUI.exe`。应用负责导入 Weave 源文件、调用编译器、保存编译结果、管理执行器进程、显示控制台输出，并通过现有 `DebugClient` 展示输入调试状态。

应用使用持久化的 `.weavec` 作为运行输入。导入时生成并保存编译结果和编译 Dump，程序库不保存 Weave 源文件。

## 组件关系

```text
.weave
    -> InputWeaverCompiler.exe compile
    -> .weavec
    -> InputWeaver.exe

.weave
    -> InputWeaverCompiler.exe dump
    -> saved dump text

InputWeaverTUI.exe
    -> manages program entries and executor processes
    -> captures compiler and executor console output
    -> owns one WindowsDebugClient for the active debug executor
```

编译器和执行器继续作为独立程序工作，`.weavec` 继续作为二者之间的持久化边界。

## 页面

应用包含三个横向页面：

```text
[Console] <-> [Programs] <-> [Debug]
```

- `Programs` 是默认页面，负责程序选择、导入、排序、运行配置、启动、停止和 Dump 展示。
- `Console` 聚合显示编译器与托管执行器的标准输出和标准错误。
- `Debug` 显示输入事件、当前按下控制、动作执行和固定健康状态。

`Left` 与 `Right` 在页面主焦点下切换到相邻页面，页面不首尾循环。编辑输入框、选择配置值或移动条目时，按键由当前操作接管。

## 核心运行约束

- 每个程序条目最多关联一个执行器。
- 不同程序条目可以同时普通运行。
- 应用全局最多管理一个 debug 执行器。
- 对另一个条目启动 debug 时，应用先停止当前 debug 执行器并等待其退出，再启动新的 debug 执行器。
- 对正在运行的条目再次提交启动不会创建新进程，也不产生额外提示。
- 应用退出时有序停止全部托管执行器。

## 文档入口

- [ApplicationModel.md](ApplicationModel.md) 定义程序条目、执行器、调试会话、运行选项和通用交互。
- [StorageAndImport.md](StorageAndImport.md) 定义程序库存储、导入、覆盖、删除和启动修复。
- [ProgramsPage.md](ProgramsPage.md) 定义 Programs 页面布局、状态标签、配置和按键。
- [ConsolePage.md](ConsolePage.md) 定义 Console 页面输出和滚动行为。
- [DebugPage.md](DebugPage.md) 定义 Debug 页面布局、事件、按下状态、动作执行和健康状态。
- [RuntimeIntegration.md](RuntimeIntegration.md) 定义编译器、执行器、dry-run、真实时间和 DebugClient 集成。
- [ImplementationStructure.md](ImplementationStructure.md) 定义 App/TUI 源码边界和 support 复用规则。
- [ColorScheme.md](ColorScheme.md) 定义 `res` 中可编辑的 JSON 配色文件及加载规则。
