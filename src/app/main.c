#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "app_settings.h"
#include "app_ui.h"
#include "keymap.h"

#define IDI_APP_ICON_LIGHT 101
#define IDI_APP_ICON_DARK 102
#define IDI_TRAY_ICON_O 103
#define IDI_TRAY_ICON_9 104

static HWND main_window;
static UINT taskbar_created;
static int restart_background;

static void set_enabled(int enabled)
{
    keymap_set_enabled(enabled);
    if (!settings_save_enabled(keymap_is_latched()))
        MessageBoxW(main_window, L"无法保存键盘模式；本次运行仍会按当前模式工作。",
                    L"Offset Pad", MB_OK | MB_ICONERROR);
    ui_refresh();
}

static void quit_app(void)
{
    if (main_window != NULL)
        DestroyWindow(main_window);
}

static void restart_in_background(void)
{
    restart_background = 1;
    quit_app();
}

static int launch_background(void)
{
    wchar_t path[MAX_PATH];
    wchar_t command[MAX_PATH + 32];
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH ||
        swprintf(command, sizeof(command) / sizeof(command[0]),
                 L"\"%ls\" --background", path) < 0)
        return 0;
    startup.cb = sizeof(startup);
    if (!CreateProcessW(path, command, NULL, NULL, FALSE,
                        0, NULL, NULL, &startup, &process))
        return 0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}

