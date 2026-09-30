#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define TEST_PREFS_KEY L"Software\\Offset Pad"
#define TEST_VALUE_COUNT 16
#define TEST_VALUE_SIZE 128
#define TEST_KEY ((HKEY)(ULONG_PTR)1)

typedef struct test_value {
    wchar_t name[32];
    DWORD type;
    DWORD size;
    BYTE data[TEST_VALUE_SIZE];
    int used;
} test_value;

static test_value values[TEST_VALUE_COUNT];
static int fail_next_write;

static LSTATUS WINAPI mock_RegOpenKeyExW(HKEY root, LPCWSTR subkey, DWORD options,
                                         REGSAM access, PHKEY result);
static LSTATUS WINAPI mock_RegCreateKeyExW(HKEY root, LPCWSTR subkey, DWORD reserved,
                                           LPWSTR class_name, DWORD options, REGSAM access,
                                           const LPSECURITY_ATTRIBUTES security,
                                           PHKEY result, LPDWORD disposition);
static LSTATUS WINAPI mock_RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved,
                                            LPDWORD type, LPBYTE data, LPDWORD size);
static LSTATUS WINAPI mock_RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved,
                                          DWORD type, const BYTE *data, DWORD size);
static LSTATUS WINAPI mock_RegDeleteValueW(HKEY key, LPCWSTR name);
static LSTATUS WINAPI mock_RegCloseKey(HKEY key);

#define RegOpenKeyExW mock_RegOpenKeyExW
#define RegCreateKeyExW mock_RegCreateKeyExW
#define RegQueryValueExW mock_RegQueryValueExW
#define RegSetValueExW mock_RegSetValueExW
#define RegDeleteValueW mock_RegDeleteValueW
#define RegCloseKey mock_RegCloseKey
#include "../src/app/app_settings.c"
#undef RegOpenKeyExW
#undef RegCreateKeyExW
#undef RegQueryValueExW
#undef RegSetValueExW
#undef RegDeleteValueW
#undef RegCloseKey

static test_value *find_value(LPCWSTR name)
{
    size_t index;
    for (index = 0; index < TEST_VALUE_COUNT; ++index)
        if (values[index].used && wcscmp(values[index].name, name) == 0)
            return &values[index];
    return NULL;
}

static test_value *get_or_create_value(LPCWSTR name)
{
    size_t index;
    test_value *value = find_value(name);
    if (value != NULL)
        return value;
    for (index = 0; index < TEST_VALUE_COUNT; ++index) {
        if (!values[index].used) {
            value = &values[index];
            {
                size_t length = wcslen(name);
                size_t capacity = sizeof(value->name) / sizeof(value->name[0]);
                if (length >= capacity)
                    length = capacity - 1;
                wmemcpy(value->name, name, length);
                value->name[length] = L'\0';
            }
            value->used = 1;
            return value;
        }
    }
    return NULL;
}

static void set_raw_value(LPCWSTR name, DWORD type, const BYTE *data, DWORD size)
{
    test_value *value = get_or_create_value(name);
    assert(value != NULL && size <= TEST_VALUE_SIZE);
    value->type = type;
    value->size = size;
    if (size != 0)
        memcpy(value->data, data, size);
}

