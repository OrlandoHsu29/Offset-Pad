#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>

/* Drive the low-level hook with physical-key event records without installing it. */
static UINT WINAPI mock_send_input(UINT count, LPINPUT input, int size);
#define SendInput mock_send_input
#include "../src/input/keymap.c"
#undef SendInput

static INPUT sent_inputs[8];
static size_t sent_count;
static size_t send_calls;

static UINT WINAPI mock_send_input(UINT count, LPINPUT input, int size)
{
    UINT index;
    assert(size == sizeof(INPUT));
    assert(count == 2);
    ++send_calls;
    assert(sent_count + count <= sizeof(sent_inputs) / sizeof(sent_inputs[0]));
    for (index = 0; index < count; ++index)
        sent_inputs[sent_count++] = input[index];
    return count;
}

static void key_event(DWORD key, WPARAM message)
{
    KBDLLHOOKSTRUCT event = {0};
    event.vkCode = key;
    keyboard_proc(HC_ACTION, message, (LPARAM)&event);
}

static void modifier(DWORD key, int down)
{
    key_event(key, down ? WM_KEYDOWN : WM_KEYUP);
}

int main(void)
{
    keymap_hotkey value;
    DWORD defaults[KEYMAP_KEY_COUNT];
    DWORD sources[KEYMAP_KEY_COUNT];
    wchar_t name[96];

    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 0});
    modifier(VK_LCONTROL, 1);
    modifier(VK_LMENU, 1);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled());
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled());
    modifier(VK_LSHIFT, 0);
    modifier(VK_LSHIFT, 1);
    assert(!keymap_is_enabled());
    modifier(VK_LSHIFT, 0);
    modifier(VK_LMENU, 0);
    modifier(VK_LCONTROL, 0);

    keymap_begin_capture();
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    value = keymap_get_hotkey();
    assert(!keymap_is_capturing());
    assert(value.modifiers == (KEYMAP_MOD_CTRL | KEYMAP_MOD_SHIFT) && value.key == 'K');
    key_event('K', WM_KEYUP);
    modifier(VK_LSHIFT, 0);
    modifier(VK_LCONTROL, 0);
    modifier(VK_LCONTROL, 1);
    modifier(VK_LSHIFT, 1);
    key_event('K', WM_KEYDOWN);
    assert(keymap_is_enabled());
    key_event('K', WM_KEYUP);
    modifier(VK_LSHIFT, 0);
    modifier(VK_LCONTROL, 0);

    keymap_set_enabled(0);
    keymap_begin_capture();
    modifier(VK_LCONTROL, 1);
    modifier(VK_LMENU, 1);
    modifier(VK_LSHIFT, 1);
    modifier(VK_LSHIFT, 0);
    value = keymap_get_hotkey();
    assert(value.modifiers == (KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT));
    assert(value.key == 0);
    modifier(VK_LMENU, 0);
    modifier(VK_LCONTROL, 0);
    modifier(VK_LCONTROL, 1);
    modifier(VK_LMENU, 1);
    modifier(VK_LSHIFT, 1);
    assert(keymap_is_enabled());
    modifier(VK_LSHIFT, 0);
    modifier(VK_LMENU, 0);
    modifier(VK_LCONTROL, 0);

    keymap_begin_capture();
    key_event(VK_ESCAPE, WM_KEYDOWN);
    key_event(VK_ESCAPE, WM_KEYUP);
    assert(!keymap_is_capturing());
    assert(keymap_get_hotkey().modifiers == value.modifiers);
    keymap_format_hotkey(name, sizeof(name) / sizeof(name[0]), value);
    assert(wcscmp(name, L"Ctrl + Alt + Shift") == 0);

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

    keymap_set_hotkey((keymap_hotkey){0, 'Q'});
    keymap_begin_source_capture(6);
    key_event('Q', WM_KEYDOWN);
    key_event('Q', WM_KEYUP);
    assert(keymap_get_source(6) == 'N');
    keymap_set_hotkey((keymap_hotkey){KEYMAP_MOD_CTRL | KEYMAP_MOD_ALT | KEYMAP_MOD_SHIFT, 0});
    keymap_begin_capture();
    key_event('N', WM_KEYDOWN);
    key_event('N', WM_KEYUP);
    assert(keymap_get_hotkey().key == 0);

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

    puts("keymap tests passed");
    return 0;
}
