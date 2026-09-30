#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <assert.h>
#include <stdio.h>

/* Drive the low-level hook with physical-key event records without installing it. */
#define TEST_REMINDER_MESSAGE (WM_APP + 6)
#define TEST_PREVIEW_MESSAGE (WM_APP + 10)
static UINT_PTR WINAPI mock_set_timer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC callback);
static BOOL WINAPI mock_kill_timer(HWND window, UINT_PTR id);
static UINT WINAPI mock_send_input(UINT count, LPINPUT input, int size);
static ULONGLONG WINAPI mock_get_tick_count64(void);

static BOOL WINAPI mock_post_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static SHORT WINAPI mock_get_async_key_state(int key);

#define SendInput mock_send_input
#define GetTickCount64 mock_get_tick_count64
#define PostMessageW mock_post_message
#define GetAsyncKeyState mock_get_async_key_state
#define SetTimer mock_set_timer
#define KillTimer mock_kill_timer

#include "../src/input/keymap.c"
#undef PostMessageW
#undef GetAsyncKeyState
#undef GetTickCount64
#undef SendInput
#undef SetTimer
#undef KillTimer


static INPUT sent_inputs[32];
static size_t sent_count;
static size_t send_calls;
static int mock_caps_lock_on;
static ULONGLONG now_ms;
static unsigned int reminder_posts;
static WPARAM preview_indices[16];
static LPARAM preview_states[16];
static size_t preview_event_count;
static UINT_PTR active_timer_id;
static UINT active_timer_delay;
static unsigned char mock_os_key_down[256];
static DWORD mock_language_mode = 1U;
static int mock_shift_toggles_ime;
static int defer_compensation_timer_for_test;
static int mock_track_injected_modifier_state;

static void fire_mock_ime_timer(void)
{
    if (!defer_compensation_timer_for_test && active_timer_id == KEYMAP_COMPENSATION_TIMER_ID)
        keymap_handle_timer(KEYMAP_COMPENSATION_TIMER_ID);
}

static SHORT WINAPI mock_get_async_key_state(int key)
{
    return key >= 0 && key < 256 && mock_os_key_down[key] ? (SHORT)-32768 : 0;
}


static ULONGLONG WINAPI mock_get_tick_count64(void)
{
    return now_ms;
}


static BOOL WINAPI mock_post_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)window;
    (void)wparam;
    (void)lparam;
    if (message == TEST_REMINDER_MESSAGE)
        ++reminder_posts;
    if (message == TEST_PREVIEW_MESSAGE) {
        assert(preview_event_count < sizeof(preview_indices) / sizeof(preview_indices[0]));
        preview_indices[preview_event_count] = wparam;
        preview_states[preview_event_count] = lparam;
        ++preview_event_count;
    }
    return TRUE;
}

static UINT_PTR WINAPI mock_set_timer(HWND window, UINT_PTR id, UINT elapse,
                                      TIMERPROC callback)
{
    (void)window;
    (void)callback;
    active_timer_id = id;
    active_timer_delay = elapse;
    return id;
}

static BOOL WINAPI mock_kill_timer(HWND window, UINT_PTR id)
{
    (void)window;
    if (active_timer_id == id)
        active_timer_id = 0;
    return TRUE;
}
static UINT WINAPI mock_send_input(UINT count, LPINPUT input, int size)
{
    UINT index;
    assert(size == sizeof(INPUT));
    assert(count > 0);
    ++send_calls;
    assert(sent_count + count <= sizeof(sent_inputs) / sizeof(sent_inputs[0]));
    for (index = 0; index < count; ++index) {
        sent_inputs[sent_count++] = input[index];
        if (mock_track_injected_modifier_state) {
            size_t modifier_index;
            for (modifier_index = 0; modifier_index < SIDE_MODIFIER_COUNT; ++modifier_index) {
                if (input[index].ki.wVk == modifier_keys[modifier_index]) {
                    mock_os_key_down[modifier_keys[modifier_index]] =
                        (input[index].ki.dwFlags & KEYEVENTF_KEYUP) == 0;
                    break;
                }
            }
        }
        if (input[index].ki.wVk == VK_CAPITAL &&
            !(input[index].ki.dwFlags & KEYEVENTF_KEYUP))
            mock_caps_lock_on = !mock_caps_lock_on;
        if (mock_shift_toggles_ime && input[index].ki.wVk == VK_LSHIFT &&
            !(input[index].ki.dwFlags & KEYEVENTF_KEYUP))
            mock_language_mode ^= 1U;
    }
    return count;
}

static LRESULT key_event(DWORD key, WPARAM message)
{
    KBDLLHOOKSTRUCT event = {0};
    event.vkCode = key;
    return keyboard_proc(HC_ACTION, message, (LPARAM)&event);
}

