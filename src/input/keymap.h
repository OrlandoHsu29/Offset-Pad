#ifndef OFFSET_PAD_KEYMAP_H
#define OFFSET_PAD_KEYMAP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#define KEYMAP_MOD_CTRL  1U
#define KEYMAP_MOD_ALT   2U
#define KEYMAP_MOD_SHIFT 4U
#define KEYMAP_MOD_WIN   8U
#define KEYMAP_KEY_COUNT 10
#define KEYMAP_CAPTURE_CANCELED 0
#define KEYMAP_CAPTURE_SAVED 1
#define KEYMAP_CAPTURE_INVALID 2

typedef struct keymap_hotkey {
    unsigned int modifiers;
    unsigned int key;
} keymap_hotkey;

int keymap_install(HINSTANCE instance, HWND notify_window, UINT changed_message);
void keymap_uninstall(void);
void keymap_set_enabled(int enabled);
int keymap_is_enabled(void);
void keymap_set_block_letters(int enabled);
int keymap_block_letters_enabled(void);
void keymap_set_hotkey(keymap_hotkey hotkey);
keymap_hotkey keymap_get_hotkey(void);
int keymap_set_sources(const DWORD sources[KEYMAP_KEY_COUNT]);
void keymap_get_sources(DWORD sources[KEYMAP_KEY_COUNT]);
DWORD keymap_get_source(size_t index);
int keymap_source_supported(DWORD source);
void keymap_format_source(wchar_t *buffer, size_t capacity, DWORD source);
void keymap_format_hotkey(wchar_t *buffer, size_t capacity, keymap_hotkey hotkey);
void keymap_begin_capture(void);
void keymap_cancel_capture(void);
int keymap_is_capturing(void);
void keymap_set_capture_message(UINT message);
void keymap_set_source_capture_message(UINT message);
void keymap_set_reminder_message(UINT message);
void keymap_begin_source_capture(size_t index);
int keymap_is_source_capturing(void);
size_t keymap_capturing_source(void);

#endif
