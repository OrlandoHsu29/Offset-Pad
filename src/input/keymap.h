#ifndef OFFSET_PAD_KEYMAP_H
#define OFFSET_PAD_KEYMAP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#define KEYMAP_MOD_CTRL   1U
#define KEYMAP_MOD_ALT    2U
#define KEYMAP_MOD_SHIFT  4U
#define KEYMAP_MOD_WIN    8U
#define KEYMAP_MOD_CAPS   16U
#define KEYMAP_MOD_LCTRL  32U
#define KEYMAP_MOD_RCTRL  64U
#define KEYMAP_MOD_LALT   128U
#define KEYMAP_MOD_RALT   256U
#define KEYMAP_MOD_LSHIFT 512U
#define KEYMAP_MOD_RSHIFT 1024U
#define KEYMAP_MOD_LWIN   2048U
#define KEYMAP_MOD_RWIN   4096U
#define KEYMAP_MOD_CTRL_SIDES  (KEYMAP_MOD_LCTRL | KEYMAP_MOD_RCTRL)
#define KEYMAP_MOD_ALT_SIDES   (KEYMAP_MOD_LALT | KEYMAP_MOD_RALT)
#define KEYMAP_MOD_SHIFT_SIDES (KEYMAP_MOD_LSHIFT | KEYMAP_MOD_RSHIFT)
#define KEYMAP_MOD_WIN_SIDES   (KEYMAP_MOD_LWIN | KEYMAP_MOD_RWIN)
#define KEYMAP_MOD_CTRL_ANY  (KEYMAP_MOD_CTRL | KEYMAP_MOD_CTRL_SIDES)
#define KEYMAP_MOD_ALT_ANY   (KEYMAP_MOD_ALT | KEYMAP_MOD_ALT_SIDES)
#define KEYMAP_MOD_SHIFT_ANY (KEYMAP_MOD_SHIFT | KEYMAP_MOD_SHIFT_SIDES)
#define KEYMAP_MOD_WIN_ANY   (KEYMAP_MOD_WIN | KEYMAP_MOD_WIN_SIDES)
#define KEYMAP_MOD_SIDE_ALL  (KEYMAP_MOD_CTRL_SIDES | KEYMAP_MOD_ALT_SIDES | \
                              KEYMAP_MOD_SHIFT_SIDES | KEYMAP_MOD_WIN_SIDES)
#define KEYMAP_MOD_ALL       (KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT | \
                              KEYMAP_MOD_WIN | KEYMAP_MOD_CAPS | KEYMAP_MOD_SIDE_ALL)
_Static_assert(KEYMAP_MOD_ALL <= 0xFFFFU, "modifier flags must fit persisted DWORD");
#define KEYMAP_KEY_COUNT 11
#define KEYMAP_HOLD_RESOLVE_TIMER_ID 0x4F50U
#define KEYMAP_HOLD_RESOLVE_DELAY_MS 500U
#define KEYMAP_CAPTURE_CANCELED 0
#define KEYMAP_CAPTURE_SAVED 1
#define KEYMAP_CAPTURE_INVALID 2
#define KEYMAP_CAPTURE_INVALID_COUNT 3
#define KEYMAP_CAPTURE_INVALID_ALT 4

typedef struct keymap_hotkey {
    unsigned int modifiers;
    unsigned int key;
} keymap_hotkey;

int keymap_install(HINSTANCE instance, HWND notify_window, UINT changed_message);
void keymap_uninstall(void);
void keymap_set_enabled(int enabled);
int keymap_is_enabled(void);
int keymap_is_visual_enabled(void);
int keymap_is_latched(void);
void keymap_set_hotkeys_enabled(int enabled);
int keymap_hotkeys_enabled(void);
void keymap_set_hold_hotkey(keymap_hotkey hotkey);
keymap_hotkey keymap_get_hold_hotkey(void);
void keymap_set_hotkeys(keymap_hotkey hotkey, keymap_hotkey hold_hotkey);
void keymap_set_block_unmapped(int enabled);
int keymap_block_unmapped_enabled(void);
void keymap_set_hotkey(keymap_hotkey hotkey);
keymap_hotkey keymap_get_hotkey(void);
int keymap_set_sources(const DWORD sources[KEYMAP_KEY_COUNT]);
void keymap_get_sources(DWORD sources[KEYMAP_KEY_COUNT]);
void keymap_get_default_sources(DWORD sources[KEYMAP_KEY_COUNT]);
DWORD keymap_get_source(size_t index);
int keymap_source_supported(DWORD source);
void keymap_format_source(wchar_t *buffer, size_t capacity, DWORD source);
void keymap_format_hotkey(wchar_t *buffer, size_t capacity, keymap_hotkey hotkey);
void keymap_begin_capture(void);
void keymap_begin_hold_capture(void);
void keymap_cancel_capture(void);
int keymap_is_capturing(void);
int keymap_is_hold_capturing(void);
void keymap_set_capture_message(UINT message);
void keymap_set_hold_capture_message(UINT message);
void keymap_set_effective_changed_message(UINT message);
void keymap_set_source_capture_message(UINT message);
void keymap_set_reminder_message(UINT message);
void keymap_set_preview_message(UINT message);
void keymap_set_preview_enabled(int enabled);
void keymap_handle_timer(UINT_PTR timer_id);
void keymap_begin_source_capture(size_t index);
int keymap_is_source_capturing(void);
size_t keymap_capturing_source(void);

#endif