static LRESULT caps_event(int down)
{
    LRESULT result = key_event(VK_CAPITAL, down ? WM_KEYDOWN : WM_KEYUP);
    if (down && result == 0)
        mock_caps_lock_on = !mock_caps_lock_on;
    fire_mock_ime_timer();
    return result;
}

static void modifier(DWORD key, int down)
{
    if (key == VK_CAPITAL)
        caps_event(down);
    else {
        if (key < 256)
            mock_os_key_down[key] = down != 0;
        LRESULT result = key_event(key, down ? WM_KEYDOWN : WM_KEYUP);
        if (!down && result == 0 &&
            (key == VK_LSHIFT || key == VK_RSHIFT) && mock_shift_toggles_ime)
            mock_language_mode ^= 1U;
        fire_mock_ime_timer();
    }
}

static void test_overlapping_toggle_orders(void)
{
    static const DWORD orders[6][3] = {
        {VK_LCONTROL, VK_LWIN, VK_LSHIFT},
        {VK_LCONTROL, VK_LSHIFT, VK_LWIN},
        {VK_LWIN, VK_LCONTROL, VK_LSHIFT},
        {VK_LWIN, VK_LSHIFT, VK_LCONTROL},
        {VK_LSHIFT, VK_LCONTROL, VK_LWIN},
        {VK_LSHIFT, VK_LWIN, VK_LCONTROL}
    };
    static const DWORD releases[6][3] = {
        {VK_LCONTROL, VK_LWIN, VK_LSHIFT},
        {VK_LCONTROL, VK_LSHIFT, VK_LWIN},
        {VK_LWIN, VK_LCONTROL, VK_LSHIFT},
        {VK_LWIN, VK_LSHIFT, VK_LCONTROL},
        {VK_LSHIFT, VK_LCONTROL, VK_LWIN},
        {VK_LSHIFT, VK_LWIN, VK_LCONTROL}
    };
    size_t order_index;
    size_t release_index;
    size_t key_index;
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT | KEYMAP_MOD_WIN, 0});
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, 0});
    for (order_index = 0; order_index < 6; ++order_index) {
        for (release_index = 0; release_index < 6; ++release_index) {
            keymap_set_enabled(0);
            sent_count = 0;
            for (key_index = 0; key_index < 3; ++key_index)
                modifier(orders[order_index][key_index], 1);
            assert(keymap_is_enabled() && keymap_is_latched());
            assert(!hold_active && !hold_provisional);
            for (key_index = 0; key_index < 3; ++key_index)
                modifier(releases[release_index][key_index], 0);
            assert(keymap_is_enabled() && keymap_is_latched());
            keymap_set_enabled(0);
        }
    }
}

