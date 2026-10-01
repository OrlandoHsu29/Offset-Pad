#ifndef OFFSET_PAD_APP_TRAY_H
#define OFFSET_PAD_APP_TRAY_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct app_tray_callbacks {
    void (*open_settings)(void);
    void (*set_enabled)(int enabled);
    void (*quit)(void);
    void (*refresh)(void);
} app_tray_callbacks;

void app_tray_init(HWND owner, UINT callback_message, HICON inactive_icon,
                   HICON active_icon, const app_tray_callbacks *callbacks);
void app_tray_shutdown(void);
void app_tray_refresh(void);
void app_tray_show_mode_reminder(void);
void app_tray_show_update_available(const wchar_t *version);
const wchar_t *app_tray_update_url(void);
void app_tray_handle_message(LPARAM message);
void app_tray_taskbar_created(void);

#endif
