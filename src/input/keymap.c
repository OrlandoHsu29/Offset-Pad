#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "keymap.h"

#define REMINDER_PRESS_COUNT 3U
#define REMINDER_WINDOW_MS 1500ULL
#define REMINDER_COOLDOWN_MS 60000ULL

typedef struct mapped_key {
    DWORD source;
    WORD target;
    unsigned char down;
    unsigned char swallow_up;
    DWORD swallow_source;
} mapped_key;

static mapped_key keys[KEYMAP_KEY_COUNT] = {
    {'U', L'7', 0, 0, 0}, {'I', L'8', 0, 0, 0},
    {'O', L'9', 0, 0, 0}, {'J', L'4', 0, 0, 0},
    {'K', L'5', 0, 0, 0}, {'L', L'6', 0, 0, 0},
    {'N', L'1', 0, 0, 0}, {'M', L'2', 0, 0, 0},
    {VK_OEM_COMMA, L'3', 0, 0, 0}, {VK_SPACE, L'0', 0, 0, 0}
};

static HHOOK hook;
static HWND notify_window;
static UINT changed_message;
static UINT capture_message;
static UINT source_capture_message;
static UINT reminder_message;
static ULONGLONG reminder_window_start;
static ULONGLONG reminder_last_sent;
static unsigned int reminder_press_count;
static int reminder_ever_sent;
static int reminder_sent_this_activation;
static size_t source_capture_index = KEYMAP_KEY_COUNT;
static int enabled;
static int block_letters;
static unsigned int blocked_letter_keys;
static unsigned int passed_letter_keys;
static unsigned char modifiers[8];
static unsigned char captured_modifiers[8];
static unsigned int capture_modifiers_seen;
static DWORD captured_key;
static int capturing;
static int block_hotkey_until_clear;
static int hotkey_key_down;
static int chord_down;
static keymap_hotkey hotkey = {KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 0};

static unsigned int active_modifiers(void)
{
    unsigned int mask = 0;
    if (modifiers[0] || modifiers[1]) mask |= KEYMAP_MOD_CTRL;
    if (modifiers[2] || modifiers[3]) mask |= KEYMAP_MOD_ALT;
    if (modifiers[4] || modifiers[5]) mask |= KEYMAP_MOD_SHIFT;
    if (modifiers[6] || modifiers[7]) mask |= KEYMAP_MOD_WIN;
    return mask;
}

static void clear_hotkey_block_if_released(void)
{
    if (active_modifiers() == 0 && captured_key == 0)
        block_hotkey_until_clear = 0;
}

static void note_unmapped_letter_press(void)
{
    ULONGLONG now;
    if (reminder_sent_this_activation || reminder_message == 0 || notify_window == NULL)
        return;
    now = GetTickCount64();
    if (reminder_press_count == 0 || now - reminder_window_start > REMINDER_WINDOW_MS) {
        reminder_window_start = now;
        reminder_press_count = 1;
    } else {
        ++reminder_press_count;
    }
    if (reminder_press_count < REMINDER_PRESS_COUNT)
        return;
    reminder_press_count = 0;
    if (reminder_ever_sent && now - reminder_last_sent < REMINDER_COOLDOWN_MS)
        return;
    if (PostMessageW(notify_window, reminder_message, 0, 0)) {
        reminder_last_sent = now;
        reminder_ever_sent = 1;
        reminder_sent_this_activation = 1;
    }
}

static void send_digit(WORD digit)
{
    INPUT input[2] = {0};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wScan = digit;
    input[0].ki.dwFlags = KEYEVENTF_UNICODE;
    input[1] = input[0];
    input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(input[0]));
}

void keymap_set_enabled(int value)
{
    size_t index;
    value = value != 0;
    if (enabled == value)
        return;
    if (!value) {
        for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
            if (keys[index].down) {
                keys[index].down = 0;
                keys[index].swallow_up = 1;
                keys[index].swallow_source = keys[index].source;
            }
        }
    }
    enabled = value;
    reminder_press_count = 0;
    reminder_sent_this_activation = 0;
}

int keymap_is_enabled(void)
{
    return enabled;
}

