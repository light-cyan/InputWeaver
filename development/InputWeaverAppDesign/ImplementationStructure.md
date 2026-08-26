# 实现结构与 support 复用

## 当前 support 内容

`src/support/` 当前只保存与业务域和操作系统无关的基础原语：

| 文件 | 当前能力 |
| --- | --- |
| `bit_mix.hpp` | 64 位混合函数。 |
| `bounded_mpmc_queue.hpp` | 固定容量、无锁、多生产者多消费者队列。 |
| `callback_ref.hpp` | 无所有权、可空的 noexcept 回调引用。 |
| `fixed_spsc_ring.hpp` | 固定容量、单生产者单消费者环形队列。 |
| `little_endian.hpp` | 无符号整数的小端写入和读取。 |
| `utf8.hpp` | UTF-8 有效性检查。 |

`src/platform/windows/support/` 当前保存与具体业务模块无关的 Win32 原语：

| 文件 | 当前能力 |
| --- | --- |
| `ordinal_string.hpp` | Windows ordinal 字符串无视大小写比较。 |
| `unique_handle.hpp` | Win32 `HANDLE` 的 RAII 所有权。 |

## 放置规则

真正跨模块复用且不包含 App、TUI、编译器、运行时或 Debug 语义的结构放在 `src/support/`。只依赖 Win32、但不属于某个业务模块的结构放在 `src/platform/windows/support/`。

只在多个 TUI 页面之间复用的焦点、滚动视口、实时按键栏和文本布局结构仍然属于 UI 域，放在 `src/ui/tui/support/`，不提升到根 `src/support/`。复用次数本身不改变结构所属的业务域。

## App 与 TUI 边界

```text
src/app/
    program catalog, import state, executor state, debug-session orchestration

src/ui/tui/
    page state, input intents, rendering, page navigation

src/ui/tui/support/
    shared focus, viewport, help-bar, text-layout and color-scheme structures

src/platform/windows/app/
    child processes, redirected pipes, atomic files and programs directory access

src/platform/windows/tui/
    console entry point, terminal input, resource-file loading and terminal drawing
```

`src/app/` 不依赖 TUI，`src/ui/tui/` 只读取 App 快照并提交 App 命令。Win32 进程、文件和终端 API 分别留在对应的 `src/platform/windows/` 模块。

## 现有结构复用

- 子进程、管道和进程句柄复用 `UniqueHandle`；为容器化管理执行器所需的移动、释放和重置能力继续在 `src/platform/windows/support/unique_handle.hpp` 内完善。
- 程序显示名称和 Windows 选择器需要无视大小写比较时复用 `EqualOrdinalIgnoreCase`。
- UTF-8 元数据读取复用 `IsValidUtf8`。
- 编译器现有的同目录临时文件和原子替换实现具有跨模块价值，通用 Win32 文件操作下沉到 `src/platform/windows/support/`，编译器接口和 App 存储共同复用。
- 配色 JSON 的固定结构解析和已验证颜色模型放在 `src/ui/tui/support/`，Windows 文件定位与读取放在 `src/platform/windows/tui/`。

`BoundedMpmcQueue` 要求元素可无异常默认构造和复制，`FixedSpscRing` 只支持单生产者与单消费者；二者都不直接承担可滚动的可变长 Console 文本历史。Console 的有界历史和并发汇入由 App 层按其字符串所有权需求实现。

## 现有源码改动计数

support 的复用整理属于代码组织，不增加产品功能。现有编译器、运行时和 Debug 组件需要的功能性扩展仍然只有 `--dry-run` 与调试墙钟时间两项。
