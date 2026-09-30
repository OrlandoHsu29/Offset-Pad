#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "keymap.h"

#define REMINDER_PRESS_COUNT 3U
#define REMINDER_WINDOW_MS 1500ULL
#define REMINDER_COOLDOWN_MS 10000ULL

enum {
    SIDE_MODIFIER_COUNT = 8,
    CAPS_MODIFIER_INDEX = 8,
    TRACKED_MODIFIER_COUNT = 9
};

static const WORD modifier_keys[SIDE_MODIFIER_COUNT] = {
    VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
    VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN
};

typedef struct mapped_key {
    DWORD source;
    WORD target;
    unsigned char down;
    unsigned char swallow_up;
    DWORD swallow_source;
    unsigned char preview_down;
} mapped_key;

typedef enum capture_mode {
    CAPTURE_NONE,
    CAPTURE_TOGGLE_HOTKEY,
    CAPTURE_HOLD_HOTKEY,
    CAPTURE_SOURCE_KEY
} capture_mode;

typedef enum caps_restore_phase {
    CAPS_RESTORE_IDLE,       /* No Caps press is being tracked. */
    CAPS_RESTORE_ARMED,      /* Caps down reached Windows. */
    CAPS_RESTORE_ON_RELEASE, /* A shortcut used that Caps press. */
    CAPS_RESTORE_TIMER       /* Caps is up; compensation is queued. */
} caps_restore_phase;

static mapped_key keys[KEYMAP_KEY_COUNT] = {
    {'U', L'7', 0, 0, 0, 0}, {'I', L'8', 0, 0, 0, 0},
    {'O', L'9', 0, 0, 0, 0}, {'J', L'4', 0, 0, 0, 0},
    {'K', L'5', 0, 0, 0, 0}, {'L', L'6', 0, 0, 0, 0},
    {'N', L'1', 0, 0, 0, 0}, {'M', L'2', 0, 0, 0, 0},
    {VK_OEM_COMMA, L'3', 0, 0, 0, 0}, {VK_SPACE, L'0', 0, 0, 0, 0}
};

static HHOOK hook;
static HWND notify_window;
static UINT changed_message;
static UINT capture_message;
static UINT hold_capture_message;
static UINT effective_changed_message;
static UINT source_capture_message;
static UINT reminder_message;
static UINT preview_message;
static int preview_enabled;
static ULONGLONG reminder_window_start;
static ULONGLONG reminder_last_sent;
static unsigned int reminder_press_count;
static int reminder_sent_in_mode;
static size_t source_capture_index = KEYMAP_KEY_COUNT;
static int enabled;
static int hotkeys_enabled = 1;
static int hold_active;
static int hold_key_down;
static int block_letters;
static unsigned int blocked_letter_keys;
static unsigned int passed_letter_keys;
static unsigned char modifiers[TRACKED_MODIFIER_COUNT];
static unsigned char captured_modifiers[TRACKED_MODIFIER_COUNT];
static unsigned int capture_modifiers_seen;
static DWORD capture_key_pending;
static keymap_hotkey capture_key_value;
static DWORD captured_key;
static capture_mode capture;
static int block_hotkey_until_clear;
static int hotkey_key_down;
static int chord_down;
static int hold_modifiers_suspended;
static int backspace_synthetic_down;
static unsigned char hold_swallowed_keys[32];
static unsigned char passed_keys[32];
static keymap_hotkey hotkey = {KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL, 0};
static keymap_hotkey hold_hotkey = {KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0};
static int hold_provisional;
static int hold_timer_active;
static int hold_provisional_was_latched;
static int hold_suppressed_until_release;
static caps_restore_phase caps_restore;

typedef struct shortcut_compensation {
    DWORD key;
    unsigned int trigger_key;
    unsigned int modifiers;
    int pending;
    int timer_active;
} shortcut_compensation;

static shortcut_compensation shift_compensation;

static void release_mapped_keys(void);

static unsigned int active_modifiers(void)
{
    unsigned int mask = 0;
    if (modifiers[0]) mask |= KEYMAP_MOD_LCTRL;
    if (modifiers[1]) mask |= KEYMAP_MOD_RCTRL;
    if (modifiers[2]) mask |= KEYMAP_MOD_LALT;
    if (modifiers[3]) mask |= KEYMAP_MOD_RALT;
    if (modifiers[4]) mask |= KEYMAP_MOD_LSHIFT;
    if (modifiers[5]) mask |= KEYMAP_MOD_RSHIFT;
    if (modifiers[6]) mask |= KEYMAP_MOD_LWIN;
    if (modifiers[7]) mask |= KEYMAP_MOD_RWIN;
    if (modifiers[8]) mask |= KEYMAP_MOD_CAPS;
    return mask;
}

static void sync_modifier_state_from_os(int current_modifier)
{
    size_t index;
    /* Injected releases during hold mode make async state differ from physical state. */
    if (hold_modifiers_suspended)
        return;
    /* Apps may consume physical modifier key-ups and replace them with injected events. */
    for (index = 0; index < SIDE_MODIFIER_COUNT; ++index) {
        if ((int)index == current_modifier)
            continue;
        modifiers[index] = (GetAsyncKeyState(modifier_keys[index]) & 0x8000) != 0;
    }
}

static int modifier_encoding_valid(unsigned int value)
{
    return !((value & KEYMAP_MOD_CTRL) && (value & KEYMAP_MOD_CTRL_SIDES)) &&
           !((value & KEYMAP_MOD_ALT) && (value & KEYMAP_MOD_ALT_SIDES)) &&
           !((value & KEYMAP_MOD_SHIFT) && (value & KEYMAP_MOD_SHIFT_SIDES)) &&
           !((value & KEYMAP_MOD_WIN) && (value & KEYMAP_MOD_WIN_SIDES));
}

static int modifier_family_equal(unsigned int active, unsigned int required,
                                 unsigned int generic, unsigned int sides)
{
    unsigned int active_sides = active & sides;
    if (required & generic)
        return active_sides != 0;
    return active_sides == (required & sides);
}

