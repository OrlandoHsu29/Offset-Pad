#ifndef OFFSET_PAD_UI_H
#define OFFSET_PAD_UI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct app_ui_actions {
    void (*set_enabled)(int enabled);
    void (*quit)(void);
    void (*restart_background)(void);
} app_ui_actions;

int ui_init(HINSTANCE instance, HWND message_window, HICON light_icon,
            HICON dark_icon, HICON tray_o_icon, HICON tray_9_icon,
            const app_ui_actions *actions);
void ui_shutdown(void);
void ui_show(void);
void ui_refresh(void);
void ui_tray_message(LPARAM message);
void ui_taskbar_created(void);
int ui_handle_dialog_message(MSG *message);

#define WM_OFFSET_PAD_TRAY (WM_APP + 1)
#define WM_OFFSET_PAD_SHOW (WM_APP + 2)
#define WM_OFFSET_PAD_MODE_CHANGED (WM_APP + 3)
#define WM_OFFSET_PAD_HOTKEY_CAPTURE_DONE (WM_APP + 4)
#define OFFSET_PAD_MAIN_CLASS L"OffsetPadMessageWindow"

#endif
