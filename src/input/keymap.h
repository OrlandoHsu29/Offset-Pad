#ifndef OFFSET_PAD_KEYMAP_H
#define OFFSET_PAD_KEYMAP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#define KEYMAP_MOD_CTRL  1U
#define KEYMAP_MOD_ALT   2U
#define KEYMAP_MOD_SHIFT 4U
#define KEYMAP_MOD_WIN   8U

typedef struct keymap_hotkey {
    unsigned int modifiers;
    unsigned int key;
} keymap_hotkey;

int keymap_install(HINSTANCE instance, HWND notify_window, UINT changed_message);
void keymap_uninstall(void);
void keymap_set_enabled(int enabled);
int keymap_is_enabled(void);
void keymap_set_hotkey(keymap_hotkey hotkey);
keymap_hotkey keymap_get_hotkey(void);
void keymap_format_hotkey(wchar_t *buffer, size_t capacity, keymap_hotkey hotkey);
void keymap_begin_capture(void);
void keymap_cancel_capture(void);
int keymap_is_capturing(void);
void keymap_set_capture_message(UINT message);

#endif
