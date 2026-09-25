# Offset Pad

<p align="center"><img src="media/OffsetPad.png" alt="Offset Pad Logo" width="180"></p>

Offset Pad 给没有独立数字小键盘的小配列键盘增加一个数字键层，支持 Windows x64。

## 为什么做这个

小配列键盘通常没有独立数字小键盘。虽然日常打字很少用到它，但在处理表格或集中录入数字时，九宫格布局又很顺手。

Offset Pad 把右手打字区临时变成近似九宫格的小键盘。左手按快捷键切换，右手保持原来的打字位置，就能按熟悉的布局输入数字；用完再切回普通键盘，无需移动手腕或另外接一个小键盘。

## 快速开始

1. 运行 Windows x64 安装包，安装并打开 Offset Pad。
2. 点击“开启小键盘”，或按默认快捷键 **Ctrl + Alt + Shift** 切换模式。
3. 在设置窗口中可以修改快捷键，或开启“开机时启动”。

| 原键 | 数字小键盘键 |
| --- | --- |
| `U` `I` `O` | `7` `8` `9` |
| `J` `K` `L` | `4` `5` `6` |
| `N` `M` `,` | `1` `2` `3` |
| 空格 | `0` |

关闭设置窗口后，程序仍在系统托盘运行。托盘图标为 `O` 时是普通键盘模式，为 `9` 时是数字小键盘模式；右键图标可以切换模式或退出程序。如果数字输入不符合预期，请检查 Num Lock 是否开启。

| 普通键盘模式 | 数字小键盘模式 |
| :---: | :---: |
| <img src="media/OffsetPad-icon-o.png" alt="普通键盘模式托盘图标 O" width="64"> | <img src="media/OffsetPad-icon-9.png" alt="数字小键盘模式托盘图标 9" width="64"> |

## 界面预览

![Offset Pad 设置窗口](media/run.png)

源码构建与项目结构见 [src/README.md](src/README.md)。

本项目采用 [MIT License](LICENSE)。
