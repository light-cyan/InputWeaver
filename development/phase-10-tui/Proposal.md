# Phase 10：TUI 设计入口

## 状态

TUI 的当前产品设计已经收束并统一记录在 `development/InputWeaverAppDesign/`。本文件仅保留 Phase 10 的阶段入口，具体页面和按键以统一设计目录为准。

## 页面入口

- `ProgramsPage.md`：程序列表、信息配置、编译转储和运行控制。
- `ConsolePage.md`：编译器与执行器输出、错误定位和滚动规则。
- `DebugPage.md`：EVENTS、PRESSED、ACTION EXECUTIONS 和固定 HEALTH 区域。
- `ApplicationModel.md`：三页导航、焦点、通用滚动和一次性运行选项。
- `ImplementationStructure.md`：TUI 共用结构与 support 放置边界。
- `ColorScheme.md`：`res` 中 JSON 配色文件的格式和加载规则。

TUI 只提交 App 命令并渲染 App 状态与 `DebugClient` 派生状态，不自行解释调试协议或复制运行时逻辑。