static int modifiers_equal(unsigned int active, unsigned int required)
{
    return modifier_family_equal(active, required, KEYMAP_MOD_CTRL, KEYMAP_MOD_CTRL_SIDES) &&
           modifier_family_equal(active, required, KEYMAP_MOD_ALT, KEYMAP_MOD_ALT_SIDES) &&
           modifier_family_equal(active, required, KEYMAP_MOD_SHIFT, KEYMAP_MOD_SHIFT_SIDES) &&
           modifier_family_equal(active, required, KEYMAP_MOD_WIN, KEYMAP_MOD_WIN_SIDES) &&
           ((active & KEYMAP_MOD_CAPS) != 0) == ((required & KEYMAP_MOD_CAPS) != 0);
}

static int modifier_family_contains(unsigned int active, unsigned int required,
                                    unsigned int generic, unsigned int sides)
{
    if (required & generic)
        return (active & sides) != 0;
    return (active & (required & sides)) == (required & sides);
}

static int modifiers_contain(unsigned int active, unsigned int required)
{
    return modifier_family_contains(active, required, KEYMAP_MOD_CTRL, KEYMAP_MOD_CTRL_SIDES) &&
           modifier_family_contains(active, required, KEYMAP_MOD_ALT, KEYMAP_MOD_ALT_SIDES) &&
           modifier_family_contains(active, required, KEYMAP_MOD_SHIFT, KEYMAP_MOD_SHIFT_SIDES) &&
           modifier_family_contains(active, required, KEYMAP_MOD_WIN, KEYMAP_MOD_WIN_SIDES) &&
           (!(required & KEYMAP_MOD_CAPS) || (active & KEYMAP_MOD_CAPS));
}

static int modifier_family_can_extend(unsigned int active, unsigned int required,
                                      unsigned int generic, unsigned int sides)
{
    if (required & generic)
        return 1;
    return ((active & sides) & ~(required & sides)) == 0;
}

static int modifiers_can_extend(unsigned int active, unsigned int required)
{
    return modifier_family_can_extend(active, required, KEYMAP_MOD_CTRL, KEYMAP_MOD_CTRL_SIDES) &&
           modifier_family_can_extend(active, required, KEYMAP_MOD_ALT, KEYMAP_MOD_ALT_SIDES) &&
           modifier_family_can_extend(active, required, KEYMAP_MOD_SHIFT, KEYMAP_MOD_SHIFT_SIDES) &&
           modifier_family_can_extend(active, required, KEYMAP_MOD_WIN, KEYMAP_MOD_WIN_SIDES) &&
           (!(active & KEYMAP_MOD_CAPS) || (required & KEYMAP_MOD_CAPS));
}

static int modifier_family_overlaps(unsigned int left, unsigned int right,
                                    unsigned int generic, unsigned int sides)
{
    unsigned int left_sides = left & sides;
    unsigned int right_sides = right & sides;
    int left_set = (left & generic) != 0 || left_sides != 0;
    int right_set = (right & generic) != 0 || right_sides != 0;
    if (!left_set || !right_set)
        return left_set == right_set;
    if ((left & generic) || (right & generic))
        return 1;
    return left_sides == right_sides;
}

static int modifier_requirements_overlap(unsigned int left, unsigned int right)
{
    return modifier_family_overlaps(left, right, KEYMAP_MOD_CTRL, KEYMAP_MOD_CTRL_SIDES) &&
           modifier_family_overlaps(left, right, KEYMAP_MOD_ALT, KEYMAP_MOD_ALT_SIDES) &&
           modifier_family_overlaps(left, right, KEYMAP_MOD_SHIFT, KEYMAP_MOD_SHIFT_SIDES) &&
           modifier_family_overlaps(left, right, KEYMAP_MOD_WIN, KEYMAP_MOD_WIN_SIDES) &&
           ((left & KEYMAP_MOD_CAPS) != 0) == ((right & KEYMAP_MOD_CAPS) != 0);
}

static void clear_hotkey_block_if_released(void)
{
    if (active_modifiers() == 0 && captured_key == 0)
        block_hotkey_until_clear = 0;
}

static int post_mode_reminder(ULONGLONG now)
{
    if (reminder_message == 0 || notify_window == NULL ||
        (reminder_sent_in_mode && now - reminder_last_sent < REMINDER_COOLDOWN_MS))
        return 0;
    if (!PostMessageW(notify_window, reminder_message, 0, 0))
        return 0;
    reminder_last_sent = now;
    reminder_sent_in_mode = 1;
    return 1;
}

static void note_unmapped_letter_press(void)
{
    ULONGLONG now;
    if (reminder_message == 0 || notify_window == NULL)
        return;
    now = GetTickCount64();
    if (reminder_sent_in_mode && now - reminder_last_sent < REMINDER_COOLDOWN_MS) {
        reminder_press_count = 0;
        return;
    }
    if (reminder_press_count == 0 || now - reminder_window_start > REMINDER_WINDOW_MS) {
        reminder_window_start = now;
        reminder_press_count = 1;
    } else {
        ++reminder_press_count;
    }
    if (reminder_press_count < REMINDER_PRESS_COUNT)
        return;
    reminder_press_count = 0;
    post_mode_reminder(now);
}

static void send_modifier_state(int key_up)
{
    INPUT input[SIDE_MODIFIER_COUNT] = {0};
    UINT count = 0;
    size_t index;
    for (index = 0; index < SIDE_MODIFIER_COUNT; ++index) {
        if (!modifiers[index])
            continue;
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = modifier_keys[index];
        if (index == 1 || index == 3 || index == 6 || index == 7)
            input[count].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        if (key_up)
            input[count].ki.dwFlags |= KEYEVENTF_KEYUP;
        ++count;
    }
    if (count != 0)
        SendInput(count, input, sizeof(input[0]));
}

static int hotkey_uses_caps(keymap_hotkey shortcut)
{
    return (shortcut.modifiers & KEYMAP_MOD_CAPS) != 0;
}

static int hotkey_uses_shift(keymap_hotkey shortcut)
{
    return (shortcut.modifiers & KEYMAP_MOD_SHIFT_ANY) != 0;
}

static int any_hotkey_uses_shift(void)
{
    return hotkey_uses_shift(hotkey) || hotkey_uses_shift(hold_hotkey);
}