void keymap_set_block_letters(int value)
{
    block_letters = value != 0;
}

int keymap_block_letters_enabled(void)
{
    return block_letters;
}

int keymap_source_supported(DWORD source)
{
    if ((source >= 'A' && source <= 'Z') ||
        (source >= '0' && source <= '9'))
        return 1;
    switch (source) {
    case VK_SPACE:
    case VK_CAPITAL:
    case VK_OEM_COMMA:
    case VK_OEM_PERIOD:
    case VK_OEM_1:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_OEM_MINUS:
    case VK_OEM_PLUS:
    case VK_OEM_102:
        return 1;
    default:
        return 0;
    }
}

void keymap_format_source(wchar_t *buffer, size_t capacity, DWORD source)
{
    const wchar_t *label = L"?";
    if (buffer == NULL || capacity == 0)
        return;
    buffer[0] = L'\0';
    if ((source >= 'A' && source <= 'Z') ||
        (source >= '0' && source <= '9')) {
        if (capacity > 1) {
            buffer[0] = (wchar_t)source;
            buffer[1] = L'\0';
        }
        return;
    }
    switch (source) {
    case VK_SPACE: label = L"空格"; break;
    case VK_CAPITAL: label = L"Caps"; break;
    case VK_OEM_COMMA: label = L","; break;
    case VK_OEM_PERIOD: label = L"."; break;
    case VK_OEM_1: label = L";"; break;
    case VK_OEM_2: label = L"/"; break;
    case VK_OEM_3: label = L"`"; break;
    case VK_OEM_4: label = L"["; break;
    case VK_OEM_5: label = L"\\"; break;
    case VK_OEM_6: label = L"]"; break;
    case VK_OEM_7: label = L"'"; break;
    case VK_OEM_MINUS: label = L"-"; break;
    case VK_OEM_PLUS: label = L"="; break;
    case VK_OEM_102: label = L"\\";
    }
    lstrcpynW(buffer, label, (int)capacity);
}

DWORD keymap_get_source(size_t index)
{
    return index < KEYMAP_KEY_COUNT ? keys[index].source : 0;
}

void keymap_get_sources(DWORD sources[KEYMAP_KEY_COUNT])
{
    size_t index;
    if (sources == NULL)
        return;
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index)
        sources[index] = keys[index].source;
}

int keymap_set_sources(const DWORD sources[KEYMAP_KEY_COUNT])
{
    size_t index;
    size_t other;
    if (sources == NULL)
        return 0;
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index) {
        if (!keymap_source_supported(sources[index]))
            return 0;
        for (other = 0; other < index; ++other)
            if (sources[index] == sources[other])
                return 0;
    }
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index) {
        if (keys[index].down) {
            keys[index].down = 0;
            keys[index].swallow_up = 1;
            keys[index].swallow_source = keys[index].source;
        }
        keys[index].source = sources[index];
    }
    return 1;
}

static void set_source(size_t index, DWORD source)
{
    size_t other;
    DWORD previous = keys[index].source;
    if (source == previous)
        return;
    for (other = 0; other < KEYMAP_KEY_COUNT; ++other) {
        if (other != index && keys[other].source == source) {
            keys[other].source = previous;
            break;
        }
    }
    keys[index].source = source;
}

static int source_in_use(DWORD source)
{
    size_t index;
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index)
        if (keys[index].source == source)
            return 1;
    return 0;
}

void keymap_set_hotkey(keymap_hotkey value)
{
    if ((value.modifiers & ~15U) != 0 || value.key > 255 ||
        (value.modifiers == 0 && value.key == 0) ||
        value.key == VK_ESCAPE || value.key == VK_SHIFT ||
        value.key == VK_CONTROL || value.key == VK_MENU ||
        value.key == VK_LSHIFT || value.key == VK_RSHIFT ||
        value.key == VK_LCONTROL || value.key == VK_RCONTROL ||
        value.key == VK_LMENU || value.key == VK_RMENU ||
        value.key == VK_LWIN || value.key == VK_RWIN ||
        (value.modifiers == 0 && source_in_use(value.key))) {
        value.modifiers = KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT;
        value.key = 0;
    }
    hotkey = value;
    chord_down = 0;
    hotkey_key_down = 0;
}

