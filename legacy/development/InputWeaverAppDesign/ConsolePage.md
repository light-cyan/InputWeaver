# Console 页面

## 内容

Console 页面按到达顺序聚合显示编译器和全部托管执行器的标准输出与标准错误。每一行包含程序显示名称和输出来源：

```text
┌─ CONSOLE ────────────────────────────────────────────────────────────────┐
│ [Game][Compiler] Wrote 1842 bytes to ...                                │
│ [Game][Runtime] Compiled program is active globally.                    │
│ [Browser][Runtime] Waiting for target browser.exe...                    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
KEYS  ↑↓ Line  PgUp/PgDn Page  Home First  End Latest  → Programs
```

编译失败、Dump 失败或执行器启动失败时，应用自动切换到 Console 并保持视图位于最新输出。

每次启用日志的运行开始时，应用把生成的具体 JSONL 路径写入 Console。Console 不读取或解析 JSONL。

## 缓冲

Console 使用有界内存缓冲保存最近输出行。缓冲移除最早行时不影响执行器输出和 JSONL 文件。

标准输出和标准错误由独立读取流汇入同一个按接收顺序追加的显示缓冲。应用排空子进程管道后才完成对应进程的停止处理。

## 自动跟随

Console 默认跟随最新输出。用户使用 `Up`、`PageUp` 或 `Home` 向上查看后停止自动跟随，新输出继续进入缓冲但不改变当前滚动位置。

按 `End` 移动到最新输出并恢复自动跟随。

## 按键

| 按键 | 行为 |
| --- | --- |
| `Up` / `Down` | 滚动一行 |
| `PageUp` / `PageDown` | 滚动一页 |
| `Home` | 移动到最早保留行 |
| `End` | 移动到最新行并恢复自动跟随 |
| `Right` | 切换到 Programs |

Console 不提供命令输入、搜索、记录详情或日志分析。

CONSOLE 获得焦点时标题和边框使用亮蓝色。底部 KEYS 行固定显示当前可用的滚动和页面导航按键；Console 没有其他焦点区域。
