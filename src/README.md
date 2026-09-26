# 开发与构建

以下命令均在项目根目录的 PowerShell 中运行。

## 构建程序

需要 Windows x64，以及以下任一 C 编译环境：MinGW-w64 GCC 和 `windres`，或 Visual Studio C++ Build Tools 和 Windows SDK。脚本优先使用 `PATH` 中的 GCC，找不到时尝试 MSVC。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Release
```

输出为 `build\Offset Pad.exe`，可以直接运行。省略 `-Release` 可保留调试信息。如需指定 GCC，添加 `-Compiler 'C:\path\to\mingw\bin\gcc.exe'`。重新构建前，先从托盘退出正在运行的程序，以免 exe 被占用。

## 构建安装包

安装 Inno Setup 7 后运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-installer.ps1 -Version 0.1.1
```

脚本会先进行 Release 构建并检查 exe 为 x64，然后输出 `dist\Offset-Pad-v0.1.1-windows-x64-setup.exe` 和 `.sha256` 校验文件。工具未被自动找到时，可用 `-Compiler` 和 `-InnoCompiler` 指定完整路径。发布新版本时，同步更新 `build-installer.ps1` 和 `installer/offset-pad.iss` 中的默认版本号。

## 测试

使用 MinGW-w64 GCC 运行键位与快捷键测试：

```powershell
gcc -std=c11 -DUNICODE -D_UNICODE -Wall -Wextra -Isrc/input tests\keymap_test.c -o build\keymap_test.exe -luser32
.\build\keymap_test.exe
```

## 项目结构

| 路径 | 职责 |
| --- | --- |
| `src/app/main.c` | 单实例、消息循环和后台启动 |
| `src/app/app_ui.c` | 设置窗口、托盘图标和菜单 |
| `src/app/app_settings.c` | 用户设置和开机启动项 |
| `src/app/rounded_box.c` | 圆角控件绘制 |
| `src/input/keymap.c` | 快捷键与数字小键盘映射 |
| `resources/app-icon.rc` | 将窗口和托盘图标嵌入 exe |
| `installer/offset-pad.iss` | 安装、升级和卸载 |

## 设置与资源

当前模式、快捷键和自定义键位保存在 `HKCU\Software\Offset Pad`；先前保存的 11 键位设置会保留前 10 个数字映射；开机启动项位于 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，值名为 `Offset Pad`。卸载时会移除这些设置。

更新托盘图标后，可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\crop-tray-icons.ps1`，等比裁紧透明边距；脚本会在被忽略的 `build/` 中保存源文件备份。更新窗口 Logo 后，可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\crop-logo.ps1`，处理窗口 ICO 和 README 使用的 PNG。

低级键盘钩子和模拟按键只在普通桌面会话中工作。Windows 安全桌面以及权限高于 Offset Pad 的程序可能收不到映射输入。