keymap_hotkey keymap_get_hotkey(void)
{
    return hotkey;
}

static void append_hotkey_part(wchar_t *buffer, size_t capacity, const wchar_t *part)
{
    size_t length = wcslen(buffer);
    size_t part_length = wcslen(part);
    if (length != 0) {
        if (length + 3 >= capacity)
            return;
        wmemcpy(buffer + length, L" + ", 3);
        length += 3;
        buffer[length] = L'\0';
    }
    if (part_length >= capacity - length)
        part_length = capacity - length - 1;
    wmemcpy(buffer + length, part, part_length);
    buffer[length + part_length] = L'\0';
}

void keymap_format_hotkey(wchar_t *buffer, size_t capacity, keymap_hotkey value)
{
    wchar_t name[64] = {0};
    UINT scan;
    if (buffer == NULL || capacity == 0)
        return;
    buffer[0] = L'\0';
    if (value.modifiers & KEYMAP_MOD_CTRL) append_hotkey_part(buffer, capacity, L"Ctrl");
    if (value.modifiers & KEYMAP_MOD_ALT) append_hotkey_part(buffer, capacity, L"Alt");
    if (value.modifiers & KEYMAP_MOD_SHIFT) append_hotkey_part(buffer, capacity, L"Shift");
    if (value.modifiers & KEYMAP_MOD_WIN) append_hotkey_part(buffer, capacity, L"Win");
    if (value.key == 0)
        return;
    if ((value.key >= 'A' && value.key <= 'Z') ||
        (value.key >= '0' && value.key <= '9')) {
        name[0] = (wchar_t)value.key;
        name[1] = L'\0';
    } else if (value.key >= VK_F1 && value.key <= VK_F24) {
        swprintf(name, sizeof(name) / sizeof(name[0]), L"F%u", value.key - VK_F1 + 1);
    } else if (value.key == VK_SPACE) {
        lstrcpynW(name, L"空格", (int)(sizeof(name) / sizeof(name[0])));
    } else if (value.key == VK_OEM_COMMA) {
        lstrcpynW(name, L",", (int)(sizeof(name) / sizeof(name[0])));
    } else {
        scan = MapVirtualKeyW(value.key, MAPVK_VK_TO_VSC);
        if (scan == 0 || GetKeyNameTextW((LONG)(scan << 16), name,
                                          (int)(sizeof(name) / sizeof(name[0]))) == 0)
            swprintf(name, sizeof(name) / sizeof(name[0]), L"VK %u", value.key);
    }
    append_hotkey_part(buffer, capacity, name);
}

void keymap_set_capture_message(UINT message)
{
    capture_message = message;
}

void keymap_set_source_capture_message(UINT message)
{
    source_capture_message = message;
}

void keymap_set_reminder_message(UINT message)
{
    reminder_message = message;
}

int keymap_is_capturing(void)
{
    return capturing;
}

int keymap_is_source_capturing(void)
{
    return source_capture_index < KEYMAP_KEY_COUNT;
}

size_t keymap_capturing_source(void)
{
    return source_capture_index;
}

static void release_mapped_keys(void)
{
    size_t index;
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index) {
        if (keys[index].down) {
            keys[index].down = 0;
            keys[index].swallow_up = 1;
            keys[index].swallow_source = keys[index].source;
        }
    }
}

void keymap_begin_capture(void)
{
    if (keymap_is_source_capturing())
        keymap_cancel_capture();
    release_mapped_keys();
    capturing = 1;
    capture_modifiers_seen = 0;
    block_hotkey_until_clear = 1;
}

void keymap_begin_source_capture(size_t index)
{
    if (index >= KEYMAP_KEY_COUNT)
        return;
    if (capturing || keymap_is_source_capturing())
        keymap_cancel_capture();
    release_mapped_keys();
    source_capture_index = index;
    capture_modifiers_seen = 0;
    block_hotkey_until_clear = 1;
}