static void clear_shift_compensation(void)
{
    if (shift_compensation.timer_active && notify_window != NULL)
        KillTimer(notify_window, KEYMAP_COMPENSATION_TIMER_ID);
    ZeroMemory(&shift_compensation, sizeof(shift_compensation));
}

static void remember_shift_key(int modifier)
{
    if (modifier < 4 || modifier > 5 || !hotkeys_enabled ||
        !any_hotkey_uses_shift() || shift_compensation.key != 0)
        return;
    shift_compensation.key = modifier_keys[modifier];
}

static void mark_shift_compensation_needed(keymap_hotkey shortcut)
{
    if (hotkey_uses_shift(shortcut) && shift_compensation.key != 0 &&
        !shift_compensation.pending) {
        shift_compensation.modifiers = shortcut.modifiers;
        shift_compensation.trigger_key = shortcut.key;
        shift_compensation.pending = 1;
    }
}

static int shortcut_modifiers_released(unsigned int active, unsigned int required)
{
    unsigned int required_ctrl = required & KEYMAP_MOD_CTRL_ANY;
    unsigned int required_alt = required & KEYMAP_MOD_ALT_ANY;
    unsigned int required_shift = required & KEYMAP_MOD_SHIFT_ANY;
    unsigned int required_win = required & KEYMAP_MOD_WIN_ANY;
    if ((required_ctrl && (active & KEYMAP_MOD_CTRL_SIDES)) ||
        (required_alt && (active & KEYMAP_MOD_ALT_SIDES)) ||
        (required_shift && (active & KEYMAP_MOD_SHIFT_SIDES)) ||
        (required_win && (active & KEYMAP_MOD_WIN_SIDES)))
        return 0;
    return !(required & KEYMAP_MOD_CAPS) || !(active & KEYMAP_MOD_CAPS);
}

static void complete_shift_compensation(void)
{
    INPUT input[2] = {{0}};
    DWORD key;
    if (!shift_compensation.pending || shift_compensation.key == 0)
        return;
    if (notify_window != NULL)
        KillTimer(notify_window, KEYMAP_COMPENSATION_TIMER_ID);
    key = shift_compensation.key;
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = (WORD)key;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    ZeroMemory(&shift_compensation, sizeof(shift_compensation));
    SendInput(2, input, sizeof(input[0]));
}

static void try_shift_compensation_after_release(void)
{
    if (!shift_compensation.pending || shift_compensation.timer_active ||
        (shift_compensation.trigger_key != 0 && (hotkey_key_down || hold_key_down)) ||
        !shortcut_modifiers_released(active_modifiers(), shift_compensation.modifiers))
        return;
    shift_compensation.timer_active = 1;
    if (notify_window == NULL ||
        SetTimer(notify_window, KEYMAP_COMPENSATION_TIMER_ID,
                 KEYMAP_COMPENSATION_DELAY_MS, NULL) == 0) {
        shift_compensation.timer_active = 0;
        complete_shift_compensation();
    }
}

static int any_hotkey_uses_caps(void)
{
    return hotkey_uses_caps(hotkey) || hotkey_uses_caps(hold_hotkey);
}

static void mark_caps_restore_needed(keymap_hotkey shortcut)
{
    if (hotkey_uses_caps(shortcut) && caps_restore == CAPS_RESTORE_ARMED)
        caps_restore = CAPS_RESTORE_ON_RELEASE;
}

static void restore_caps_lock_after_release(void);

static void start_caps_cleanup_timer(void)
{
    if (caps_restore != CAPS_RESTORE_ON_RELEASE)
        return;
    caps_restore = CAPS_RESTORE_TIMER;
    if (notify_window == NULL ||
        SetTimer(notify_window, KEYMAP_CAPS_RELEASE_TIMER_ID,
                 KEYMAP_CAPS_RELEASE_DELAY_MS, NULL) == 0) {
        caps_restore = CAPS_RESTORE_IDLE;
        restore_caps_lock_after_release();
    }
}

static void restore_caps_lock_after_release(void)
{
    INPUT input[2] = {0};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CAPITAL;
    input[1] = input[0];
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(input[0]));
}

static void complete_caps_restore(void)
{
    if (caps_restore != CAPS_RESTORE_TIMER)
        return;
    if (notify_window != NULL)
        KillTimer(notify_window, KEYMAP_CAPS_RELEASE_TIMER_ID);
    caps_restore = CAPS_RESTORE_IDLE;
    restore_caps_lock_after_release();
}

static int is_symbol_key(DWORD key)
{
    return (key >= VK_OEM_1 && key <= VK_OEM_8) || key == VK_OEM_102;
}


static int caps_press_was_passed(void)
{
    return caps_restore == CAPS_RESTORE_ARMED ||
           caps_restore == CAPS_RESTORE_ON_RELEASE;
}

static LRESULT pass_caps_key_up(int code, WPARAM message, LPARAM parameter)
{
    LRESULT result = CallNextHookEx(hook, code, message, parameter);
    if (caps_restore == CAPS_RESTORE_ON_RELEASE)
        start_caps_cleanup_timer();
    else
        caps_restore = CAPS_RESTORE_IDLE;
    return result;
}

static int hold_has_swallowed_modifier(void)
{
    size_t index;
    for (index = 0; index < SIDE_MODIFIER_COUNT; ++index) {
        unsigned char mask = (unsigned char)(1U << (modifier_keys[index] & 7U));
        if (hold_swallowed_keys[modifier_keys[index] >> 3] & mask)
            return 1;
    }
    return 0;
}

static int hold_key_is_swallowed(DWORD key)
{
    unsigned char mask;
    if (key >= 256)
        return 0;
    mask = (unsigned char)(1U << (key & 7U));
    return (hold_swallowed_keys[key >> 3] & mask) != 0;
}

