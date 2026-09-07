# 第一个输入映射

[文档首页](README.md)

这篇教程会创建一个小程序：在 Windows 记事本中，把 A 键映射为 B 键。完成后，你会知道如何编辑、运行和停止 Weave 程序。

## 1. 打开 InputWeaver

在 Windows 10 或 Windows 11 x64 上，将完整发行包解压到一个可写文件夹，保留其中的四个 EXE 和 `res` 文件夹，然后运行 `InputWeaverHost.exe`。

界面打开后位于 Program 页。左侧 PROGRAM 是程序列表，右上角 PROGRAM INFORMATION 是名称和运行配置，右下角 SOURCE 是源码。

用 `Tab` 在这三个区域之间切换。也可以先用方向键选择区域，再按 `Enter` 进入；`Esc` 返回上一层。

## 2. 创建程序

1. 进入左侧 PROGRAM 区域，按 `A` 打开 Add Program。
2. 用左右方向键选择 `New Blank`，按 `Enter`。在名称输入框中确认或修改程序名，再按 `Enter` 创建。
3. 创建后会进入 SOURCE 区域，按 `E` 开始编辑。
4. 输入下面的完整程序，或用 `Ctrl+V` 粘贴。

```weave
TARGET = "notepad.exe";

A -> B;
```

`TARGET` 选择规则生效的应用程序。`A -> B;` 表示按下、按住和松开 A 时，都按 B 的对应状态处理。每条完整语句用分号结束。

按 `Esc` 保存并退出编辑。编辑时停顿约 400 毫秒也会自动保存和校验；出现红色标记时，先检查名称、引号和分号，必要时到 Console 页查看输出。

## 3. 运行并观察效果

打开记事本。在 InputWeaver 的 Program 页确认选中了刚创建的程序，底部 NEXT RUN 的三个选项均为 `OFF`，然后按空格运行。

切回记事本，使其处于前台。在普通英文输入状态下按 A，应输入 `b`；最终字符仍由键盘布局、输入法、Shift 和 Caps Lock 状态决定。切到其他应用后，A 按原来的用途工作。

运行中的程序在列表中显示 `[RUN]`。InputWeaver 界面本身受到输入排除保护，可以继续正常操作。[目标与排除的具体行为](running.md)

## 4. 停止程序

有三种常用方式：

- 返回 Program 页，选中该程序，退出编辑后按 `X`。
- 按默认退出组合键 `Ctrl+Shift+F12`，停止这个执行器。
- 右击系统托盘中的 InputWeaver 图标，选择 `Exit InputWeaver`，退出 InputWeaver 并停止界面管理的所有 Weave 程序。

关闭界面窗口会让程序继续在托盘中运行。再次单击托盘图标可以打开界面。

## 5. 再加入一个按键宏

先停止执行器，再把源码替换为下面的程序：

```weave
TARGET = "notepad.exe";

A -> B;
F6:down => tap(H) | tap(I);
```

保存后再次按空格运行。切回记事本，按 F6 会依次按一下 H 和 I。这里的 `down` 表示首次按下，`tap` 表示按下后松开，`|` 在两个动作之间加入间隔，`=>` 表示这个 F6 输入由规则接管。

接下来阅读[语言基础](language.md)，或直接查看[输入映射与规则](rules.md)，学习条件、组合键和不同箭头的区别。