static LRESULT CALLBACK main_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (taskbar_created != 0 && message == taskbar_created) {
        ui_taskbar_created();
        return 0;
    }
    switch (message) {
    case WM_OFFSET_PAD_TRAY:
        ui_tray_message(lparam);
        return 0;
    case WM_OFFSET_PAD_SHOW:
        ui_show();
        return 0;
    case WM_OFFSET_PAD_MODE_CHANGED:
        if (!settings_save_enabled(keymap_is_latched()))
            MessageBoxW(window, L"无法保存键盘模式；本次运行仍会按当前模式工作。",
                        L"Offset Pad", MB_OK | MB_ICONERROR);
        ui_refresh();
        return 0;
    case WM_OFFSET_PAD_EFFECTIVE_CHANGED:
        ui_refresh();
        return 0;
    case WM_OFFSET_PAD_HOLD_CAPTURE_DONE:
        if (wparam == KEYMAP_CAPTURE_INVALID)
            MessageBoxW(window, L"快捷键已占用或与数字映射冲突，请换一个组合。",
                        L"Offset Pad", MB_OK | MB_ICONINFORMATION);
        else if (wparam == KEYMAP_CAPTURE_SAVED &&
                 !settings_save_hold_hotkey(keymap_get_hold_hotkey()))
            MessageBoxW(window, L"无法保存按住快捷键；本次运行仍会使用新快捷键。",
                        L"Offset Pad", MB_OK | MB_ICONERROR);
        ui_refresh();
        return 0;
    case WM_OFFSET_PAD_MODE_REMINDER:
        ui_show_mode_reminder();
        return 0;
    case WM_OFFSET_PAD_HOTKEY_CAPTURE_DONE:
        if (wparam == KEYMAP_CAPTURE_INVALID)
            MessageBoxW(window, L"快捷键已占用或与数字映射冲突，请换一个组合。",
                        L"Offset Pad", MB_OK | MB_ICONINFORMATION);
        else if (wparam == KEYMAP_CAPTURE_SAVED &&
                 !settings_save_hotkey(keymap_get_hotkey()))
            MessageBoxW(window, L"无法保存快捷键；本次运行仍会使用新快捷键。",
                        L"Offset Pad", MB_OK | MB_ICONERROR);
        ui_refresh();
        return 0;
    case WM_OFFSET_PAD_SOURCE_CAPTURE_DONE:
        if (wparam == KEYMAP_CAPTURE_INVALID) {
            MessageBoxW(window, L"请按单个字母、数字、常用符号或空格，且不要占用单键快捷键；Esc 可取消。",
                        L"Offset Pad", MB_OK | MB_ICONINFORMATION);
        } else if (wparam == KEYMAP_CAPTURE_SAVED) {
            DWORD sources[KEYMAP_KEY_COUNT];
            keymap_get_sources(sources);
            if (!settings_save_sources(sources))
                MessageBoxW(window, L"无法保存按键映射；本次运行仍会使用新键位。",
                            L"Offset Pad", MB_OK | MB_ICONERROR);
        }
        ui_refresh();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        main_window = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command)
{
    WNDCLASSW definition = {0};
    app_ui_actions actions = {set_enabled, quit_app, restart_in_background};
    HANDLE singleton;
    HWND existing;
    HICON light_icon;
    HICON dark_icon;
    HICON tray_o_icon;
    HICON tray_9_icon;
    int small_icon_width;
    int small_icon_height;
    MSG message;
    DWORD sources[KEYMAP_KEY_COUNT];
    int result;
    int background = wcsstr(GetCommandLineW(), L"--background") != NULL;
    int exit_code = 0;
    (void)previous;
    (void)command_line;
    (void)show_command;

    singleton = CreateMutexW(NULL, TRUE, L"Local\\Offset Pad.SingleInstance");
    if (singleton == NULL) {
        MessageBoxW(NULL, L"无法创建单实例锁。", L"Offset Pad", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        int attempt;
        for (attempt = 0; attempt < 20; ++attempt) {
            existing = FindWindowW(OFFSET_PAD_MAIN_CLASS, NULL);
            if (existing != NULL) {
                if (!background)
                    PostMessageW(existing, WM_OFFSET_PAD_SHOW, 0, 0);
                break;
            }
            Sleep(50);
        }
        CloseHandle(singleton);
        return 0;
    }

    SetProcessDPIAware();
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    small_icon_width = GetSystemMetrics(SM_CXSMICON);
    small_icon_height = GetSystemMetrics(SM_CYSMICON);
    light_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON_LIGHT),
                                   IMAGE_ICON, small_icon_width, small_icon_height, LR_SHARED);
    dark_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON_DARK),
                                  IMAGE_ICON, small_icon_width, small_icon_height, LR_SHARED);
    tray_o_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_TRAY_ICON_O),
                                    IMAGE_ICON, small_icon_width, small_icon_height, LR_SHARED);
    tray_9_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_TRAY_ICON_9),
                                    IMAGE_ICON, small_icon_width, small_icon_height, LR_SHARED);
    if (light_icon == NULL)
        light_icon = LoadIconW(NULL, IDI_APPLICATION);
    if (dark_icon == NULL)
        dark_icon = light_icon;
    if (tray_o_icon == NULL)
        tray_o_icon = light_icon;
    if (tray_9_icon == NULL)
        tray_9_icon = dark_icon;
    definition.lpfnWndProc = main_proc;
    definition.hInstance = instance;
    definition.hIcon = settings_load_enabled() ? dark_icon : light_icon;
    definition.lpszClassName = OFFSET_PAD_MAIN_CLASS;
    if (!RegisterClassW(&definition)) {
        exit_code = 1;
        goto cleanup_mutex;
    }
    main_window = CreateWindowExW(0, OFFSET_PAD_MAIN_CLASS, L"Offset Pad", WS_OVERLAPPED,
                                   0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (main_window == NULL) {
        exit_code = 1;
        goto cleanup_class;
    }
    if (settings_load_sources(sources))
        keymap_set_sources(sources);
    keymap_set_enabled(settings_load_enabled());
    keymap_set_hotkeys_enabled(settings_load_hotkeys_enabled());
    keymap_set_block_letters(settings_load_block_letters());
    keymap_set_hotkey(settings_load_hotkey());
    keymap_set_hold_hotkey(settings_load_hold_hotkey());
    keymap_set_capture_message(WM_OFFSET_PAD_HOTKEY_CAPTURE_DONE);
    keymap_set_hold_capture_message(WM_OFFSET_PAD_HOLD_CAPTURE_DONE);
    keymap_set_effective_changed_message(WM_OFFSET_PAD_EFFECTIVE_CHANGED);
    keymap_set_source_capture_message(WM_OFFSET_PAD_SOURCE_CAPTURE_DONE);
    keymap_set_reminder_message(WM_OFFSET_PAD_MODE_REMINDER);
    if (!ui_init(instance, main_window, light_icon, dark_icon,
                 tray_o_icon, tray_9_icon, &actions)) {
        exit_code = 1;
        goto cleanup_window;
    }
    if (!keymap_install(instance, main_window, WM_OFFSET_PAD_MODE_CHANGED)) {
        MessageBoxW(NULL, L"无法安装键盘监听，请检查系统权限。", L"Offset Pad",
                    MB_OK | MB_ICONERROR);
        exit_code = 1;
        goto cleanup_ui;
    }
    if (!background)
        ui_show();
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (ui_handle_dialog_message(&message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result < 0)
        exit_code = 1;
    keymap_uninstall();
cleanup_ui:
    ui_shutdown();
cleanup_window:
    if (main_window != NULL)
        DestroyWindow(main_window);
cleanup_class:
    UnregisterClassW(OFFSET_PAD_MAIN_CLASS, instance);
cleanup_mutex:
    ReleaseMutex(singleton);
    CloseHandle(singleton);
    if (restart_background && !launch_background()) {
        MessageBoxW(NULL, L"无法在后台重新启动 Offset Pad。", L"Offset Pad", MB_OK | MB_ICONERROR);
        exit_code = 1;
    }
    return exit_code;
}
