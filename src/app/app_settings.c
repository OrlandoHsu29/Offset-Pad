#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "app_settings.h"

#define PREFS_KEY L"Software\\Offset Pad"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define DEFAULT_HOTKEY ((DWORD)((KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT) << 16))

int settings_load_enabled(void)
{
    HKEY key;
    DWORD value = 0;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(key, L"NumpadEnabled", NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value))
        value = 0;
    RegCloseKey(key);
    return value != 0;
}

int settings_save_enabled(int enabled)
{
    HKEY key;
    DWORD value = enabled != 0;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, NULL, 0,
                                  KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegSetValueExW(key, L"NumpadEnabled", 0, REG_DWORD,
                            (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

keymap_hotkey settings_load_hotkey(void)
{
    HKEY key;
    DWORD value = DEFAULT_HOTKEY;
    DWORD type = 0;
    DWORD size = sizeof(value);
    keymap_hotkey result;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"ToggleHotkey", NULL, &type,
                             (BYTE *)&value, &size) != ERROR_SUCCESS ||
            type != REG_DWORD || size != sizeof(value))
            value = DEFAULT_HOTKEY;
        RegCloseKey(key);
    }
    result.modifiers = value >> 16;
    result.key = value & 0xFFFFU;
    return result;
}

int settings_save_hotkey(keymap_hotkey hotkey)
{
    HKEY key;
    DWORD value = (DWORD)((hotkey.modifiers << 16) | hotkey.key);
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, NULL, 0,
                                  KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegSetValueExW(key, L"ToggleHotkey", 0, REG_DWORD,
                            (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

int settings_autostart_enabled(void)
{
    HKEY key;
    DWORD type = 0;
    DWORD size = 0;
    LONG result;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    result = RegQueryValueExW(key, L"Offset Pad", NULL, &type, NULL, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ && size > sizeof(wchar_t);
}

int settings_set_autostart(int enabled)
{
    HKEY key;
    LONG result;
    if (!enabled) {
        result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND)
            return 1;
        if (result != ERROR_SUCCESS)
            return 0;
        result = RegDeleteValueW(key, L"Offset Pad");
        RegCloseKey(key);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    {
        wchar_t path[MAX_PATH];
        wchar_t command[MAX_PATH + 32];
        DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH ||
            swprintf(command, sizeof(command) / sizeof(command[0]),
                     L"\"%ls\" --background", path) < 0)
            return 0;
        result = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &key, NULL);
        if (result != ERROR_SUCCESS)
            return 0;
        result = RegSetValueExW(key, L"Offset Pad", 0, REG_SZ,
                                (const BYTE *)command,
                                (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }
}
