# 程序库存储与导入

## 目录

应用在自身同级目录使用 `programs/` 保存程序库：

```text
programs/
    programs.index
    000001.entry
    000001.weavec
    000001.dump.txt
    000002.entry
    000002.weavec
    000002.dump.txt
    logs/
        000001-20260827-101530-1.jsonl
```

内部 ID 使用固定宽度数字文件名。显示名称只存在于 `.entry` 中。

## programs.index

`programs.index` 只保存稳定内部 ID 的显示顺序。文件中的记录顺序就是 Programs 页面的列表顺序。

调整顺序时，应用先写入同目录临时文件，再原子替换 `programs.index`。

## entry 文件

每个 `.entry` 保存显示名称、Target 模式、Executable 选择器和 Logging 模式。元数据修改使用同目录临时文件和原子替换。

## 添加

用户在 Programs 页面按 `A` 打开单文件路径输入框。输入框接受手工输入、剪贴板粘贴，以及终端把拖入文件转换成的路径文本。

应用去除路径外层引号并接受一个 `.weave` 文件。一次输入包含多个文件时，应用保留输入框并显示一次单文件要求。

默认显示名称是源文件名去掉扩展名后的结果。

## 编译发布

导入使用以下顺序：

1. 在程序库目录生成唯一临时产物路径。
2. 调用 `InputWeaverCompiler.exe compile <source.weave> <temporary.weavec>`。
3. 编译成功后调用 `InputWeaverCompiler.exe dump <source.weave>` 并把标准输出保存到临时 Dump。
4. 为新条目写入临时 `.entry`。
5. 原子发布 `.weavec`、Dump 和 `.entry`。
6. 原子更新 `programs.index`。

`compile` 已执行完整验证，导入流程不单独调用 `validate`。

编译或 Dump 失败时，应用删除临时文件，不增加条目，不替换已存在产物，并自动切换到 Console 展示编译器诊断。

## 名称冲突

显示名称使用 Windows ordinal 无视大小写比较；`Game` 与 `game` 视为同名。显示名称已经存在时，应用提供 `Overwrite`、`Rename` 和 `Cancel`：

- `Overwrite` 使用新 `.weavec` 和 Dump 替换原条目的文件，并保留原 ID、显示名称、显示顺序和运行配置。
- `Rename` 请求新的唯一名称，并以新内部 ID 创建条目。
- `Cancel` 删除临时文件并返回 Programs。

覆盖一个正在运行的条目只改变下次启动使用的产物，当前执行器继续使用已经加载的程序。

## 修改名称

用户按 `R` 修改所选条目的显示名称。新名称必须非空且在程序库中唯一。修改只替换对应 `.entry`。

## 删除

用户按 `D` 后先显示包含条目名称的删除确认。确认框实时按键栏显示 `Enter Delete` 和 `Esc Cancel`；确认后，如果条目正在运行，应用先请求执行器有序停止并等待退出。

删除操作移除索引记录、`.entry`、`.weavec` 和 Dump。历史日志保留在 `programs/logs/`。

## 启动修复

`programs.index` 不存在时，应用扫描所有 `.entry`，保留具有对应 `.weavec` 的条目，按内部 ID 排序并重新生成索引，然后显示一次顺序恢复提示。

索引引用的 `.weavec` 不存在时，应用从索引移除对应条目，删除相应 `.entry` 和 Dump，并在加载完成后统一列出被移除的显示名称。

索引引用的 `.entry` 不存在时，应用从索引移除对应 ID，保留孤立的 `.weavec`，并在加载完成后统一提示元数据缺失。

不在索引中的完整条目文件在索引正常存在时不自动加入列表。