void keymap_cancel_capture(void)
{
    int was_hotkey = capturing;
    int was_source = keymap_is_source_capturing();
    if (!was_hotkey && !was_source)
        return;
    capturing = 0;
    source_capture_index = KEYMAP_KEY_COUNT;
    capture_modifiers_seen = 0;
    block_hotkey_until_clear = 1;
    clear_hotkey_block_if_released();
    if (was_hotkey && capture_message != 0)
        PostMessageW(notify_window, capture_message, KEYMAP_CAPTURE_CANCELED, 0);
    if (was_source && source_capture_message != 0)
        PostMessageW(notify_window, source_capture_message, KEYMAP_CAPTURE_CANCELED, 0);
}

static void finish_capture(keymap_hotkey value)
{
    int conflict = value.modifiers == 0 && source_in_use(value.key);
    capturing = 0;
    capture_modifiers_seen = 0;
    if (!conflict)
        keymap_set_hotkey(value);
    block_hotkey_until_clear = 1;
    clear_hotkey_block_if_released();
    if (capture_message != 0)
        PostMessageW(notify_window, capture_message,
                     conflict ? KEYMAP_CAPTURE_INVALID : KEYMAP_CAPTURE_SAVED, 0);
}

static void finish_source_capture(DWORD source)
{
    int valid = keymap_source_supported(source) && active_modifiers() == 0 &&
                !(hotkey.modifiers == 0 && hotkey.key == source);
    size_t index = source_capture_index;
    source_capture_index = KEYMAP_KEY_COUNT;
    if (valid)
        set_source(index, source);
    block_hotkey_until_clear = 1;
    clear_hotkey_block_if_released();
    if (source_capture_message != 0)
        PostMessageW(notify_window, source_capture_message,
                     valid ? KEYMAP_CAPTURE_SAVED : KEYMAP_CAPTURE_INVALID, 0);
}
static int modifier_index(const KBDLLHOOKSTRUCT *event)
{
    switch (event->vkCode) {
    case VK_LCONTROL: return 0;
    case VK_RCONTROL: return 1;
    case VK_CONTROL: return (event->flags & LLKHF_EXTENDED) ? 1 : 0;
    case VK_LMENU: return 2;
    case VK_RMENU: return 3;
    case VK_MENU: return (event->flags & LLKHF_EXTENDED) ? 3 : 2;
    case VK_LSHIFT: return 4;
    case VK_RSHIFT: return 5;
    case VK_SHIFT: return event->scanCode == 0x36 ? 5 : 4;
    case VK_LWIN: return 6;
    case VK_RWIN: return 7;
    default: return -1;
    }
}

static LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM parameter)
{
    const KBDLLHOOKSTRUCT *event;
    int released;
    int modifier;
    unsigned int mask;
    size_t index;

    if (code != HC_ACTION)
        return CallNextHookEx(hook, code, message, parameter);
    event = (const KBDLLHOOKSTRUCT *)parameter;
    if (event->flags & LLKHF_INJECTED)
        return CallNextHookEx(hook, code, message, parameter);
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN &&
        message != WM_KEYUP && message != WM_SYSKEYUP)
        return CallNextHookEx(hook, code, message, parameter);

    released = message == WM_KEYUP || message == WM_SYSKEYUP;
    modifier = modifier_index(event);
    if (modifier >= 0) {
        modifiers[modifier] = !released;
        mask = active_modifiers();
        if (capturing) {
            if (!released) {
                capture_modifiers_seen |= mask;
                captured_modifiers[modifier] = 1;
                return 1;
            }
            if (captured_modifiers[modifier]) {
                keymap_hotkey captured = {capture_modifiers_seen, 0};
                captured_modifiers[modifier] = 0;
                if (captured.modifiers != 0)
                    finish_capture(captured);
                return 1;
            }
            return CallNextHookEx(hook, code, message, parameter);
        }
        if (keymap_is_source_capturing()) {
            if (!released) {
                captured_modifiers[modifier] = 1;
                return 1;
            }
            if (captured_modifiers[modifier]) {
                captured_modifiers[modifier] = 0;
                return 1;
            }
            return CallNextHookEx(hook, code, message, parameter);
        }
        if (captured_modifiers[modifier]) {
            if (released)
                captured_modifiers[modifier] = 0;
            clear_hotkey_block_if_released();
            return 1;
        }
        clear_hotkey_block_if_released();
        if (hotkey.key == 0 && hotkey.modifiers != 0) {
            if ((mask & hotkey.modifiers) != hotkey.modifiers)
                chord_down = 0;
            else if (!released && mask == hotkey.modifiers &&
                     !chord_down && !block_hotkey_until_clear) {
                chord_down = 1;
                keymap_set_enabled(!enabled);
                PostMessageW(notify_window, changed_message, 0, 0);
            }
        }
        return CallNextHookEx(hook, code, message, parameter);
    }

    if (event->vkCode >= 'A' && event->vkCode <= 'Z') {
        unsigned int bit = 1U << (event->vkCode - 'A');
        if (blocked_letter_keys & bit) {
            if (released)
                blocked_letter_keys &= ~bit;
            return 1;
        }
        if (passed_letter_keys & bit) {
            if (released)
                passed_letter_keys &= ~bit;
            return CallNextHookEx(hook, code, message, parameter);
        }
    }

    if (event->vkCode == captured_key && captured_key != 0) {
        if (released) {
            for (index = 0; index < KEYMAP_KEY_COUNT; ++index) {
                if (keys[index].swallow_up &&
                    keys[index].swallow_source == captured_key) {
                    keys[index].swallow_up = 0;
                    keys[index].swallow_source = 0;
                }
            }
            captured_key = 0;
            clear_hotkey_block_if_released();
        }
        return 1;
    }
    if (capturing) {
        if (!released) {
            keymap_hotkey captured = {active_modifiers(), event->vkCode};
            captured_key = event->vkCode;
            if (event->vkCode == VK_ESCAPE)
                keymap_cancel_capture();
            else
                finish_capture(captured);
            return 1;
        }
        return CallNextHookEx(hook, code, message, parameter);
    }
    if (keymap_is_source_capturing()) {
        if (!released) {
            captured_key = event->vkCode;
            if (event->vkCode == VK_ESCAPE)
                keymap_cancel_capture();
            else
                finish_source_capture(event->vkCode);
            return 1;
        }
        return CallNextHookEx(hook, code, message, parameter);
    }
    clear_hotkey_block_if_released();
    if (hotkey.key != 0 && event->vkCode == hotkey.key) {
        if (hotkey_key_down) {
            if (released)
                hotkey_key_down = 0;
            return 1;
        }
        if (!released && !block_hotkey_until_clear &&
            active_modifiers() == hotkey.modifiers) {
            hotkey_key_down = 1;
            keymap_set_enabled(!enabled);
            PostMessageW(notify_window, changed_message, 0, 0);
            return 1;
        }
    }

    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        mapped_key *key = &keys[index];
        if (key->swallow_up && key->swallow_source == event->vkCode) {
            if (released) {
                key->swallow_up = 0;
                key->swallow_source = 0;
            }
            return 1;
        }
        if (key->source != event->vkCode)
            continue;
        if (released) {
            if (key->down) {
                key->down = 0;
                return 1;
            }
        } else if (enabled) {
            key->down = 1;
            send_digit(key->target);
            return 1;
        }
        break;
    }
    if (!released && event->vkCode >= 'A' && event->vkCode <= 'Z') {
        unsigned int bit = 1U << (event->vkCode - 'A');
        if (enabled &&
            !(active_modifiers() & (KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_WIN))) {
            note_unmapped_letter_press();
            if (block_letters) {
                blocked_letter_keys |= bit;
                return 1;
            }
        }
        passed_letter_keys |= bit;
    }
    return CallNextHookEx(hook, code, message, parameter);
}

int keymap_install(HINSTANCE instance, HWND window, UINT message)
{
    notify_window = window;
    changed_message = message;
    hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, instance, 0);
    return hook != NULL;
}

void keymap_uninstall(void)
{
    capturing = 0;
    source_capture_index = KEYMAP_KEY_COUNT;
    keymap_set_enabled(0);
    if (hook != NULL) {
        UnhookWindowsHookEx(hook);
        hook = NULL;
    }
}
