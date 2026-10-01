#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#include "app_tray.h"
#include "app_settings.h"
#include "keymap.h"

#define APP_TRAY_ID 1
#define MENU_OPEN 201
#define MENU_TOGGLE 202
#define MENU_AUTOSTART 203
#define MENU_EXIT 204
#define MENU_DISABLE_HOTKEYS 205

static HWND owner_window;
static UINT callback_message;
static HICON inactive_icon;
static HICON active_icon;
static app_tray_callbacks actions;
static int tray_added;
static int update_notice_active;

static HICON current_icon(void)
{
    return keymap_is_visual_enabled() ? active_icon : inactive_icon;
}

static void show_error(const wchar_t *message)
{
    MessageBoxW(owner_window, message, L"Offset Pad", MB_OK | MB_ICONERROR);
}

static void add_tray(void)
{
    NOTIFYICONDATAW data = {0};
    if (tray_added)
        return;
    data.cbSize = sizeof(data);
    data.hWnd = owner_window;
    data.uID = APP_TRAY_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = callback_message;
    data.hIcon = current_icon();
    lstrcpynW(data.szTip, L"Offset Pad", ARRAYSIZE(data.szTip));
    tray_added = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (tray_added) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

void app_tray_init(HWND owner, UINT message, HICON idle_icon, HICON enabled_icon,
                   const app_tray_callbacks *callbacks)
{
    owner_window = owner;
    callback_message = message;
    inactive_icon = idle_icon;
    active_icon = enabled_icon;
    actions = *callbacks;
    add_tray();
}

void app_tray_shutdown(void)
{
    NOTIFYICONDATAW data = {0};
    if (!tray_added)
        return;
    data.cbSize = sizeof(data);
    data.hWnd = owner_window;
    data.uID = APP_TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &data);
    tray_added = 0;
}

void app_tray_refresh(void)
{
    NOTIFYICONDATAW data = {0};
    if (!tray_added)
        return;
    data.cbSize = sizeof(data);
    data.hWnd = owner_window;
    data.uID = APP_TRAY_ID;
    data.uFlags = NIF_TIP | NIF_ICON | NIF_SHOWTIP;
    data.hIcon = current_icon();
    lstrcpynW(data.szTip, L"Offset Pad", ARRAYSIZE(data.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void app_tray_show_mode_reminder(void)
{
    NOTIFYICONDATAW data = {0};
    wchar_t shortcut[96];
    update_notice_active = 0;
    if (!tray_added || !keymap_is_enabled())
        return;
    keymap_format_hotkey(shortcut, ARRAYSIZE(shortcut), keymap_get_hotkey());
    data.cbSize = sizeof(data);
    data.hWnd = owner_window;
    data.uID = APP_TRAY_ID;
    data.uFlags = NIF_INFO | NIF_REALTIME;
    data.dwInfoFlags = NIIF_INFO;
    lstrcpynW(data.szInfoTitle, L"Offset Pad", ARRAYSIZE(data.szInfoTitle));
    if (keymap_is_latched())
        swprintf(data.szInfo, ARRAYSIZE(data.szInfo),
                 L"当前已经处于小键盘模式。按 %ls 切回普通键盘。", shortcut);
    else
        lstrcpynW(data.szInfo,
                  L"当前处于临时小键盘模式。松开按住快捷键即可恢复普通键盘。",
                  ARRAYSIZE(data.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void app_tray_show_update_available(const wchar_t *version)
{
    NOTIFYICONDATAW data = {0};
    if (version == NULL || !tray_added)
        return;
    update_notice_active = 1;
    data.cbSize = sizeof(data);
    data.hWnd = owner_window;
    data.uID = APP_TRAY_ID;
    data.uFlags = NIF_INFO | NIF_REALTIME;
    data.dwInfoFlags = NIIF_INFO;
    lstrcpynW(data.szInfoTitle, L"Offset Pad 更新", ARRAYSIZE(data.szInfoTitle));
    swprintf(data.szInfo, ARRAYSIZE(data.szInfo),
             L"发现新版本 %ls。点击此通知查看 Gitee Releases。", version);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

static void show_tray_menu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT cursor;
    UINT choice;
    if (menu == NULL)
        return;
    AppendMenuW(menu, MF_STRING, MENU_OPEN, L"打开设置");
    AppendMenuW(menu, MF_STRING, MENU_TOGGLE,
                keymap_is_latched() ? L"关闭小键盘" : L"开启小键盘");
    AppendMenuW(menu, MF_STRING | (keymap_hotkeys_enabled() ? 0 : MF_CHECKED),
                MENU_DISABLE_HOTKEYS, L"禁用快捷键");
    AppendMenuW(menu, MF_STRING | (settings_autostart_enabled() ? MF_CHECKED : 0),
                MENU_AUTOSTART, L"开机时启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"退出程序");
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_window);
    choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                            cursor.x, cursor.y, 0, owner_window, NULL);
    PostMessageW(owner_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (choice) {
    case MENU_OPEN:
        actions.open_settings();
        break;
    case MENU_TOGGLE:
        actions.set_enabled(!keymap_is_latched());
        break;
    case MENU_DISABLE_HOTKEYS: {
        int enabled = !keymap_hotkeys_enabled();
        if (!settings_save_hotkeys_enabled(enabled))
            show_error(L"无法保存快捷键设置。");
        else
            keymap_set_hotkeys_enabled(enabled);
        actions.refresh();
        break;
    }
    case MENU_AUTOSTART:
        if (!settings_set_autostart(!settings_autostart_enabled()))
            show_error(L"无法保存开机时启动设置。");
        actions.refresh();
        break;
    case MENU_EXIT:
        actions.quit();
        break;
    }
}

void app_tray_handle_message(LPARAM message)
{
    UINT event = LOWORD(message);
    if (event == NIN_BALLOONUSERCLICK && update_notice_active) {
        update_notice_active = 0;
        ShellExecuteW(NULL, L"open",
                      L"https://gitee.com/OrlandoHsu29/offset-pad/releases",
                      NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    if (event == NIN_BALLOONHIDE || event == NIN_BALLOONTIMEOUT)
        update_notice_active = 0;
    if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
        show_tray_menu();
    else if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
        actions.open_settings();
}

void app_tray_taskbar_created(void)
{
    tray_added = 0;
    add_tray();
}
