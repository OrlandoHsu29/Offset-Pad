#ifndef OFFSET_PAD_SETTINGS_H
#define OFFSET_PAD_SETTINGS_H

#include "keymap.h"

int settings_load_hotkeys_enabled(void);
int settings_save_hotkeys_enabled(int enabled);
int settings_load_auto_updates(void);
int settings_save_auto_updates(int enabled);
int settings_update_was_available(const wchar_t *current_version);
int settings_cache_update_available(const wchar_t *current_version);
int settings_load_block_letters(void);
int settings_save_block_letters(int enabled);
int settings_autostart_enabled(void);
int settings_set_autostart(int enabled);
keymap_hotkey settings_load_hotkey(void);
keymap_hotkey settings_default_hotkey(void);
int settings_save_hotkey(keymap_hotkey hotkey);
keymap_hotkey settings_load_hold_hotkey(void);
keymap_hotkey settings_default_hold_hotkey(void);
int settings_save_hold_hotkey(keymap_hotkey hotkey);
int settings_load_sources(DWORD sources[KEYMAP_KEY_COUNT]);
int settings_save_sources(const DWORD sources[KEYMAP_KEY_COUNT]);

#endif
