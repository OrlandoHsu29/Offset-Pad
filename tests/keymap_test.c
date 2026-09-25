#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>

/* Drive the low-level hook with physical-key event records without installing it. */
#include "../src/input/keymap.c"

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

    puts("keymap tests passed");
    return 0;
}