static void send_backspace_event(int key_up)
{
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_BACK;
    if (key_up)
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

static void track_passed_key(DWORD key, int released)
{
    unsigned char mask;
    if (key >= 256)
        return;
    mask = (unsigned char)(1U << (key & 7U));
    if (released)
        passed_keys[key >> 3] &= (unsigned char)~mask;
    else
        passed_keys[key >> 3] |= mask;
}

static int hold_swallow_key(DWORD key, int released)
{
    unsigned char mask;
    int was_swallowed;
    if (key >= 256)
        return 0;
    mask = (unsigned char)(1U << (key & 7U));
    was_swallowed = hold_key_is_swallowed(key);
    if (released) {
        hold_swallowed_keys[key >> 3] &= (unsigned char)~mask;
        return was_swallowed;
    }
    hold_swallowed_keys[key >> 3] |= mask;
    return 1;
}

static int key_uses_extended_flag(DWORD key)
{
    switch (key) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT:
    case VK_UP: case VK_DOWN: case VK_NUMLOCK: case VK_DIVIDE:
    case VK_SNAPSHOT: case VK_APPS:
        return 1;
    default:
        return 0;
    }
}

static void release_passed_keys(void)
{
    INPUT input[256] = {0};
    UINT count = 0;
    DWORD key;
    for (key = 0; key < 256; ++key) {
        unsigned char mask = (unsigned char)(1U << (key & 7U));
        if (!(passed_keys[key >> 3] & mask))
            continue;
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = (WORD)key;
        input[count].ki.dwFlags = KEYEVENTF_KEYUP;
        if (key_uses_extended_flag(key))
            input[count].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        ++count;
        hold_swallowed_keys[key >> 3] |= mask;
    }
    if (count != 0)
        SendInput(count, input, sizeof(input[0]));
    ZeroMemory(passed_keys, sizeof(passed_keys));
    passed_letter_keys = 0;
}

