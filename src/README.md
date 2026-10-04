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
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-installer.ps1 -Version 0.2.11
```

脚本会先进行 Release 构建并检查 exe 为 x64，然后输出 `dist\Offset-Pad-v0.2.11-windows-x64-setup.exe` 和 `.sha256` 校验文件。工具未被自动找到时，可用 `-Compiler` 和 `-InnoCompiler` 指定完整路径。发布新版本时，同步更新 `build-installer.ps1` 和 `installer/offset-pad.iss` 中的默认版本号。

## 测试

使用 MinGW-w64 GCC 运行键位与快捷键测试：

```powershell
gcc -std=c11 -DUNICODE -D_UNICODE -Wall -Wextra -Isrc/input tests\keymap_test.c -o build\keymap_test.exe -luser32
.\build\keymap_test.exe
```

注册表配置读写测试使用模拟 API，不会访问当前用户的真实注册表：

```powershell
gcc -std=c11 -DUNICODE -D_UNICODE -Wall -Wextra -Isrc/app -Isrc/input tests\app_settings_test.c src\input\keymap.c -o build\app_settings_test.exe -ladvapi32 -luser32
.\build\app_settings_test.exe
```

## 项目结构

| 路径 | 职责 |
| --- | --- |
| `src/app/main.c` | 单实例、消息循环和后台启动 |
| `src/app/ui/app_ui.c` | 设置窗口与界面生命周期 |
| `src/app/ui/app_ui_paint.c` | 自绘控件与键位预览 |
| `src/app/ui/app_tray.c` | 托盘图标、菜单和通知 |
| `src/app/app_settings.c` | 用户设置和开机启动项 |
| `src/app/update_check.c` | 启动时异步查询 Gitee Releases |
| `src/app/ui/rounded_box.c` | 圆角控件绘制 |
| `src/input/keymap.c` | 快捷键与数字小键盘映射 |
| `resources/app-icon.rc` | 将窗口和托盘图标嵌入 exe |
| `installer/offset-pad.iss` | 安装、升级和卸载 |

## 设置与资源

快捷键、快捷键启用状态、自定义键位、“屏蔽未映射字符”和“自动检查更新”开关保存在 `HKCU\Software\Offset Pad`；自动检查更新默认开启，每次启动时通过 WinHTTP 请求 Gitee Releases 最新稳定版接口，只有发现更新才显示托盘通知，不会下载或安装；先前保存的 11 键位设置会保留前 10 个数字映射；开机时启动项位于 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，值名为 `Offset Pad`。卸载时会移除这些设置。

“屏蔽未映射字符”新安装默认开启；启用后，小键盘模式仅允许映射键、顶部数字行和快捷键输入，其他未映射字符会被拦截；Backspace、方向键、回车和 Delete 等功能键照常工作；已保存的开关值不变。“按下切换模式”快捷键默认为 LCtrl + Caps Lock；“按住输入”快捷键默认为 Caps Lock + Shift；按住该组合并输入字符时临时启用小键盘，松开后恢复此前模式；单独短按 Caps Lock 仍正常切换大小写锁定。旧版默认的 Caps Lock 按住快捷键会自动迁移为 Caps Lock + Shift；用户自定义的其他快捷键保持不变。临时输入不会改写持久化的模式开关。程序每次启动均从普通键盘模式开始，模式状态只在本次运行中有效。快捷键必须由 2–4 个按键组成，不支持单键触发。录入任一快捷键时按 Delete 或 Backspace 可清除绑定；托盘菜单的“禁用快捷键”选项会同时停用两种快捷键，不影响界面按钮操作。

误按提醒仅在小键盘模式下统计未映射的普通字母首次按下：1.5 秒内 3 次触发；同次开启小键盘时提醒后冷却 10 秒，冷却期按键不计入下一轮；切回普通模式再开启会清除冷却，可立即重新统计。已映射键、长按重复和 Ctrl/Alt/Win 组合键不计数。通知通过现有托盘图标发送。

更新托盘图标后，可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\crop-tray-icons.ps1`，等比裁紧透明边距；脚本会在被忽略的 `build/` 中保存源文件备份。更新窗口 Logo 后，可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\crop-logo.ps1`，处理窗口 ICO 和 README 使用的 PNG。

低级键盘钩子和模拟按键只在普通桌面会话中工作。Windows 安全桌面以及权限高于 Offset Pad 的程序可能收不到映射输入。
