#ifndef OFFSET_PAD_SETTINGS_H
#define OFFSET_PAD_SETTINGS_H

#include "keymap.h"

int settings_load_enabled(void);
int settings_save_enabled(int enabled);
int settings_autostart_enabled(void);
int settings_set_autostart(int enabled);
keymap_hotkey settings_load_hotkey(void);
int settings_save_hotkey(keymap_hotkey hotkey);

#endif
