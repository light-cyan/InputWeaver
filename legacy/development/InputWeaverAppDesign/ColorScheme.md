# 配色文件

## 文件位置

默认配色保存在 `res/InputWeaverTUI.colors.json`。构建 TUI 时把该文件复制到 `bin/res/InputWeaverTUI.colors.json`，运行时始终从 `<executable-directory>/res/InputWeaverTUI.colors.json` 读取，不在页面代码中硬编码颜色。

配色文件使用 UTF-8 JSON。修改文件后重启 TUI 即可应用新颜色。

## 格式

顶层 `version` 当前固定为 `1`，`colors` 包含所有必需颜色。颜色值使用 `#RRGGBB`，不保存 ANSI 转义序列。

```json
{
  "version": 1,
  "colors": {
    "text": "#D0D0D0",
    "muted_text": "#808080",
    "unfocused_border": "#666666",
    "focus_console": "#5C9EFF",
    "focus_programs": "#4DD0E1",
    "focus_program_information": "#FFD54F",
    "focus_compiled_dump": "#CE93D8",
    "focus_events": "#4DD0E1",
    "focus_pressed": "#81C784",
    "focus_action_executions": "#CE93D8",
    "selection_active_foreground": "#101010",
    "selection_active_background": "#B2EBF2",
    "selection_inactive_foreground": "#D0D0D0",
    "selection_inactive_background": "#455A64",
    "status_running": "#26C6DA",
    "status_debug": "#80DEEA",
    "status_dry_run": "#66BB6A",
    "status_exec_permission": "#CE93D8",
    "execution_running": "#26C6DA",
    "execution_completed": "#66BB6A",
    "execution_failed": "#EF5350",
    "execution_cancelled": "#FFCA28",
    "instruction_recent_oldest": "#006064",
    "instruction_recent": "#00ACC1",
    "instruction_current": "#84FFFF",
    "health_trusted": "#66BB6A",
    "health_recovering": "#FFCA28",
    "health_fault": "#FF4D2E"
  }
}
```

仓库中的 JSON 文件是默认值和格式基准，文档示例与该文件保持一致。

## 读取与验证

TUI 在初始化终端绘制前读取并验证整个文件。缺少文件、JSON 无效、版本不支持、必需键缺失或颜色不是 `#RRGGBB` 时，程序向标准错误输出文件路径和具体字段后退出，不使用静默默认值。

JSON 解析器只实现该固定结构并位于 `src/ui/tui/support/`，不引入第三方库。文件读取属于 `src/platform/windows/tui/`。页面渲染只引用已验证的颜色键。

## 终端输出

渲染器把 RGB 值转换为终端颜色序列。焦点、选中行、运行标签、Action Execution、最近指令和 HEALTH 的默认颜色均来自该文件；无焦点边框统一读取 `unfocused_border`。
