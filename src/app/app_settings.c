#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "app_settings.h"

#define PREFS_KEY L"Software\\Offset Pad"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define DEFAULT_HOTKEY ((DWORD)(((KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL) << 16)))
#define LEGACY_DEFAULT_HOTKEY ((DWORD)(((KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT) << 16)))
#define PREVIOUS_DEFAULT_HOTKEY ((DWORD)(((KEYMAP_MOD_CAPS | KEYMAP_MOD_LSHIFT) << 16)))
#define PREVIOUS_CTRL_DEFAULT_HOTKEY ((DWORD)(((KEYMAP_MOD_CAPS | KEYMAP_MOD_CTRL) << 16)))
#define DEFAULT_HOLD_HOTKEY ((DWORD)(KEYMAP_MOD_CAPS << 16))

static int unsafe_alt_caps_hotkey(DWORD stored)
{
    unsigned int modifiers = stored >> 16;
    return (modifiers & KEYMAP_MOD_ALT_ANY) != 0 &&
           (modifiers & KEYMAP_MOD_CAPS) != 0 &&
           (modifiers & KEYMAP_MOD_CTRL_ANY) == 0;
}

static DWORD load_dword(const wchar_t *name, DWORD fallback)
{
    HKEY key;
    DWORD value = fallback;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return fallback;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value))
        value = fallback;
    RegCloseKey(key);
    return value;
}