static LSTATUS WINAPI mock_RegOpenKeyExW(HKEY root, LPCWSTR subkey, DWORD options,
                                         REGSAM access, PHKEY result)
{
    (void)options;
    (void)access;
    if (root != HKEY_CURRENT_USER || wcscmp(subkey, TEST_PREFS_KEY) != 0)
        return ERROR_FILE_NOT_FOUND;
    *result = TEST_KEY;
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI mock_RegCreateKeyExW(HKEY root, LPCWSTR subkey, DWORD reserved,
                                           LPWSTR class_name, DWORD options, REGSAM access,
                                           const LPSECURITY_ATTRIBUTES security,
                                           PHKEY result, LPDWORD disposition)
{
    (void)reserved;
    (void)class_name;
    (void)options;
    (void)access;
    (void)security;
    if (root != HKEY_CURRENT_USER || wcscmp(subkey, TEST_PREFS_KEY) != 0)
        return ERROR_ACCESS_DENIED;
    *result = TEST_KEY;
    if (disposition != NULL)
        *disposition = REG_OPENED_EXISTING_KEY;
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI mock_RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved,
                                            LPDWORD type, LPBYTE data, LPDWORD size)
{
    test_value *value;
    (void)reserved;
    if (key != TEST_KEY || size == NULL)
        return ERROR_INVALID_PARAMETER;
    value = find_value(name);
    if (value == NULL)
        return ERROR_FILE_NOT_FOUND;
    if (type != NULL)
        *type = value->type;
    if (data == NULL) {
        *size = value->size;
        return ERROR_SUCCESS;
    }
    if (*size < value->size) {
        *size = value->size;
        return ERROR_MORE_DATA;
    }
    if (value->size != 0)
        memcpy(data, value->data, value->size);
    *size = value->size;
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI mock_RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved,
                                          DWORD type, const BYTE *data, DWORD size)
{
    (void)reserved;
    if (key != TEST_KEY || size > TEST_VALUE_SIZE)
        return ERROR_INVALID_PARAMETER;
    if (fail_next_write) {
        fail_next_write = 0;
        return ERROR_ACCESS_DENIED;
    }
    set_raw_value(name, type, data, size);
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI mock_RegDeleteValueW(HKEY key, LPCWSTR name)
{
    test_value *value;
    if (key != TEST_KEY)
        return ERROR_INVALID_HANDLE;
    value = find_value(name);
    if (value == NULL)
        return ERROR_FILE_NOT_FOUND;
    ZeroMemory(value, sizeof(*value));
    return ERROR_SUCCESS;
}

static LSTATUS WINAPI mock_RegCloseKey(HKEY key)
{
    return key == TEST_KEY ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

int main(void)
{
    keymap_hotkey hotkey;
    DWORD malformed = 1;
    DWORD legacy_hotkey = (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT) << 16;
    DWORD previous_default_hotkey = (KEYMAP_MOD_CAPS | KEYMAP_MOD_LSHIFT) << 16;
    DWORD previous_ctrl_default_hotkey = (KEYMAP_MOD_CAPS | KEYMAP_MOD_CTRL) << 16;
    DWORD old_alt_caps_hotkey = (KEYMAP_MOD_CAPS | KEYMAP_MOD_ALT) << 16;
    DWORD previous_default_hold_hotkey = KEYMAP_MOD_CAPS << 16;

    ZeroMemory(values, sizeof(values));
    assert(settings_load_block_letters());
    assert(settings_load_hotkeys_enabled());
    assert(settings_default_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(settings_default_hold_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT));

    assert(!settings_update_was_available(L"0.2.6"));
    assert(settings_cache_update_available(L"0.2.6"));
    assert(settings_update_was_available(L"0.2.6"));
    assert(!settings_update_was_available(L"0.2.7"));
    assert(find_value(L"UpdateAvailable") == NULL);
    assert(find_value(L"UpdateAvailableForVersion") == NULL);
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);

    set_raw_value(L"ToggleHotkey", REG_DWORD,
                  (const BYTE *)&legacy_hotkey, sizeof(legacy_hotkey));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);
    {
        test_value *saved = find_value(L"ToggleHotkey");
        DWORD persisted;
        assert(saved != NULL && saved->size == sizeof(persisted));
        memcpy(&persisted, saved->data, sizeof(persisted));
        assert(persisted == ((KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL) << 16));
    }
    set_raw_value(L"ToggleHotkey", REG_DWORD,
                  (const BYTE *)&previous_default_hotkey, sizeof(previous_default_hotkey));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);
    {
        test_value *saved = find_value(L"ToggleHotkey");
        DWORD persisted;
        assert(saved != NULL && saved->size == sizeof(persisted));
        memcpy(&persisted, saved->data, sizeof(persisted));
        assert(persisted == ((KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL) << 16));
    }
    set_raw_value(L"ToggleHotkey", REG_DWORD,
                  (const BYTE *)&previous_ctrl_default_hotkey, sizeof(previous_ctrl_default_hotkey));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);
    set_raw_value(L"ToggleHotkey", REG_DWORD,
                  (const BYTE *)&old_alt_caps_hotkey, sizeof(old_alt_caps_hotkey));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);

    hotkey = settings_load_hold_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT));
    assert(hotkey.key == 0);
    set_raw_value(L"HoldHotkey", REG_DWORD,
                  (const BYTE *)&previous_default_hold_hotkey,
                  sizeof(previous_default_hold_hotkey));
    hotkey = settings_load_hold_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT));
    {
        test_value *saved = find_value(L"HoldHotkey");
        DWORD persisted;
        assert(saved != NULL && saved->size == sizeof(persisted));
        memcpy(&persisted, saved->data, sizeof(persisted));
        assert(persisted == ((KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT) << 16));
    }

    assert(settings_save_block_letters(0));
    assert(!settings_load_block_letters());
    assert(settings_save_block_letters(-1));
    assert(settings_load_block_letters());

    assert(settings_save_hotkeys_enabled(0));
    assert(!settings_load_hotkeys_enabled());
    assert(settings_save_hotkeys_enabled(5));
    assert(settings_load_hotkeys_enabled());

    hotkey.modifiers = KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT | KEYMAP_MOD_WIN;
    hotkey.key = 'K';
    assert(settings_save_hotkey(hotkey));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT | KEYMAP_MOD_WIN));
    assert(hotkey.key == 'K');

    hotkey.modifiers = KEYMAP_MOD_RALT | KEYMAP_MOD_LSHIFT;
    hotkey.key = VK_F12;
    assert(settings_save_hold_hotkey(hotkey));
    hotkey = settings_load_hold_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_RALT | KEYMAP_MOD_LSHIFT) &&
           hotkey.key == VK_F12);
    assert(settings_save_hold_hotkey((keymap_hotkey){0, 0}));
    hotkey = settings_load_hold_hotkey();
    assert(hotkey.modifiers == 0 && hotkey.key == 0);

    set_raw_value(L"BlockLetterInput", REG_SZ, (const BYTE *)&malformed, sizeof(malformed));
    assert(settings_load_block_letters());
    set_raw_value(L"HotkeysEnabled", REG_DWORD, (const BYTE *)&malformed, sizeof(malformed) - 1);
    assert(settings_load_hotkeys_enabled());
    set_raw_value(L"ToggleHotkey", REG_SZ, (const BYTE *)&malformed, sizeof(malformed));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);

    set_raw_value(L"ToggleHotkey", REG_DWORD,
                  (const BYTE *)&legacy_hotkey, sizeof(legacy_hotkey));
    hotkey = settings_load_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL));
    assert(hotkey.key == 0);
    {
        test_value *saved = find_value(L"ToggleHotkey");
        DWORD persisted;
        assert(saved != NULL && saved->size == sizeof(persisted));
        memcpy(&persisted, saved->data, sizeof(persisted));
        assert(persisted == ((KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL) << 16));
    }
    set_raw_value(L"HoldHotkey", REG_DWORD, (const BYTE *)&malformed, sizeof(malformed) + 1);
    hotkey = settings_load_hold_hotkey();
    assert(hotkey.modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT));
    assert(hotkey.key == 0);
    assert(settings_save_hotkeys_enabled(1));
    fail_next_write = 1;
    assert(!settings_save_hotkeys_enabled(0));
    assert(settings_load_hotkeys_enabled());

    puts("app_settings tests passed");
    return 0;
}