static void sync_hold_input_layer(void)
{
    int should_suspend = hold_active || backspace_synthetic_down;
    if (should_suspend == hold_modifiers_suspended)
        return;
    if (should_suspend) {
        size_t index;
        for (index = 0; index < SIDE_MODIFIER_COUNT; ++index) {
            if (modifiers[index])
                hold_swallow_key(modifier_keys[index], 0);
        }
        send_modifier_state(1);
        release_passed_keys();
        hold_modifiers_suspended = 1;
    } else {
        /* Keep the OS modifier state neutral until each physical key is released. */
        hold_modifiers_suspended = 0;
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

static void effective_mode_changed(int was_enabled)
{
    int is_enabled = enabled || hold_active;
    if (was_enabled == is_enabled)
        return;
    if (!is_enabled)
        release_mapped_keys();
    reminder_press_count = 0;
    if (is_enabled)
        reminder_sent_in_mode = 0;
}

static void set_hold_active_internal(int value, int show_reminder)
{
    int was_enabled = keymap_is_enabled();
    value = value != 0;
    if (hold_active == value)
        return;
    if (value) {
        mark_caps_restore_needed(hold_hotkey);
        mark_shift_compensation_needed(hold_hotkey);
    }
    hold_active = value;
    sync_hold_input_layer();
    effective_mode_changed(was_enabled);
    if (value && was_enabled && show_reminder)
        post_mode_reminder(GetTickCount64());
    if (effective_changed_message != 0 && notify_window != NULL &&
        was_enabled != keymap_is_enabled())
        PostMessageW(notify_window, effective_changed_message, 0, 0);
}

static void set_hold_active(int value)
{
    set_hold_active_internal(value, 1);
}

static void stop_hold_resolution_timer(void)
{
    if (hold_timer_active && notify_window != NULL)
        KillTimer(notify_window, KEYMAP_HOLD_RESOLVE_TIMER_ID);
    hold_timer_active = 0;
}

static void clear_hold_provisional(void)
{
    stop_hold_resolution_timer();
    hold_provisional = 0;
    hold_provisional_was_latched = 0;
}

static int hold_hotkey_is_toggle_prefix(unsigned int active)
{
    return hold_hotkey.key == 0 && hotkey.key == 0 &&
           hold_hotkey.modifiers != 0 &&
           !modifiers_equal(active, hotkey.modifiers) &&
           modifiers_can_extend(active, hotkey.modifiers);
}

static void confirm_hold_provisional(void)
{
    int show_reminder = hold_provisional_was_latched;
    clear_hold_provisional();
    if (show_reminder && hold_active)
        post_mode_reminder(GetTickCount64());
}

static void begin_hold_provisional(void)
{
    if (hold_provisional)
        return;
    hold_provisional = 1;
    hold_provisional_was_latched = enabled;
    set_hold_active_internal(1, 0);
    if (notify_window != NULL &&
        SetTimer(notify_window, KEYMAP_HOLD_RESOLVE_TIMER_ID,
                 KEYMAP_HOLD_RESOLVE_DELAY_MS, NULL) != 0) {
        hold_timer_active = 1;
    } else {
        confirm_hold_provisional();
    }
}

void keymap_set_hotkeys_enabled(int value)
{
    hotkeys_enabled = value != 0;
    chord_down = 0;
    block_hotkey_until_clear = 1;
    if (!hotkeys_enabled) {
        clear_hold_provisional();
        hold_suppressed_until_release = 0;
        set_hold_active(0);
    }
    clear_hotkey_block_if_released();
}

int keymap_hotkeys_enabled(void)
{
    return hotkeys_enabled;
}

void keymap_set_enabled(int value)
{
    int was_enabled = keymap_is_enabled();
    enabled = value != 0;
    effective_mode_changed(was_enabled);
}

int keymap_is_enabled(void)
{
    return enabled || hold_active;
}

int keymap_is_visual_enabled(void)
{
    return keymap_is_enabled();
}

int keymap_is_latched(void)
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
    release_mapped_keys();
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index)
        keys[index].source = sources[index];
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

static unsigned int hotkey_key_count(keymap_hotkey value)
{
    unsigned int count = value.key != 0;
    unsigned int modifiers_left = value.modifiers;
    while (modifiers_left != 0) {
        count += modifiers_left & 1U;
        modifiers_left >>= 1;
    }
    return count;
}

static int only_modifier_families(unsigned int value, unsigned int allowed)
{
    return value != 0 && (value & ~allowed) == 0;
}

static int alt_shortcut_is_unsafe(keymap_hotkey value)
{
    /* Ctrl-containing Alt chords are allowed; only the known disruptive forms are blocked. */
    if ((value.modifiers & KEYMAP_MOD_CTRL_ANY) != 0)
        return 0;
    if ((value.modifiers & KEYMAP_MOD_ALT_ANY) != 0 &&
        (value.modifiers & KEYMAP_MOD_CAPS) != 0)
        return 1;
    return (value.key != 0 &&
            only_modifier_families(value.modifiers, KEYMAP_MOD_ALT_ANY)) ||
           (value.key == 0 &&
            only_modifier_families(value.modifiers,
                                   KEYMAP_MOD_ALT_ANY | KEYMAP_MOD_SHIFT_ANY) &&
            (value.modifiers & KEYMAP_MOD_ALT_ANY) != 0 &&
            (value.modifiers & KEYMAP_MOD_SHIFT_ANY) != 0);
}

static int validate_hotkey(keymap_hotkey value)
{
    unsigned int key_count;
    if ((value.modifiers & ~KEYMAP_MOD_ALL) != 0 || value.key > 255 ||
        !modifier_encoding_valid(value.modifiers))
        return KEYMAP_CAPTURE_INVALID;
    key_count = hotkey_key_count(value);
    if (key_count < 2 || key_count > 4 ||
        value.key == VK_ESCAPE || value.key == VK_SHIFT ||
        value.key == VK_CONTROL || value.key == VK_MENU ||
        value.key == VK_LSHIFT || value.key == VK_RSHIFT ||
        value.key == VK_LCONTROL || value.key == VK_RCONTROL ||
        value.key == VK_LMENU || value.key == VK_RMENU ||
        value.key == VK_LWIN || value.key == VK_RWIN ||
        (value.modifiers == 0 && source_in_use(value.key)))
        return key_count < 2 || key_count > 4 ?
               KEYMAP_CAPTURE_INVALID_COUNT : KEYMAP_CAPTURE_INVALID;
    if (alt_shortcut_is_unsafe(value))
        return KEYMAP_CAPTURE_INVALID_ALT;
    return KEYMAP_CAPTURE_SAVED;
}

static int hotkey_valid(keymap_hotkey value)
{
    return validate_hotkey(value) == KEYMAP_CAPTURE_SAVED;
}

static int hold_hotkey_valid(keymap_hotkey value)
{
    return hotkey_valid(value);
}

static int same_hotkey(keymap_hotkey left, keymap_hotkey right)
{
    return left.key == right.key &&
           modifier_requirements_overlap(left.modifiers, right.modifiers);
}

void keymap_set_hotkey(keymap_hotkey value)
{
    int empty = value.modifiers == 0 && value.key == 0;
    if (!empty && same_hotkey(value, hold_hotkey))
        return;
    if (!empty && !hotkey_valid(value)) {
        value.modifiers = KEYMAP_MOD_SHIFT;
        value.key = VK_SPACE;
    }
    clear_hold_provisional();
    hold_suppressed_until_release = 0;
    hotkey = value;
    chord_down = 0;
    hotkey_key_down = 0;
}

keymap_hotkey keymap_get_hotkey(void)
{
    return hotkey;
}

void keymap_set_hold_hotkey(keymap_hotkey value)
{
    if ((value.modifiers != 0 || value.key != 0) &&
        (!hold_hotkey_valid(value) || same_hotkey(value, hotkey))) {
        value = (keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0};
        if (same_hotkey(value, hotkey)) {
            value.modifiers = 0;
            value.key = 0;
        }
    }
    clear_hold_provisional();
    hold_suppressed_until_release = 0;
    set_hold_active(0);
    hold_key_down = 0;
    hold_hotkey = value;
}

keymap_hotkey keymap_get_hold_hotkey(void)
{
    return hold_hotkey;
}

void keymap_set_hotkeys(keymap_hotkey value, keymap_hotkey hold_value)
{
    keymap_set_hold_hotkey((keymap_hotkey){0, 0});
    keymap_set_hotkey(value);
    keymap_set_hold_hotkey(hold_value);
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
    if (value.modifiers & KEYMAP_MOD_CAPS) append_hotkey_part(buffer, capacity, L"Caps");
    if (value.modifiers & KEYMAP_MOD_CTRL) append_hotkey_part(buffer, capacity, L"Ctrl");
    if (value.modifiers & KEYMAP_MOD_LCTRL) append_hotkey_part(buffer, capacity, L"LCtrl");
    if (value.modifiers & KEYMAP_MOD_RCTRL) append_hotkey_part(buffer, capacity, L"RCtrl");
    if (value.modifiers & KEYMAP_MOD_ALT) append_hotkey_part(buffer, capacity, L"Alt");
    if (value.modifiers & KEYMAP_MOD_LALT) append_hotkey_part(buffer, capacity, L"LAlt");
    if (value.modifiers & KEYMAP_MOD_RALT) append_hotkey_part(buffer, capacity, L"RAlt");
    if (value.modifiers & KEYMAP_MOD_SHIFT) append_hotkey_part(buffer, capacity, L"Shift");
    if (value.modifiers & KEYMAP_MOD_LSHIFT) append_hotkey_part(buffer, capacity, L"LShift");
    if (value.modifiers & KEYMAP_MOD_RSHIFT) append_hotkey_part(buffer, capacity, L"RShift");
    if (value.modifiers & KEYMAP_MOD_WIN) append_hotkey_part(buffer, capacity, L"Win");
    if (value.modifiers & KEYMAP_MOD_LWIN) append_hotkey_part(buffer, capacity, L"LWin");
    if (value.modifiers & KEYMAP_MOD_RWIN) append_hotkey_part(buffer, capacity, L"RWin");
    if (value.key == 0)
        return;
    if ((value.key >= 'A' && value.key <= 'Z') ||
        (value.key >= '0' && value.key <= '9')) {
        name[0] = (wchar_t)value.key;
        name[1] = L'\0';
    } else if (value.key >= VK_F1 && value.key <= VK_F24) {
        swprintf(name, sizeof(name) / sizeof(name[0]), L"F%u", value.key - VK_F1 + 1);
    } else if (value.key == VK_SPACE) {
        lstrcpynW(name, L"Space", (int)(sizeof(name) / sizeof(name[0])));
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

void keymap_set_hold_capture_message(UINT message)
{
    hold_capture_message = message;
}

void keymap_set_effective_changed_message(UINT message)
{
    effective_changed_message = message;
}

void keymap_set_source_capture_message(UINT message)
{
    source_capture_message = message;
}

void keymap_set_reminder_message(UINT message)
{
    reminder_message = message;
}

void keymap_set_preview_message(UINT message)
{
    preview_message = message;
}

void keymap_set_preview_enabled(int value)
{
    size_t index;
    preview_enabled = value != 0;
    if (!preview_enabled) {
        for (index = 0; index < KEYMAP_KEY_COUNT; ++index)
            keys[index].preview_down = 0;
    }
}

static int hotkey_capture_active(void)
{
    return capture == CAPTURE_TOGGLE_HOTKEY || capture == CAPTURE_HOLD_HOTKEY;
}

static int capture_modifier_is_down(void)
{
    size_t index;
    for (index = 0; index < TRACKED_MODIFIER_COUNT; ++index) {
        if (captured_modifiers[index])
            return 1;
    }
    return 0;
}

int keymap_is_capturing(void)
{
    return capture == CAPTURE_TOGGLE_HOTKEY;
}

int keymap_is_hold_capturing(void)
{
    return capture == CAPTURE_HOLD_HOTKEY;
}

int keymap_is_source_capturing(void)
{
    return capture == CAPTURE_SOURCE_KEY;
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

static void begin_capture(capture_mode mode, size_t source_index)
{
    if (capture != CAPTURE_NONE)
        keymap_cancel_capture();
    release_mapped_keys();
    clear_hold_provisional();
    hold_suppressed_until_release = 0;
    set_hold_active(0);
    hold_key_down = 0;
    capture = mode;
    source_capture_index = source_index;
    capture_modifiers_seen = 0;
    capture_key_pending = 0;
    ZeroMemory(&capture_key_value, sizeof(capture_key_value));
    block_hotkey_until_clear = 1;
}

void keymap_begin_capture(void)
{
    begin_capture(CAPTURE_TOGGLE_HOTKEY, KEYMAP_KEY_COUNT);
}

void keymap_begin_hold_capture(void)
{
    begin_capture(CAPTURE_HOLD_HOTKEY, KEYMAP_KEY_COUNT);
}

void keymap_begin_source_capture(size_t index)
{
    if (index < KEYMAP_KEY_COUNT)
        begin_capture(CAPTURE_SOURCE_KEY, index);
}

void keymap_cancel_capture(void)
{
    capture_mode previous = capture;
    if (previous == CAPTURE_NONE)
        return;
    capture = CAPTURE_NONE;
    source_capture_index = KEYMAP_KEY_COUNT;
    capture_modifiers_seen = 0;
    capture_key_pending = 0;
    ZeroMemory(&capture_key_value, sizeof(capture_key_value));
    block_hotkey_until_clear = 1;
    clear_hotkey_block_if_released();
    if (previous == CAPTURE_TOGGLE_HOTKEY && capture_message != 0)
        PostMessageW(notify_window, capture_message, KEYMAP_CAPTURE_CANCELED, 0);
    if (previous == CAPTURE_HOLD_HOTKEY && hold_capture_message != 0)
        PostMessageW(notify_window, hold_capture_message, KEYMAP_CAPTURE_CANCELED, 0);
    if (previous == CAPTURE_SOURCE_KEY && source_capture_message != 0)
        PostMessageW(notify_window, source_capture_message, KEYMAP_CAPTURE_CANCELED, 0);
}

static void finish_capture(keymap_hotkey value)
{
    int is_hold = capture == CAPTURE_HOLD_HOTKEY;
    int empty = value.modifiers == 0 && value.key == 0;
    int result = empty ? KEYMAP_CAPTURE_SAVED : validate_hotkey(value);
    UINT message = is_hold ? hold_capture_message : capture_message;
    if (result == KEYMAP_CAPTURE_SAVED && !empty &&
        (is_hold ? same_hotkey(value, hotkey) :
         same_hotkey(value, hold_hotkey)))
        result = KEYMAP_CAPTURE_INVALID;
    capture = CAPTURE_NONE;
    capture_modifiers_seen = 0;
    capture_key_pending = 0;
    ZeroMemory(&capture_key_value, sizeof(capture_key_value));
    if (result == KEYMAP_CAPTURE_SAVED) {
        if (is_hold)
            keymap_set_hold_hotkey(value);
        else
            keymap_set_hotkey(value);
    }
    block_hotkey_until_clear = 1;
    clear_hotkey_block_if_released();
    if (message != 0)
        PostMessageW(notify_window, message,
                     result, 0);
}

static void finish_source_capture(DWORD source)
{
    int valid = keymap_source_supported(source) && active_modifiers() == 0 &&
                !(hotkey.modifiers == 0 && hotkey.key == source) &&
                !(hold_hotkey.modifiers == 0 && hold_hotkey.key == source);
    size_t index = source_capture_index;
    capture = CAPTURE_NONE;
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
    case VK_CAPITAL: return 8;
    default: return -1;
    }
}

static LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM parameter)
{
    const KBDLLHOOKSTRUCT *event;
    int released;
    int modifier;
    int was_hold_suspended;
    int hold_swallowed;
    int caps_key_was_passed;
    int caps_shortcut_triggered = 0;
    int shift_shortcut_triggered = 0;

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
    if (modifier == CAPS_MODIFIER_INDEX && !released &&
        caps_restore == CAPS_RESTORE_TIMER)
        complete_caps_restore();
    if (modifier == CAPS_MODIFIER_INDEX &&
        caps_restore == CAPS_RESTORE_IDLE &&
        (!hotkeys_enabled || !any_hotkey_uses_caps()) &&
        !hotkey_capture_active() &&
        !captured_modifiers[CAPS_MODIFIER_INDEX] &&
        !hold_key_is_swallowed(VK_CAPITAL))
        return CallNextHookEx(hook, code, message, parameter);
    if (modifier >= 0) {
        if ((modifier == 4 || modifier == 5) && !released &&
            capture == CAPTURE_NONE && !keymap_is_source_capturing())
            remember_shift_key(modifier);
        sync_modifier_state_from_os(modifier);
        was_hold_suspended = hold_modifiers_suspended;
        caps_key_was_passed = modifier == CAPS_MODIFIER_INDEX && released &&
                              caps_press_was_passed();
        hold_swallowed = released ? hold_swallow_key(event->vkCode, 1) : 0;
        modifiers[modifier] = !released;
        mask = active_modifiers();
        if (released)
            try_shift_compensation_after_release();
        if (caps_key_was_passed && capture != CAPTURE_NONE) {
            clear_hotkey_block_if_released();
            return pass_caps_key_up(code, message, parameter);
        }
        if (hotkey_capture_active()) {
            if (!released) {
                capture_modifiers_seen |= mask;
                captured_modifiers[modifier] = 1;
                return 1;
            }
            if (captured_modifiers[modifier]) {
                captured_modifiers[modifier] = 0;
                if (capture_key_pending == 0 && !capture_modifier_is_down() &&
                    capture_modifiers_seen != 0) {
                    keymap_hotkey captured = {capture_modifiers_seen, 0};
                    finish_capture(captured);
                }
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
        if (hold_suppressed_until_release && hold_hotkey.modifiers != 0 &&
            !modifiers_contain(mask, hold_hotkey.modifiers))
            hold_suppressed_until_release = 0;
        if (hold_provisional) {
            if (released &&
                !modifiers_contain(mask, hold_hotkey.modifiers)) {
                clear_hold_provisional();
                set_hold_active(0);
            }
        }
        if (!hold_provisional) {
            if (hold_hotkey.key == 0) {
                int should_hold = hotkeys_enabled && hold_hotkey.modifiers != 0 &&
                                  modifiers_equal(mask, hold_hotkey.modifiers) &&
                                  !block_hotkey_until_clear &&
                                  !hold_suppressed_until_release;
                if (should_hold && hold_hotkey_is_toggle_prefix(mask))
                    begin_hold_provisional();
                else {
                    set_hold_active(should_hold);
                    if (should_hold && hold_hotkey.key == 0) {
                        if ((modifier == 4 || modifier == 5) && !released)
                            clear_shift_compensation();
                        else
                            mark_shift_compensation_needed(hold_hotkey);
                        mark_caps_restore_needed(hold_hotkey);
                    }
                }
            } else if (hold_key_down) {
                set_hold_active(hotkeys_enabled && modifiers_equal(mask, hold_hotkey.modifiers));
            }
        }

        if (hotkey.key == 0 && hotkey.modifiers != 0) {
            if (!modifiers_contain(mask, hotkey.modifiers))
                chord_down = 0;
            else if (hotkeys_enabled && (!hold_active || hold_provisional) && !released &&
                     (modifiers_equal(mask, hotkey.modifiers) ||
                      (hold_provisional &&
                       modifiers_contain(mask, hotkey.modifiers))) && !chord_down &&
                     !block_hotkey_until_clear) {
                chord_down = 1;
                if (hold_provisional) {
                    clear_hold_provisional();
                    hold_suppressed_until_release = 1;
                    keymap_set_enabled(!enabled);
                    set_hold_active(0);
                } else {
                    keymap_set_enabled(!enabled);
                }
                if ((modifier == 4 || modifier == 5) && !released &&
                    caps_restore == CAPS_RESTORE_ARMED &&
                    (hotkey.modifiers & KEYMAP_MOD_CAPS) != 0 &&
                    (hotkey.modifiers & KEYMAP_MOD_SHIFT_ANY) != 0) {
                    /* Caps already reached Windows; swallow Shift completing its shortcut. */
                    shift_shortcut_triggered = 1;
                    clear_shift_compensation();
                }
                if ((hotkey.modifiers & KEYMAP_MOD_CAPS) != 0) {
                    mark_caps_restore_needed(hotkey);
                    if (modifier == CAPS_MODIFIER_INDEX && !released)
                        caps_shortcut_triggered = 1;
                }
                if (!shift_shortcut_triggered)
                    mark_shift_compensation_needed(hotkey);
                PostMessageW(notify_window, changed_message, 0, 0);
            }
        }
        if (caps_key_was_passed) {
            try_shift_compensation_after_release();
            return pass_caps_key_up(code, message, parameter);
        }
        if (caps_shortcut_triggered) {
            captured_modifiers[CAPS_MODIFIER_INDEX] = 1;
            return 1;
        }
        if (shift_shortcut_triggered) {
            captured_modifiers[modifier] = 1;
            return 1;
        }
        if (hold_swallowed || was_hold_suspended || hold_modifiers_suspended ||
            hold_has_swallowed_modifier()) {
            if (!released)
                hold_swallow_key(event->vkCode, 0);
            return 1;
        }
        if (modifier == CAPS_MODIFIER_INDEX && !released &&
            hotkeys_enabled && any_hotkey_uses_caps()) {
            caps_restore = CAPS_RESTORE_ARMED;
        }
        try_shift_compensation_after_release();
        if ((modifier == 4 || modifier == 5) && released &&
            !shift_compensation.pending &&
            !(active_modifiers() & KEYMAP_MOD_SHIFT_SIDES))
            clear_shift_compensation();
        return CallNextHookEx(hook, code, message, parameter);
    }

    if (hold_provisional) {
        if (hotkeys_enabled && !block_hotkey_until_clear && hold_active &&
            modifiers_contain(active_modifiers(), hold_hotkey.modifiers)) {
            confirm_hold_provisional();
        } else {
            clear_hold_provisional();
            set_hold_active(0);
        }
    }
    if (event->vkCode == VK_BACK && (hold_active || backspace_synthetic_down)) {
        if (!released && hold_active) {
            backspace_synthetic_down = 1;
            sync_hold_input_layer();
            send_backspace_event(0);
        } else if (released && backspace_synthetic_down) {
            send_backspace_event(1);
            backspace_synthetic_down = 0;
            sync_hold_input_layer();
        }
        return 1;
    }

    if (capture_key_pending != 0) {
        if (event->vkCode == capture_key_pending && released) {
            keymap_hotkey captured = capture_key_value;
            capture_key_pending = 0;
            if (captured_key == event->vkCode)
                captured_key = 0;
            ZeroMemory(&capture_key_value, sizeof(capture_key_value));
            if (hotkey_capture_active())
                finish_capture(captured);
            return 1;
        }
        if (hotkey_capture_active())
            return 1;
    }

    if (preview_enabled && !hotkey_capture_active() && !keymap_is_source_capturing()) {
        for (index = 0; index < KEYMAP_KEY_COUNT; ++index) {
            if (keys[index].source != event->vkCode)
                continue;
            if (released && keys[index].preview_down) {
                keys[index].preview_down = 0;
                if (preview_message != 0 && notify_window != NULL)
                    PostMessageW(notify_window, preview_message, index, 0);
            } else if (!released && !keys[index].preview_down &&
                       (active_modifiers() == 0 || hold_active)) {
                keys[index].preview_down = 1;
                if (preview_message != 0 && notify_window != NULL)
                    PostMessageW(notify_window, preview_message, index, 1);
            }
            break;
        }
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
            track_passed_key(event->vkCode, released);
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
    if (hotkey_capture_active()) {
        if (!released) {
            keymap_hotkey captured = {capture_modifiers_seen | active_modifiers(),
                                      event->vkCode};
            if (event->vkCode == VK_ESCAPE) {
                captured_key = event->vkCode;
                keymap_cancel_capture();
            } else if (captured.modifiers == 0 &&
                       (captured.key == VK_DELETE || captured.key == VK_BACK)) {
                captured_key = event->vkCode;
                finish_capture((keymap_hotkey){0, 0});
            } else {
                capture_key_pending = event->vkCode;
                captured_key = event->vkCode;
                capture_key_value = captured;
            }
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
    if (hold_hotkey.key != 0 && event->vkCode == hold_hotkey.key) {
        if (hold_key_down) {
            if (released) {
                hold_key_down = 0;
                set_hold_active(0);
                try_shift_compensation_after_release();
            }
            return 1;
        }
        if (hotkeys_enabled && !released && !block_hotkey_until_clear &&
            modifiers_equal(active_modifiers(), hold_hotkey.modifiers)) {
            hold_key_down = 1;
            set_hold_active(1);
            return 1;
        }
    }
    if (hotkey.key != 0 && event->vkCode == hotkey.key) {
        if (hotkey_key_down) {
            if (released) {
                hotkey_key_down = 0;
                try_shift_compensation_after_release();
            }
            return 1;
        }
        if (hotkeys_enabled && !hold_active && !released &&
            !block_hotkey_until_clear && modifiers_equal(active_modifiers(), hotkey.modifiers)) {
            hotkey_key_down = 1;
            mark_caps_restore_needed(hotkey);
            mark_shift_compensation_needed(hotkey);
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
        } else if (keymap_is_enabled()) {
            key->down = 1;
            send_digit(key->target);
            return 1;
        }
        break;
    }
    if (is_symbol_key(event->vkCode)) {
        unsigned char symbol_mask = (unsigned char)(1U << (event->vkCode & 7U));
        int was_passed = (passed_keys[event->vkCode >> 3] & symbol_mask) != 0;
        if ((!released && hold_active) || (released && was_passed)) {
            track_passed_key(event->vkCode, released);
            return CallNextHookEx(hook, code, message, parameter);
        }
    }
    if (released ? hold_swallow_key(event->vkCode, 1) :
        (hold_active && hold_swallow_key(event->vkCode, 0))) {
        return 1;
    }
    if (hold_active && !released && event->vkCode >= 'A' &&
        event->vkCode <= 'Z')
        note_unmapped_letter_press();
    if (!released && event->vkCode >= 'A' && event->vkCode <= 'Z') {
        unsigned int bit = 1U << (event->vkCode - 'A');
        if (keymap_is_enabled() &&
            !(active_modifiers() & (KEYMAP_MOD_CTRL_SIDES | KEYMAP_MOD_ALT_SIDES | KEYMAP_MOD_WIN_SIDES))) {
            note_unmapped_letter_press();
            if (block_letters) {
                blocked_letter_keys |= bit;
                return 1;
            }
        }
        passed_letter_keys |= bit;
    }
    track_passed_key(event->vkCode, released);
    return CallNextHookEx(hook, code, message, parameter);
}

int keymap_install(HINSTANCE instance, HWND window, UINT message)
{
    notify_window = window;
    changed_message = message;
    hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, instance, 0);
    return hook != NULL;
}

void keymap_handle_timer(UINT_PTR timer_id)
{
    if (timer_id == KEYMAP_COMPENSATION_TIMER_ID) {
        shift_compensation.timer_active = 0;
        complete_shift_compensation();
        return;
    }
    if (timer_id == KEYMAP_CAPS_RELEASE_TIMER_ID) {
        complete_caps_restore();
        return;
    }
    if (timer_id != KEYMAP_HOLD_RESOLVE_TIMER_ID || !hold_provisional)
        return;
    if (!hotkeys_enabled || block_hotkey_until_clear || !hold_active ||
        !modifiers_contain(active_modifiers(), hold_hotkey.modifiers)) {
        clear_hold_provisional();
        set_hold_active(0);
        return;
    }
    confirm_hold_provisional();
}

void keymap_uninstall(void)
{
    clear_shift_compensation();
    if (caps_restore == CAPS_RESTORE_TIMER)
        complete_caps_restore();
    else
        caps_restore = CAPS_RESTORE_IDLE;
    clear_hold_provisional();
    hold_suppressed_until_release = 0;
    capture = CAPTURE_NONE;
    source_capture_index = KEYMAP_KEY_COUNT;
    if (backspace_synthetic_down) {
        send_backspace_event(1);
        backspace_synthetic_down = 0;
    }
    set_hold_active(0);
    if (hold_modifiers_suspended) {
        send_modifier_state(0);
        hold_modifiers_suspended = 0;
    }
    ZeroMemory(hold_swallowed_keys, sizeof(hold_swallowed_keys));
    ZeroMemory(passed_keys, sizeof(passed_keys));
    passed_letter_keys = 0;
    keymap_set_enabled(0);
    if (hook != NULL) {
        UnhookWindowsHookEx(hook);
        hook = NULL;
    }
}