static int save_dword(const wchar_t *name, DWORD value)
{
    HKEY key;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, NULL, 0,
                                  KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegSetValueExW(key, name, 0, REG_DWORD,
                            (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static keymap_hotkey load_hotkey(const wchar_t *name, DWORD fallback)
{
    DWORD value = load_dword(name, fallback);
    keymap_hotkey result = {value >> 16, value & 0xFFFFU};
    return result;
}

static int save_hotkey(const wchar_t *name, keymap_hotkey value)
{
    return save_dword(name, (DWORD)((value.modifiers << 16) | value.key));
}

int settings_load_block_letters(void)
{
    return load_dword(L"BlockLetterInput", 1) != 0;
}

int settings_save_block_letters(int enabled)
{
    return save_dword(L"BlockLetterInput", enabled != 0);
}

int settings_load_hotkeys_enabled(void)
{
    return load_dword(L"HotkeysEnabled", 1) != 0;
}

int settings_save_hotkeys_enabled(int enabled)
{
    return save_dword(L"HotkeysEnabled", enabled != 0);
}

int settings_load_auto_updates(void)
{
    return load_dword(L"AutoUpdates", 1) != 0;
}

int settings_save_auto_updates(int enabled)
{
    return save_dword(L"AutoUpdates", enabled != 0);
}
#define UPDATE_CACHE_VALUE L"UpdateAvailable"
#define UPDATE_CACHE_FOR_VALUE L"UpdateAvailableForVersion"

static int load_registry_string(HKEY key, const wchar_t *name,
                                wchar_t *buffer, size_t capacity)
{
    DWORD type = 0;
    DWORD size = 0;
    size_t chars;
    if (RegQueryValueExW(key, name, NULL, &type, NULL, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size < sizeof(wchar_t) ||
        size % sizeof(wchar_t) != 0 || size > capacity * sizeof(wchar_t))
        return 0;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)buffer, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size < sizeof(wchar_t) || size % sizeof(wchar_t) != 0)
        return 0;
    chars = size / sizeof(wchar_t);
    return buffer[chars - 1] == L'\0' && wcslen(buffer) == chars - 1;
}

static void clear_update_cache(HKEY key)
{
    RegDeleteValueW(key, UPDATE_CACHE_VALUE);
    RegDeleteValueW(key, UPDATE_CACHE_FOR_VALUE);
}

int settings_update_was_available(const wchar_t *current_version)
{
    HKEY key;
    DWORD available = 0;
    DWORD type = 0;
    DWORD size = sizeof(available);
    wchar_t cached_for[32];
    int valid;
    if (current_version == NULL || wcslen(current_version) >= 32 ||
        RegOpenKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    valid = RegQueryValueExW(key, UPDATE_CACHE_VALUE, NULL, &type,
                             (BYTE *)&available, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && size == sizeof(available) && available == 1 &&
            load_registry_string(key, UPDATE_CACHE_FOR_VALUE, cached_for,
                                 sizeof(cached_for) / sizeof(cached_for[0])) &&
            wcscmp(cached_for, current_version) == 0;
    if (!valid)
        clear_update_cache(key);
    RegCloseKey(key);
    return valid;
}

int settings_cache_update_available(const wchar_t *current_version)
{
    HKEY key;
    DWORD available = 1;
    DWORD size;
    LONG result;
    size_t length;
    if (current_version == NULL || (length = wcslen(current_version)) == 0 || length >= 32)
        return 0;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, NULL, 0,
                             KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;
    size = (DWORD)((length + 1) * sizeof(wchar_t));
    result = RegSetValueExW(key, UPDATE_CACHE_FOR_VALUE, 0, REG_SZ,
                            (const BYTE *)current_version, size);
    if (result == ERROR_SUCCESS)
        result = RegSetValueExW(key, UPDATE_CACHE_VALUE, 0, REG_DWORD,
                                (const BYTE *)&available, sizeof(available));
    if (result != ERROR_SUCCESS)
        clear_update_cache(key);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
keymap_hotkey settings_default_hotkey(void)
{
    keymap_hotkey value = {DEFAULT_HOTKEY >> 16, DEFAULT_HOTKEY & 0xFFFFU};
    return value;
}

keymap_hotkey settings_load_hotkey(void)
{
    DWORD stored = load_dword(L"ToggleHotkey", DEFAULT_HOTKEY);
    if (stored == LEGACY_DEFAULT_HOTKEY || stored == PREVIOUS_DEFAULT_HOTKEY ||
        stored == PREVIOUS_CTRL_DEFAULT_HOTKEY || unsafe_alt_caps_hotkey(stored)) {
        stored = DEFAULT_HOTKEY;
        save_dword(L"ToggleHotkey", stored);
    }
    return (keymap_hotkey){stored >> 16, stored & 0xFFFFU};
}

int settings_save_hotkey(keymap_hotkey hotkey)
{
    return save_hotkey(L"ToggleHotkey", hotkey);
}

keymap_hotkey settings_default_hold_hotkey(void)
{
    keymap_hotkey value = {DEFAULT_HOLD_HOTKEY >> 16, DEFAULT_HOLD_HOTKEY & 0xFFFFU};
    return value;
}

keymap_hotkey settings_load_hold_hotkey(void)
{
    return load_hotkey(L"HoldHotkey", DEFAULT_HOLD_HOTKEY);
}

int settings_save_hold_hotkey(keymap_hotkey hotkey)
{
    return save_hotkey(L"HoldHotkey", hotkey);
}

int settings_load_sources(DWORD sources[KEYMAP_KEY_COUNT])
{
    HKEY key;
    DWORD stored[KEYMAP_KEY_COUNT + 1];
    DWORD type = 0;
    DWORD size = sizeof(stored);
    LONG result;
    size_t index;
    if (sources == NULL)
        return 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    result = RegQueryValueExW(key, L"SourceKeys", NULL, &type, (BYTE *)stored, &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_BINARY ||
        (size != sizeof(DWORD) * KEYMAP_KEY_COUNT && size != sizeof(stored)))
        return 0;
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index)
        sources[index] = stored[index];
    return 1;
}

int settings_save_sources(const DWORD sources[KEYMAP_KEY_COUNT])
{
    HKEY key;
    LONG result;
    if (sources == NULL)
        return 0;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, PREFS_KEY, 0, NULL, 0,
                             KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegSetValueExW(key, L"SourceKeys", 0, REG_BINARY,
                            (const BYTE *)sources, sizeof(DWORD) * KEYMAP_KEY_COUNT);
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
