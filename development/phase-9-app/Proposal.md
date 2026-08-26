# Phase 9：App 设计入口

## 状态

App 的当前产品设计已经收束并统一记录在 `development/InputWeaverAppDesign/`。本文件仅保留 Phase 9 的阶段入口，具体行为以统一设计目录为准。

## 设计范围

- `ApplicationModel.md` 定义程序条目、运行配置、执行器和唯一调试会话。
- `StorageAndImport.md` 定义 `.weave` 导入、`.weavec` 管理、排序索引和恢复规则。
- `RuntimeIntegration.md` 定义编译器、执行器和 `DebugClient` 的调用边界。
- `ProgramsPage.md` 定义程序管理和运行控制所需的 App 行为。
- `ImplementationStructure.md` 定义 App 源码边界和 support 复用。

Phase 9 不在编译器、运行时或 `DebugClient` 内增加应用状态；App 作为 `InputWeaverTUI.exe` 内的控制层管理条目、子进程和调试连接。