int main(void)
{
    keymap_hotkey value;
    DWORD defaults[KEYMAP_KEY_COUNT];
    DWORD sources[KEYMAP_KEY_COUNT];
    wchar_t name[96];
    unsigned int reminders_before_overlap;
    notify_window = (HWND)1;

    assert(!hotkey_valid((keymap_hotkey){0, 'A'}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL, 0}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_ALT, 'K'}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 0}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT, 0}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 0}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_LCTRL | KEYMAP_MOD_LALT | KEYMAP_MOD_LSHIFT, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_LCTRL, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_RCTRL, 'K'}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_LALT, 'K'}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_RALT | KEYMAP_MOD_LSHIFT, 0}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_LCTRL | KEYMAP_MOD_RALT, 0}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_LCTRL, 'K'}));
    assert(same_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL, 'K'},
                       (keymap_hotkey){KEYMAP_MOD_LCTRL, 'K'}));
    assert(!same_hotkey((keymap_hotkey){KEYMAP_MOD_LCTRL, 'K'},
                        (keymap_hotkey){KEYMAP_MOD_RCTRL, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_ALT, 0}));
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_ALT, 'K'}));
    assert(hotkey_valid((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_ALT | KEYMAP_MOD_CTRL, 0}));
    assert(validate_hotkey((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_ALT, 0}) ==
           KEYMAP_CAPTURE_INVALID_ALT);
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_CAPS, 0}));
    assert(validate_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL, 0}) ==
           KEYMAP_CAPTURE_INVALID_COUNT);
    assert(validate_hotkey((keymap_hotkey){KEYMAP_MOD_ALT, 'K'}) ==
           KEYMAP_CAPTURE_INVALID_ALT);
    assert(validate_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL, 'K'}) ==
           KEYMAP_CAPTURE_SAVED);
    keymap_format_hotkey(name, sizeof(name) / sizeof(name[0]),
                         (keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0});
    assert(wcscmp(name, L"Caps + Shift") == 0);
    assert(!hotkey_valid((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT | KEYMAP_MOD_WIN, 'K'}));
    assert(keymap_get_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LCTRL) &&
           keymap_get_hotkey().key == 0);
    assert(keymap_get_hold_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT) &&
           keymap_get_hold_hotkey().key == 0);
    assert(!hold_hotkey_valid((keymap_hotkey){KEYMAP_MOD_CAPS, 0}));
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CAPS, 0});
    assert(keymap_get_hold_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT));

    /* Default Ctrl+Caps Lock toggles in either press order. */
    keymap_set_enabled(0);
    modifier(VK_LCONTROL, 1);
    assert(caps_event(1) == 1 && keymap_is_enabled());
    assert(caps_event(0) == 1);
    modifier(VK_LCONTROL, 0);
    keymap_set_enabled(0);
    assert(caps_event(1) == 0);
    modifier(VK_LCONTROL, 1);
    assert(keymap_is_enabled());
    modifier(VK_LCONTROL, 0);
    assert(caps_event(0) == 0);
    if (active_timer_id == KEYMAP_CAPS_RELEASE_TIMER_ID)
        keymap_handle_timer(KEYMAP_CAPS_RELEASE_TIMER_ID);
    keymap_set_enabled(0);

    /* Default Caps+Shift hold works in either order and restores Caps state. */
    keymap_set_enabled(0);
    mock_caps_lock_on = 0;
    active_timer_id = 0;
    clear_shift_compensation();
    modifier(VK_LSHIFT, 1);
    assert(caps_event(1) == 1 && hold_active && !mock_caps_lock_on);
    sent_count = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(sent_count == 2 && sent_inputs[0].ki.wScan == L'1');
    assert(caps_event(0) == 1 && !hold_active);
    modifier(VK_LSHIFT, 0);
    if (active_timer_id == KEYMAP_COMPENSATION_TIMER_ID)
        keymap_handle_timer(KEYMAP_COMPENSATION_TIMER_ID);
    assert(!mock_caps_lock_on && !keymap_is_enabled());

    /* Injected Shift-up must not erase the physical Shift state used by Caps+Shift. */
    mock_caps_lock_on = 0;
    active_timer_id = 0;
    clear_shift_compensation();
    sent_count = 0;
    mock_track_injected_modifier_state = 1;
    mock_os_key_down[VK_LSHIFT] = 0;
    modifier(VK_LSHIFT, 1);
    assert(caps_event(1) == 1 && hold_active);
    assert(mock_os_key_down[VK_LSHIFT] == 0);
    key_event(VK_CAPITAL, WM_KEYDOWN); /* A repeated Caps event must not cancel the hold. */
    assert(hold_active && keymap_is_visual_enabled());
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(hold_active && sent_inputs[sent_count - 2].ki.wScan == L'1');
    caps_event(0);
    assert(!hold_active);
    modifier(VK_LSHIFT, 0);
    mock_track_injected_modifier_state = 0;
    mock_caps_lock_on = 0;
    active_timer_id = 0;
    clear_shift_compensation();
    assert(caps_event(1) == 0 && mock_caps_lock_on);
    modifier(VK_LSHIFT, 1);
    assert(hold_active && caps_restore == CAPS_RESTORE_ON_RELEASE);
    assert(caps_event(0) == 0 && !hold_active);
    assert(active_timer_id == KEYMAP_CAPS_RELEASE_TIMER_ID);
    keymap_handle_timer(KEYMAP_CAPS_RELEASE_TIMER_ID);
    modifier(VK_LSHIFT, 0);
    assert(!mock_caps_lock_on && !keymap_is_enabled());
    keymap_set_hotkeys((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0},
                       (keymap_hotkey){KEYMAP_MOD_CTRL, 'K'});
    assert(keymap_get_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT));
    assert(keymap_get_hold_hotkey().modifiers == KEYMAP_MOD_CTRL &&
           keymap_get_hold_hotkey().key == 'K');
    keymap_set_hotkeys((keymap_hotkey){KEYMAP_MOD_SHIFT, VK_SPACE},
                       (keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0});

    /* A user-selected Shift+Space toggle remains supported. */
    keymap_set_enabled(0);
    modifier(VK_LSHIFT, 1);
    assert(key_event(VK_SPACE, WM_KEYDOWN) == 1);
    assert(keymap_is_enabled());
    assert(key_event(VK_SPACE, WM_KEYUP) == 1);
    modifier(VK_LSHIFT, 0);
    keymap_set_enabled(0);
    modifier(VK_RSHIFT, 1);
    assert(key_event(VK_SPACE, WM_KEYDOWN) == 1);
    assert(keymap_is_enabled());
    assert(key_event(VK_SPACE, WM_KEYUP) == 1);
    modifier(VK_RSHIFT, 0);
    keymap_set_enabled(0);

    keymap_set_hotkeys((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0},
                       (keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, 0});

    /* A standalone Caps press keeps normal Windows behavior. */
    keymap_set_enabled(0);
    mock_caps_lock_on = 0;
    assert(caps_event(1) == 0 && mock_caps_lock_on);
    key_event('O', WM_KEYDOWN);
    key_event('O', WM_KEYUP);
    assert(!hold_active && !keymap_is_enabled());
    assert(caps_event(0) == 0 && mock_caps_lock_on);
    assert(caps_event(1) == 0 && !mock_caps_lock_on);
    assert(caps_event(0) == 0 && !mock_caps_lock_on);
    /* Caps+Shift sends a compensating Shift tap after release. */
    keymap_set_enabled(0);
    mock_caps_lock_on = 0;
    clear_shift_compensation();
    active_timer_id = 0;
    mock_language_mode = 1U;
    mock_shift_toggles_ime = 1;
    defer_compensation_timer_for_test = 1;
    modifier(VK_LSHIFT, 1);
    assert(shift_compensation.key == VK_LSHIFT);
    assert(caps_event(1) == 1);
    assert(shift_compensation.pending);
    assert(keymap_is_enabled() && !mock_caps_lock_on);
    assert(caps_event(0) == 1);
    assert(shift_compensation.pending);
    modifier(VK_LSHIFT, 0);
    assert(mock_language_mode == 0);
    assert(active_timer_id == KEYMAP_COMPENSATION_TIMER_ID);
    keymap_handle_timer(KEYMAP_COMPENSATION_TIMER_ID);
    assert(mock_language_mode == 1U);
    defer_compensation_timer_for_test = 0;
    mock_shift_toggles_ime = 0;
    assert(keymap_is_enabled() && !mock_caps_lock_on);
    keymap_set_enabled(0);

    /* Caps-first order passes Caps through, but swallows the Shift completing the chord. */
    clear_shift_compensation();
    active_timer_id = 0;
    mock_caps_lock_on = 0;
    mock_language_mode = 1U;
    mock_shift_toggles_ime = 1;
    defer_compensation_timer_for_test = 1;
    assert(caps_event(1) == 0 && mock_caps_lock_on);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled() && keymap_is_visual_enabled());
    assert(captured_modifiers[4]);
    assert(caps_event(0) == 0);
    assert(active_timer_id == KEYMAP_CAPS_RELEASE_TIMER_ID);
    modifier(VK_LSHIFT, 0);
    assert(!captured_modifiers[4]);
    assert(mock_language_mode == 1U);
    assert(active_timer_id == KEYMAP_CAPS_RELEASE_TIMER_ID);
    keymap_handle_timer(KEYMAP_CAPS_RELEASE_TIMER_ID);
    assert(!mock_caps_lock_on);
    defer_compensation_timer_for_test = 0;
    mock_shift_toggles_ime = 0;
    keymap_set_enabled(0);

    assert(caps_event(1) == 0 && mock_caps_lock_on);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled() && mock_caps_lock_on);
    assert(caps_event(0) == 0 && mock_caps_lock_on);
    assert(active_timer_id == KEYMAP_CAPS_RELEASE_TIMER_ID);
    keymap_handle_timer(KEYMAP_CAPS_RELEASE_TIMER_ID);
    assert(!mock_caps_lock_on);
    modifier(VK_LSHIFT, 0);
    assert(keymap_is_enabled() && !mock_caps_lock_on);

    /* A standalone Caps binding is rejected for hold input too. */
    keymap_begin_hold_capture();
    assert(caps_event(1) == 1);
    assert(caps_event(0) == 1);
    assert(keymap_get_hold_hotkey().modifiers == (KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT) &&
           keymap_get_hold_hotkey().key == 0);
    keymap_begin_capture();
    assert(caps_event(1) == 1);
    modifier(VK_LSHIFT, 1);
    modifier(VK_LSHIFT, 0);
    assert(keymap_is_capturing());
    assert(caps_event(0) == 1);
    assert(!keymap_is_capturing());
    assert(keymap_get_hotkey().modifiers == (KEYMAP_MOD_CAPS | KEYMAP_MOD_LSHIFT));
    /* Non-Caps shortcuts bypass Caps tracking and preserve ordinary Caps behavior. */
    keymap_set_enabled(0);
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_SHIFT, VK_SPACE});
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, 0});
    mock_caps_lock_on = 0;
    assert(caps_event(1) == 0 && mock_caps_lock_on);
    assert(caps_event(0) == 0);
    modifier(VK_LSHIFT, 1);
    assert(key_event(VK_SPACE, WM_KEYDOWN) == 1);
    assert(keymap_is_enabled() && keymap_is_visual_enabled());
    key_event(VK_SPACE, WM_KEYUP);
    modifier(VK_LSHIFT, 0);
    keymap_set_enabled(0);
    keymap_set_hotkeys((keymap_hotkey){KEYMAP_MOD_CAPS | KEYMAP_MOD_SHIFT, 0},
                       (keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT, 0});
    clear_shift_compensation();
    active_timer_id = 0;

    /* Modifier-first Caps+Shift toggles without changing Caps state. */
    mock_caps_lock_on = 0;
    modifier(VK_LSHIFT, 1);
    assert(caps_event(1) == 1);
    assert(keymap_is_enabled() && !mock_caps_lock_on);
    assert(caps_event(0) == 1);
    modifier(VK_LSHIFT, 0);
    assert(keymap_is_enabled());
    keymap_set_enabled(0);

    /* Disabling hotkeys still consumes the key-up paired with a swallowed Caps down. */
    modifier(VK_LSHIFT, 1);
    assert(caps_event(1) == 1);
    assert(keymap_is_enabled());
    keymap_set_hotkeys_enabled(0);
    assert(caps_event(0) == 1);
    modifier(VK_LSHIFT, 0);
    keymap_set_hotkeys_enabled(1);
    keymap_set_enabled(0);
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, 0});
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL, 'K'});

    keymap_begin_capture();
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_capturing());
    key_event('K', WM_KEYUP);
    value = keymap_get_hotkey();
    assert(!keymap_is_capturing());
    assert(value.modifiers == (KEYMAP_MOD_LCTRL | KEYMAP_MOD_LSHIFT) && value.key == 'K');
    modifier(VK_LSHIFT, 0);
    modifier(VK_LCONTROL, 0);
    keymap_set_hold_hotkey((keymap_hotkey){0, 0});
    modifier(VK_RCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(!keymap_is_enabled());
    key_event('K', WM_KEYUP);
    modifier(VK_LSHIFT, 0);
    modifier(VK_RCONTROL, 0);
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_enabled());
    key_event('K', WM_KEYUP);
    modifier(VK_LSHIFT, 0);
    modifier(VK_LCONTROL, 0);

    keymap_set_enabled(0);
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_RCTRL | KEYMAP_MOD_RSHIFT, 'K'});
    modifier(VK_RCONTROL, 1);
    modifier(VK_RSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_enabled());
    key_event('K', WM_KEYUP);
    modifier(VK_RSHIFT, 0);
    modifier(VK_RCONTROL, 0);

    keymap_set_enabled(0);
    keymap_begin_capture();
    modifier(VK_LCONTROL, 1);
    modifier(VK_LMENU, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_capturing());
    key_event('K', WM_KEYUP);
    value = keymap_get_hotkey();
    assert(!keymap_is_capturing());
    assert(value.modifiers == (KEYMAP_MOD_LCTRL | KEYMAP_MOD_LALT | KEYMAP_MOD_LSHIFT));
    assert(value.key == 'K');
    modifier(VK_LSHIFT, 0);
    modifier(VK_LMENU, 0);
    modifier(VK_LCONTROL, 0);
    modifier(VK_LCONTROL, 1);
    modifier(VK_LMENU, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_enabled());
    key_event('K', WM_KEYUP);
    modifier(VK_LSHIFT, 0);
    modifier(VK_LMENU, 0);
    modifier(VK_LCONTROL, 0);

    keymap_begin_capture();
    key_event(VK_ESCAPE, WM_KEYDOWN);
    key_event(VK_ESCAPE, WM_KEYUP);
    assert(!keymap_is_capturing());
    assert(keymap_get_hotkey().modifiers == value.modifiers);
    keymap_format_hotkey(name, sizeof(name) / sizeof(name[0]), value);
    assert(wcscmp(name, L"LCtrl + LAlt + LShift + K") == 0);

    keymap_begin_capture();
    modifier(VK_LMENU, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_capturing());
    key_event('K', WM_KEYUP);
    value = keymap_get_hotkey();
    assert(value.modifiers == (KEYMAP_MOD_LALT | KEYMAP_MOD_LSHIFT) && value.key == 'K');
    modifier(VK_LSHIFT, 0);
    modifier(VK_LMENU, 0);

    keymap_begin_capture();
    modifier(VK_LSHIFT, 1);
    modifier(VK_LCONTROL, 1);
    key_event('J', WM_KEYDOWN);
    assert(keymap_is_capturing());
    key_event('J', WM_KEYUP);
    value = keymap_get_hotkey();
    assert(value.modifiers == (KEYMAP_MOD_LCTRL | KEYMAP_MOD_LSHIFT) && value.key == 'J');
    modifier(VK_LCONTROL, 0);
    modifier(VK_LSHIFT, 0);

    keymap_begin_capture();
    keymap_begin_source_capture(6);
    assert(keymap_is_source_capturing() && !keymap_is_capturing());
    keymap_begin_hold_capture();
    assert(keymap_is_hold_capturing() && !keymap_is_source_capturing());
    keymap_cancel_capture();
    assert(!keymap_is_capturing() && !keymap_is_hold_capturing() &&
           !keymap_is_source_capturing() &&
           keymap_capturing_source() == KEYMAP_KEY_COUNT);

    keymap_get_sources(defaults);
    assert(defaults[6] == 'N' && defaults[9] == VK_SPACE);
    assert(keymap_get_source(10) == 0);
    keymap_begin_source_capture(6);
    assert(keymap_is_source_capturing() && keymap_capturing_source() == 6);
    key_event('B', WM_KEYDOWN);
    assert(!keymap_is_source_capturing() && keymap_get_source(6) == 'B');
    key_event('B', WM_KEYUP);

    keymap_begin_source_capture(6);
    key_event('J', WM_KEYDOWN);
    assert(keymap_get_source(6) == 'J' && keymap_get_source(3) == 'B');
    key_event('J', WM_KEYUP);

    keymap_begin_source_capture(6);
    key_event(VK_ESCAPE, WM_KEYDOWN);
    key_event(VK_ESCAPE, WM_KEYUP);
    assert(keymap_get_source(6) == 'J');

    keymap_begin_source_capture(6);
    key_event(VK_TAB, WM_KEYDOWN);
    key_event(VK_TAB, WM_KEYUP);
    assert(!keymap_is_source_capturing() && keymap_get_source(6) == 'J');

    keys[6].swallow_up = 1;
    keys[6].swallow_source = 'J';
    keymap_begin_source_capture(6);
    key_event('J', WM_KEYDOWN);
    key_event('J', WM_KEYUP);
    assert(!keys[6].swallow_up && keys[6].swallow_source == 0);

    keymap_get_sources(sources);
    sources[6] = 'B';
    assert(!keymap_set_sources(sources));
    assert(keymap_get_source(6) == 'J');
    sources[6] = VK_TAB;
    assert(!keymap_set_sources(sources));
    assert(keymap_set_sources(defaults));
    assert(keymap_get_source(6) == 'N' && keymap_get_source(3) == 'J');
    keymap_format_source(name, sizeof(name) / sizeof(name[0]), VK_SPACE);
    assert(wcscmp(name, L"空格") == 0);

    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL, 'Q'});
    assert(keymap_get_hotkey().modifiers == KEYMAP_MOD_CTRL &&
           keymap_get_hotkey().key == 'Q');
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 0});
    keymap_begin_capture();
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(keymap_get_hotkey().modifiers ==
           (KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT) &&
           keymap_get_hotkey().key == 0);

    keymap_set_enabled(1);
    sent_count = 0;
    send_calls = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(sent_count == 4 && send_calls == 2);
    assert(sent_inputs[0].ki.wVk == 0 && sent_inputs[0].ki.wScan == L'1' &&
           sent_inputs[0].ki.dwFlags == KEYEVENTF_UNICODE);
    assert(sent_inputs[1].ki.wVk == 0 && sent_inputs[1].ki.wScan == L'1' &&
           sent_inputs[1].ki.dwFlags == (KEYEVENTF_UNICODE | KEYEVENTF_KEYUP));
    assert(sent_inputs[2].ki.wScan == L'1' &&
           sent_inputs[2].ki.dwFlags == KEYEVENTF_UNICODE);
    assert(sent_inputs[3].ki.wScan == L'1' &&
           sent_inputs[3].ki.dwFlags == (KEYEVENTF_UNICODE | KEYEVENTF_KEYUP));

    sent_count = 0;
    send_calls = 0;
    key_event('N', WM_KEYDOWN);
    key_event('O', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    key_event('O', WM_KEYUP);
    assert(sent_count == 4 && send_calls == 2);
    assert(sent_inputs[0].ki.wScan == L'1' &&
           sent_inputs[0].ki.dwFlags == KEYEVENTF_UNICODE);
    assert(sent_inputs[1].ki.wScan == L'1' &&
           sent_inputs[1].ki.dwFlags == (KEYEVENTF_UNICODE | KEYEVENTF_KEYUP));
    assert(sent_inputs[2].ki.wScan == L'9' &&
           sent_inputs[2].ki.dwFlags == KEYEVENTF_UNICODE);
    assert(sent_inputs[3].ki.wScan == L'9' &&
           sent_inputs[3].ki.dwFlags == (KEYEVENTF_UNICODE | KEYEVENTF_KEYUP));

    keymap_set_enabled(0);
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(sent_count == 4);

    assert(!keymap_block_letters_enabled());
    keymap_set_enabled(1);
    assert(key_event('A', WM_KEYDOWN) != 1);
    keymap_set_block_letters(1);
    assert(keymap_block_letters_enabled());
    assert(key_event('A', WM_KEYDOWN) != 1);
    assert(key_event('A', WM_KEYUP) != 1);
    assert(key_event('A', WM_KEYDOWN) == 1);
    assert(key_event('A', WM_KEYDOWN) == 1);
    keymap_set_enabled(0);
    assert(key_event('A', WM_KEYUP) == 1);
    assert(key_event('A', WM_KEYDOWN) != 1);
    assert(key_event('A', WM_KEYUP) != 1);

    keymap_set_enabled(1);
    modifier(VK_LCONTROL, 1);
    assert(key_event('C', WM_KEYDOWN) != 1);
    assert(key_event('C', WM_KEYUP) != 1);
    modifier(VK_LCONTROL, 0);
    modifier(VK_LSHIFT, 1);
    assert(key_event('A', WM_KEYDOWN) == 1);
    assert(key_event('A', WM_KEYUP) == 1);
    modifier(VK_LSHIFT, 0);

    sent_count = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(sent_count == 2 && sent_inputs[0].ki.wScan == L'1');
    keymap_begin_source_capture(6);
    assert(key_event('B', WM_KEYDOWN) == 1);
    assert(keymap_get_source(6) == 'B');
    assert(key_event('B', WM_KEYUP) == 1);
    assert(sent_count == 2);
    assert(keymap_set_sources(defaults));

    assert(key_event('A', WM_KEYDOWN) == 1);
    keymap_set_block_letters(0);
    assert(key_event('A', WM_KEYUP) == 1);
    assert(key_event('A', WM_KEYDOWN) != 1);
    assert(key_event('A', WM_KEYUP) != 1);
    keymap_set_enabled(0);

    notify_window = (HWND)1;
    keymap_set_reminder_message(TEST_REMINDER_MESSAGE);
    reminder_posts = 0;
    now_ms = 90000;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 0);

    keymap_set_enabled(1);
    modifier(VK_LCONTROL, 1);
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    modifier(VK_LCONTROL, 0);
    assert(reminder_posts == 0);

    now_ms = 100000;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    now_ms = 101000;
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    now_ms = 101501;
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 0);

    sent_count = 0;
    now_ms = 104000;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    now_ms = 104500;
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    now_ms = 104700;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(reminder_posts == 0 && sent_count == 2);
    now_ms = 104900;
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 1);
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 1);

    now_ms = 114899;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 1);

    now_ms = 114900;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    now_ms = 115100;
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    now_ms = 115300;
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 2);

    keymap_set_enabled(0);
    keymap_set_enabled(1);
    now_ms = 115301;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 3);

    now_ms = 125300;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 3);
    now_ms = 125301;
    key_event('A', WM_KEYDOWN);
    key_event('A', WM_KEYUP);
    key_event('B', WM_KEYDOWN);
    key_event('B', WM_KEYUP);
    key_event('C', WM_KEYDOWN);
    key_event('C', WM_KEYUP);
    assert(reminder_posts == 4);
    keymap_set_enabled(0);

    /* A held shortcut overlays the saved mode, including while keys are mapped. */
    keymap_set_block_letters(0);
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL, 'Q'});
    assert(!keymap_is_enabled() && !keymap_is_latched());
    modifier(VK_LCONTROL, 1);
    assert(key_event('Q', WM_KEYDOWN) == 1);
    assert(keymap_is_enabled() && !keymap_is_latched());
    sent_count = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(sent_count == 2 && sent_inputs[0].ki.wScan == L'1');
    keymap_set_block_letters(1);
    assert(key_event(VK_OEM_1, WM_KEYDOWN) != 1);
    assert(key_event(VK_OEM_1, WM_KEYUP) != 1);
    keymap_set_block_letters(0);

    modifier(VK_LCONTROL, 0);
    assert(!keymap_is_enabled());
    assert(key_event('Q', WM_KEYUP) == 1);
    sent_count = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(sent_count == 0);

    modifier(VK_LCONTROL, 1);
    key_event('Q', WM_KEYDOWN);
    keymap_set_enabled(1);
    assert(keymap_is_latched());
    key_event('Q', WM_KEYUP);
    modifier(VK_LCONTROL, 0);
    assert(keymap_is_enabled() && keymap_is_latched());
    keymap_set_enabled(0);

    /* Modifier-only hold ends on release and does not latch. */
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT, 0});
    modifier(VK_LCONTROL, 1);
    modifier(VK_LMENU, 1);
    assert(keymap_is_enabled() && !keymap_is_latched());
    modifier(VK_LMENU, 0);
    assert(!keymap_is_enabled());
    modifier(VK_LCONTROL, 0);

    sent_count = 0;
    /* Only the extra modifier in the overlapping toggle chord resolves the prefix. */
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT | KEYMAP_MOD_WIN, 0});
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, 0});
    keymap_set_enabled(0);
    keymap_set_block_letters(0);
    now_ms += 20000;
    reminders_before_overlap = reminder_posts;
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled() && !keymap_is_latched() && hold_provisional);
    modifier(VK_LMENU, 1); /* An unrelated modifier must not cancel the pending chord. */
    assert(keymap_is_enabled() && hold_provisional);
    modifier(VK_LWIN, 1);
    assert(keymap_is_enabled() && keymap_is_latched());
    assert(!hold_active && !hold_provisional);
    assert(active_timer_id == 0 && reminder_posts == reminders_before_overlap);
    modifier(VK_LWIN, 0);
    modifier(VK_LSHIFT, 0);
    modifier(VK_LCONTROL, 0);
    modifier(VK_LMENU, 0);
    test_overlapping_toggle_orders();
    keymap_set_enabled(1);

    /* When keypad mode was already latched, the overlapping toggle turns it off. */
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled() && keymap_is_latched() && hold_provisional);
    modifier(VK_LMENU, 1);
    modifier(VK_LWIN, 1);
    assert(!keymap_is_enabled() && !keymap_is_latched());
    assert(!hold_active && !hold_provisional);
    assert(reminder_posts == reminders_before_overlap);
    modifier(VK_LWIN, 0);
    modifier(VK_LMENU, 0);
    modifier(VK_LSHIFT, 0);
    modifier(VK_LCONTROL, 0);

    /* Confirming a real hold after the delay sends the reminder only then. */
    keymap_set_enabled(1);
    now_ms += KEYMAP_HOLD_RESOLVE_DELAY_MS;
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled() && hold_provisional);
    assert(reminder_posts == reminders_before_overlap);
    keymap_handle_timer(KEYMAP_HOLD_RESOLVE_TIMER_ID);
    assert(hold_active && !hold_provisional);
    assert(reminder_posts == reminders_before_overlap + 1);
    modifier(VK_LSHIFT, 0);
    assert(keymap_is_enabled() && !hold_active);
    modifier(VK_LCONTROL, 0);
    keymap_set_enabled(0);

    /* A mapped key pressed during the decision window is still sent as a digit. */
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled() && hold_provisional);
    sent_count = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(keymap_is_enabled() && hold_active && !hold_provisional);
    assert(sent_count == 2 && sent_inputs[0].ki.wScan == L'1');
    modifier(VK_LSHIFT, 0);
    assert(!keymap_is_enabled());
    modifier(VK_LCONTROL, 0);

    /* Rapid mapped keys do not release a key-specific hold while its chord remains down. */
    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT, 'F'});
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    assert(key_event('F', WM_KEYDOWN) == 1);
    assert(keymap_is_enabled() && hold_active);
    sent_count = 0;
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    key_event('O', WM_KEYDOWN);
    key_event('O', WM_KEYUP);
    assert(keymap_is_enabled() && hold_active);
    assert(sent_count == 4 && sent_inputs[0].ki.wScan == L'1' &&
           sent_inputs[2].ki.wScan == L'9');
    modifier(VK_LCONTROL, 0);
    assert(!keymap_is_enabled() && !hold_active);
    modifier(VK_LSHIFT, 0);
    key_event('F', WM_KEYUP);
    assert(!keymap_is_enabled() && !hold_active);

    keymap_set_hold_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT, 0});
    /* Capture rejects reserved and mapped keys; Delete clears the hold binding. */
    keymap_begin_hold_capture();
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(!keymap_is_hold_capturing());
    assert(keymap_get_hold_hotkey().modifiers == (KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT));
    keymap_begin_hold_capture();
    key_event(VK_DELETE, WM_KEYDOWN);
    key_event(VK_DELETE, WM_KEYUP);
    assert(keymap_get_hold_hotkey().modifiers == 0 && keymap_get_hold_hotkey().key == 0);
    keymap_set_hold_hotkey((keymap_hotkey){0, 0});

    keymap_set_preview_message(TEST_PREVIEW_MESSAGE);
    keymap_set_preview_enabled(1);
    keymap_set_enabled(0);
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(preview_event_count == 2);
    assert(preview_indices[0] == 6 && preview_states[0] == 1);
    assert(preview_indices[1] == 6 && preview_states[1] == 0);

    modifier(VK_LCONTROL, 1);
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    modifier(VK_LCONTROL, 0);
    assert(preview_event_count == 2);

    keymap_set_enabled(1);
    key_event('O', WM_KEYDOWN);
    key_event('O', WM_KEYUP);
    assert(preview_event_count == 4);
    assert(preview_indices[2] == 2 && preview_states[2] == 1);
    assert(preview_indices[3] == 2 && preview_states[3] == 0);
    keymap_set_preview_enabled(0);

    puts("keymap tests passed");
    return 0;
}
