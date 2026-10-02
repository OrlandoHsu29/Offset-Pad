#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wchar.h>

#include "app_ui.h"
#include "app_ui_paint.h"
#include "app_tray.h"
#include "app_version.h"
#include "app_settings.h"
#include "rounded_box.h"
#include "keymap.h"

#define RESET_TOOLTIP_TIMER_ID 0x4F74U
#define RESET_TOOLTIP_DELAY_MS 400U
#define RESET_HINT_CLASS L"OffsetPadResetHint"
#define IDI_APP_ICON_LIGHT 101
#define IDI_APP_ICON_DARK 102


static const wchar_t settings_class[] = L"OffsetPadSettingsWindow";
static HINSTANCE instance;
static HWND message_window;
static HWND settings_window;
static HWND autostart_check;
static HWND block_letters_check;
static HWND hotkey_button;
static HWND shortcut_tooltip;
static HWND hold_hotkey_button;
static HWND auto_updates_check;
static HWND update_link_button;
static HWND mode_badge_button;
static int update_available;
static HFONT title_font;
static HFONT heading_font;
static HFONT body_font;
static HFONT control_font;
static HFONT small_font;
static HFONT compact_font;
static HFONT symbol_font;
static HFONT large_symbol_font;
static HFONT keycap_font;
static HFONT icon_font;
static HICON light_icon;
static HICON dark_icon;
static HICON light_logo;
static HICON dark_logo;
static app_ui_actions actions;
static int dpi = 96;
static int hovered_source_key = -1;
static unsigned int pressed_source_keys;
static int mouse_leave_tracking;
static int hovered_button_id;
static int hovered_reset_id;
static int pressed_reset_id;
static int active_hint_id;
static int tooltip_tail_center_x;
static int hovered_hint_id;
static wchar_t hotkey_capture_status[128];

typedef struct hover_button {
    HWND window;
    WNDPROC original_proc;
    int id;
    int tracking_mouse_leave;
} hover_button;

static hover_button hover_buttons[10];

static HICON current_icon(void)
{
    return keymap_is_visual_enabled() ? dark_icon : light_icon;
}

static HICON current_logo(void)
{
    HICON logo = keymap_is_visual_enabled() ? dark_logo : light_logo;
    return logo != NULL ? logo : current_icon();
}

#define COLOR_WHITE RGB(255, 255, 255)
#define COLOR_INK RGB(34, 34, 34)
#define COLOR_MUTED RGB(112, 112, 112)
#define COLOR_ACCENT RGB(43, 43, 43)
#define COLOR_ACCENT_DOWN RGB(22, 22, 22)
#define COLOR_TINT RGB(246, 246, 246)
#define COLOR_HOVER RGB(243, 243, 243)
#define COLOR_ACCENT_HOVER RGB(58, 58, 58)
#define COLOR_RESET_HOVER RGB(96, 96, 96)
#define COLOR_BORDER RGB(222, 222, 222)
#define COLOR_MODE_BADGE_HOVER_BORDER RGB(190, 190, 190)
#define COLOR_KEYCAP RGB(252, 252, 252)
#define COLOR_KEYCAP_OUTPUT RGB(244, 244, 244)
#define COLOR_KEYCAP_HOVER RGB(240, 240, 240)
#define COLOR_SWITCH_HOVER RGB(205, 205, 205)
#define COLOR_AUTOSTART_HOVER_ON RGB(70, 70, 70)
#define COLOR_AUTOSTART_HOVER_OFF RGB(190, 190, 190)

static app_ui_paint_state paint_state(void)
{
    app_ui_paint_state state = {0};
    state.dpi = dpi;
    state.logo = current_logo();
    state.title_font = title_font;
    state.heading_font = heading_font;
    state.body_font = body_font;
    state.control_font = control_font;
    state.small_font = small_font;
    state.compact_font = compact_font;
    state.symbol_font = symbol_font;
    state.large_symbol_font = large_symbol_font;
    state.keycap_font = keycap_font;
    state.icon_font = icon_font;
    state.capture_status = hotkey_capture_status;
    state.hovered_source_key = hovered_source_key;
    state.pressed_source_keys = pressed_source_keys;
    state.hovered_button_id = hovered_button_id;
    state.hovered_reset_id = hovered_reset_id;
    state.pressed_reset_id = pressed_reset_id;
    state.active_hint_id = active_hint_id;
    state.tooltip_tail_center_x = tooltip_tail_center_x;
    return state;
}
static int scale(int value)
{
    return MulDiv(value, dpi, 96);
}

static RECT scaled_rect(int left, int top, int right, int bottom)
{
    RECT rect = {scale(left), scale(top), scale(right), scale(bottom)};
    return rect;
}

static int header_update_button_left(void)
{
    HDC dc = GetDC(NULL);
    SIZE extent = {0};
    if (dc != NULL) {
        HGDIOBJ old_font = SelectObject(dc, title_font);
        GetTextExtentPoint32W(dc, L"Offset Pad", 10, &extent);
        SelectObject(dc, old_font);
        ReleaseDC(NULL, dc);
    }
    return scale(84) + extent.cx + scale(6);
}

static RECT source_key_rect(size_t index)
{
    int row;
    int column;
    int left;
    if (index == 9)
        return scaled_rect(78, 323, 141, 352);
    if (index == 10)
        return scaled_rect(146, 323, 175, 352);
    row = (int)index / 3;
    column = (int)index % 3;
    left = 67 + (row == 1 ? 11 : 0) + column * 34;
    return scaled_rect(left, 221 + row * 34, left + 29, 250 + row * 34);
}

static int source_key_at(POINT point)
{
    size_t index;
    for (index = 0; index < KEYMAP_KEY_COUNT; ++index) {
        RECT rect = source_key_rect(index);
        if (PtInRect(&rect, point))
            return (int)index;
    }
    return -1;
}

static hover_button *find_hover_button(HWND window)
{
    size_t index;
    for (index = 0; index < sizeof(hover_buttons) / sizeof(hover_buttons[0]); ++index)
        if (hover_buttons[index].window == window)
            return &hover_buttons[index];
    return NULL;
}

static LRESULT CALLBACK reset_hint_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam)
{
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        RECT rect;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &rect);
        app_ui_paint_hint(dc, rect, paint_state());
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wparam, lparam);
}static void update_reset_tooltip(HWND tool, int id)
{
    RECT client;
    RECT owner_rect;
    POINT anchor;
    SIZE extent = {0};
    wchar_t label[64];
    HDC dc;
    HGDIOBJ old_font;
    int width;
    int height;
    int x;
    int y;
    if (shortcut_tooltip == NULL)
        return;
    if (id == 0 || tool == NULL) {
        active_hint_id = 0;
        ShowWindow(shortcut_tooltip, SW_HIDE);
        return;
    }
    if (active_hint_id == id && IsWindowVisible(shortcut_tooltip))
        return;
    if (!GetClientRect(tool, &client))
        return;

    if (id == ID_RESET_KEYMAP) {
        lstrcpynW(label, L"\x91CD\x7F6E\x6309\x952E\x6620\x5C04",
                  (int)(sizeof(label) / sizeof(label[0])));
    } else if (id == ID_BLOCK_LETTERS_TITLE) {
        lstrcpynW(label, L"小键盘模式下拦截未映射字符键；\n顶部数字行仍输出对应符号。",
                  (int)(sizeof(label) / sizeof(label[0])));
    } else {
        lstrcpynW(label, L"重置快捷键",
                  (int)(sizeof(label) / sizeof(label[0])));
    }
    dc = GetDC(tool);
    if (dc == NULL)
        return;
    old_font = SelectObject(dc, compact_font != NULL ? compact_font :
                            GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(dc, label, (int)wcslen(label), &extent);
    SelectObject(dc, old_font);
    ReleaseDC(tool, dc);
    width = id == ID_BLOCK_LETTERS_TITLE ? scale(224) : extent.cx + scale(34);
    if (width < scale(76))
        width = scale(76);
    height = id == ID_BLOCK_LETTERS_TITLE ? scale(48) : scale(34);

    if (id == ID_BLOCK_LETTERS_TITLE) {
        MONITORINFO monitor_info = {sizeof(monitor_info)};
        HMONITOR monitor;
        SIZE title_extent = {0};
        GetWindowTextW(GetDlgItem(GetParent(tool), ID_BLOCK_LETTERS_CARD),
                       label, (int)(sizeof(label) / sizeof(label[0])));
        dc = GetDC(tool);
        if (dc == NULL)
            return;
        old_font = SelectObject(dc, control_font != NULL ? control_font :
                                GetStockObject(DEFAULT_GUI_FONT));
        GetTextExtentPoint32W(dc, label, (int)wcslen(label), &title_extent);
        SelectObject(dc, old_font);
        ReleaseDC(tool, dc);
        if (!GetWindowRect(settings_window, &owner_rect))
            return;
        anchor.x = title_extent.cx / 2;
        anchor.y = client.bottom / 2;
        ClientToScreen(tool, &anchor);
        x = anchor.x - width / 2;
        y = anchor.y - height - scale(8);
        monitor = MonitorFromWindow(settings_window, MONITOR_DEFAULTTONEAREST);
        if (monitor != NULL && GetMonitorInfoW(monitor, &monitor_info)) {
            if (x < monitor_info.rcWork.left)
                x = monitor_info.rcWork.left;
            if (x + width > monitor_info.rcWork.right)
                x = monitor_info.rcWork.right - width;
            if (y < monitor_info.rcWork.top)
                y = monitor_info.rcWork.top;
        }
    } else {
        anchor.x = client.right - scale(16);
        anchor.y = scale(13);
        ClientToScreen(tool, &anchor);
        x = anchor.x - width / 2;
        y = anchor.y - height - scale(8);
        if (GetWindowRect(settings_window, &owner_rect)) {
            if (x < owner_rect.left + scale(4))
                x = owner_rect.left + scale(4);
            if (x + width > owner_rect.right - scale(4))
                x = owner_rect.right - width - scale(4);
        }
    }
    tooltip_tail_center_x = anchor.x - x;
    active_hint_id = id;
    InvalidateRect(shortcut_tooltip, NULL, FALSE);
    SetWindowPos(shortcut_tooltip, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
static int shortcut_reset_at(HWND window, POINT point)
{
    RECT client;
    RECT hit;
    int control_id = GetDlgCtrlID(window);
    if (control_id == ID_RESET_KEYMAP) {
        if (!GetClientRect(window, &client))
            return 0;
        return PtInRect(&client, point) ? ID_RESET_KEYMAP : 0;
    }
    if (control_id != ID_HOTKEY && control_id != ID_HOLD_HOTKEY)
        return 0;
    if (!GetClientRect(window, &client))
        return 0;
    hit.left = client.right - scale(28);
    hit.top = scale(1);
    hit.right = client.right - scale(4);
    hit.bottom = scale(25);
    if (!PtInRect(&hit, point))
        return 0;
    return control_id == ID_HOTKEY ? ID_RESET_HOTKEY : ID_RESET_HOLD_HOTKEY;
}

static int hover_hint_at(HWND window, POINT point)
{
    int reset_id = shortcut_reset_at(window, point);
    if (reset_id != 0)
        return reset_id;
    if (GetDlgCtrlID(window) == ID_BLOCK_LETTERS_TITLE) {
        RECT client;
        RECT title_rect;
        wchar_t label[64];
        SIZE extent = {0};
        HDC dc;
        HGDIOBJ old_font;
        if (!GetClientRect(window, &client))
            return 0;
        GetWindowTextW(GetDlgItem(GetParent(window), ID_BLOCK_LETTERS_CARD),
                       label, (int)(sizeof(label) / sizeof(label[0])));
        dc = GetDC(window);
        if (dc == NULL)
            return 0;
        old_font = SelectObject(dc, control_font != NULL ? control_font :
                                GetStockObject(DEFAULT_GUI_FONT));
        GetTextExtentPoint32W(dc, label, (int)wcslen(label), &extent);
        SelectObject(dc, old_font);
        ReleaseDC(window, dc);
        title_rect.left = 0;
        title_rect.right = extent.cx;
        title_rect.top = (client.bottom - scale(20)) / 2;
        title_rect.bottom = title_rect.top + scale(20);
        if (PtInRect(&title_rect, point))
            return ID_BLOCK_LETTERS_TITLE;
    }
    return 0;
}

static void invalidate_hover_button(int id)
{
    size_t index;
    if (id == 0)
        return;
    for (index = 0; index < sizeof(hover_buttons) / sizeof(hover_buttons[0]); ++index) {
        if (hover_buttons[index].id == id && hover_buttons[index].window != NULL) {
            InvalidateRect(hover_buttons[index].window, NULL, FALSE);
            return;
        }
    }
}


static void update_reset_tooltip(HWND tool, int id);

static LRESULT CALLBACK hover_button_proc(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam)
{
    hover_button *button = find_hover_button(window);
    WNDPROC original_proc;
    if (button == NULL)
        return DefWindowProcW(window, message, wparam, lparam);
    original_proc = button->original_proc;
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, HOVER_DEFAULT};
        POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int previous_id = hovered_button_id;
        int previous_reset_id = hovered_reset_id;
        int previous_hint_id = hovered_hint_id;
        int reset_id = shortcut_reset_at(window, point);
        int hint_id = hover_hint_at(window, point);
        if (!button->tracking_mouse_leave && TrackMouseEvent(&tracking))
            button->tracking_mouse_leave = 1;
        if (hovered_source_key >= 0) {
            hovered_source_key = -1;
            InvalidateRect(GetParent(window), NULL, FALSE);
        }
        hovered_button_id = button->id;
        hovered_reset_id = reset_id;
        hovered_hint_id = hint_id;
        if (previous_id != hovered_button_id &&
            previous_id != ID_BLOCK_LETTERS_TITLE)
            invalidate_hover_button(previous_id);
        if ((previous_id != hovered_button_id || previous_reset_id != hovered_reset_id) &&
            button->id != ID_BLOCK_LETTERS_TITLE)
            InvalidateRect(window, NULL, FALSE);
        if (previous_hint_id != hovered_hint_id) {
            KillTimer(window, RESET_TOOLTIP_TIMER_ID);
            update_reset_tooltip(window, 0);
            if (hovered_hint_id != 0 &&
                SetTimer(window, RESET_TOOLTIP_TIMER_ID, RESET_TOOLTIP_DELAY_MS, NULL) == 0)
                update_reset_tooltip(window, hovered_hint_id);
        }
    } else if (message == WM_MOUSELEAVE) {
        button->tracking_mouse_leave = 0;
        if (hovered_button_id == button->id) {
            KillTimer(window, RESET_TOOLTIP_TIMER_ID);
            update_reset_tooltip(window, 0);
            hovered_button_id = 0;
            hovered_reset_id = 0;
            hovered_hint_id = 0;
            if (button->id != ID_BLOCK_LETTERS_TITLE)
                InvalidateRect(window, NULL, FALSE);
        }
    } else if (message == WM_TIMER && wparam == RESET_TOOLTIP_TIMER_ID) {
        KillTimer(window, RESET_TOOLTIP_TIMER_ID);
        if (hovered_hint_id != 0)
            update_reset_tooltip(window, hovered_hint_id);
        return 0;
    } else if (message == WM_LBUTTONDOWN) {
        POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int reset_id = shortcut_reset_at(window, point);
        if (reset_id != 0) {
            pressed_reset_id = reset_id;
            SetFocus(window);
            SetCapture(window);
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
    } else if (message == WM_LBUTTONUP && pressed_reset_id != 0) {
        POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        int reset_id = pressed_reset_id;
        int activate = shortcut_reset_at(window, point) == reset_id;
        pressed_reset_id = 0;
        if (GetCapture() == window)
            ReleaseCapture();
        InvalidateRect(window, NULL, FALSE);
        if (activate)
            SendMessageW(GetParent(window), WM_COMMAND,
                         MAKEWPARAM(reset_id, BN_CLICKED), (LPARAM)window);
        return 0;
    } else if (message == WM_CAPTURECHANGED && pressed_reset_id != 0) {
        pressed_reset_id = 0;
        InvalidateRect(window, NULL, FALSE);
    } else if (message == WM_NCDESTROY) {
        if (hovered_button_id == button->id)
            hovered_button_id = 0;
        ZeroMemory(button, sizeof(*button));
        return CallWindowProcW(original_proc, window, message, wparam, lparam);
    }
    return CallWindowProcW(original_proc, window, message, wparam, lparam);
}

static void attach_hover_tracking(HWND window, int id)
{
    size_t index;
    hover_button *button = NULL;
    WNDPROC original_proc;
    for (index = 0; index < sizeof(hover_buttons) / sizeof(hover_buttons[0]); ++index) {
        if (hover_buttons[index].window == NULL) {
            button = &hover_buttons[index];
            break;
        }
    }
    if (button == NULL)
        return;
    original_proc = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (original_proc == NULL)
        return;
    button->window = window;
    button->original_proc = original_proc;
    button->id = id;
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(window, GWLP_WNDPROC,
                          (LONG_PTR)hover_button_proc) == 0 &&
        GetLastError() != ERROR_SUCCESS)
        ZeroMemory(button, sizeof(*button));
}

static void show_error(const wchar_t *message)
{
    MessageBoxW(settings_window, message, L"Offset Pad", MB_OK | MB_ICONERROR);
}

void ui_show_mode_reminder(void)
{
    app_tray_show_mode_reminder();
}
static void set_update_available_indicator(void)
{
    update_available = 1;
    if (update_link_button != NULL) {
        ShowWindow(update_link_button, SW_SHOWNA);
        InvalidateRect(update_link_button, NULL, FALSE);
    }
}

void ui_restore_update_available(void)
{
    set_update_available_indicator();
}

void ui_key_preview(size_t source_index, int down)
{
    unsigned int mask;
    if (settings_window == NULL || source_index >= KEYMAP_KEY_COUNT)
        return;
    mask = 1U << source_index;
    if (down)
        pressed_source_keys |= mask;
    else
        pressed_source_keys &= ~mask;
    InvalidateRect(settings_window, NULL, FALSE);
    UpdateWindow(settings_window);
}

void ui_show_update_available(const wchar_t *version)
{
    if (version == NULL)
        return;
    set_update_available_indicator();
    app_tray_show_update_available(version);
}
void ui_hotkey_capture_result(WPARAM result)
{
    switch (result) {
    case KEYMAP_CAPTURE_INVALID_ALT:
        lstrcpynW(hotkey_capture_status,
                  L"不支持 Alt+单键/Alt+Shift/Alt+Caps：易失焦",
                  (int)(sizeof(hotkey_capture_status) / sizeof(hotkey_capture_status[0])));
        break;
    case KEYMAP_CAPTURE_INVALID_COUNT:
        lstrcpynW(hotkey_capture_status,
                  L"快捷键需由 2–4 个键组成，且至少包含一个修饰键",
                  (int)(sizeof(hotkey_capture_status) / sizeof(hotkey_capture_status[0])));
        break;
    case KEYMAP_CAPTURE_INVALID:
        lstrcpynW(hotkey_capture_status,
                  L"快捷键已占用或与另一个快捷键冲突，请换一个组合",
                  (int)(sizeof(hotkey_capture_status) / sizeof(hotkey_capture_status[0])));
        break;
    default:
        hotkey_capture_status[0] = L'\0';
        break;
    }
    ui_refresh();
}
void ui_refresh(void)
{
    if (settings_window != NULL) {
        InvalidateRect(settings_window, NULL, FALSE);
        SendMessageW(settings_window, WM_SETICON, ICON_SMALL, (LPARAM)current_icon());
        SendMessageW(settings_window, WM_SETICON, ICON_BIG, (LPARAM)current_logo());
    }
    if (mode_badge_button != NULL) {
        SetWindowTextW(mode_badge_button, keymap_is_visual_enabled() ? L"ON" : L"OFF");
        InvalidateRect(mode_badge_button, NULL, FALSE);
    }
    if (autostart_check != NULL)
        InvalidateRect(autostart_check, NULL, FALSE);
    if (block_letters_check != NULL)
        InvalidateRect(block_letters_check, NULL, FALSE);
    if (hotkey_button != NULL)
        InvalidateRect(hotkey_button, NULL, FALSE);
    if (hold_hotkey_button != NULL)
        InvalidateRect(hold_hotkey_button, NULL, FALSE);
    if (auto_updates_check != NULL)
        InvalidateRect(auto_updates_check, NULL, FALSE);
    if (update_link_button != NULL)
        InvalidateRect(update_link_button, NULL, FALSE);
    app_tray_refresh();
}

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        hotkey_button = CreateWindowExW(0, L"BUTTON", L"按下切换模式 · 快捷键",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                         scale(24), scale(375), scale(188), scale(46),
                                         window, (HMENU)(INT_PTR)ID_HOTKEY, instance, NULL);
        hold_hotkey_button = CreateWindowExW(0, L"BUTTON", L"按住输入 · 快捷键",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                              scale(224), scale(375), scale(176), scale(46),
                                              window, (HMENU)(INT_PTR)ID_HOLD_HOTKEY, instance, NULL);
        CreateWindowExW(0, L"STATIC", L"屏蔽未映射字符",
                                              WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                              scale(24), scale(431), scale(196), scale(46),
                                              window, (HMENU)(INT_PTR)ID_BLOCK_LETTERS_CARD, instance, NULL);
        CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"",
                        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
                        scale(38), scale(444), scale(128), scale(20),
                        window, (HMENU)(INT_PTR)ID_BLOCK_LETTERS_TITLE, instance, NULL);
        CreateWindowExW(0, L"STATIC", L"开机时启动",
                                           WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                                           scale(232), scale(431), scale(168), scale(46),
                                           window, (HMENU)(INT_PTR)ID_AUTOSTART_CARD, instance, NULL);

        block_letters_check = CreateWindowExW(0, L"BUTTON", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        scale(166), scale(442), scale(42), scale(24),
                        window, (HMENU)(INT_PTR)ID_BLOCK_LETTERS, instance, NULL);
        autostart_check = CreateWindowExW(0, L"BUTTON", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        scale(346), scale(442), scale(42), scale(24),
                        window, (HMENU)(INT_PTR)ID_AUTOSTART, instance, NULL);
        auto_updates_check = CreateWindowExW(0, L"BUTTON", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        scale(24), scale(492), scale(16), scale(16),
                        window, (HMENU)(INT_PTR)ID_AUTO_UPDATES, instance, NULL);
        CreateWindowExW(0, L"BUTTON", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        scale(372), scale(212), scale(24), scale(24),
                        window, (HMENU)(INT_PTR)ID_RESET_KEYMAP, instance, NULL);
        update_link_button = CreateWindowExW(0, L"BUTTON", L"有可用更新，点击查看",
                        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW |
                        (update_available ? WS_VISIBLE : 0),
                        header_update_button_left(), scale(23), scale(24), scale(24),
                        window, (HMENU)(INT_PTR)ID_UPDATE_LINK, instance, NULL);
        mode_badge_button = CreateWindowExW(0, L"BUTTON", keymap_is_visual_enabled() ? L"ON" : L"OFF",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        scale(341), scale(112), scale(40), scale(25),
                        window, (HMENU)(INT_PTR)ID_MODE_BADGE, instance, NULL);
        attach_hover_tracking(GetDlgItem(window, ID_HOTKEY), ID_HOTKEY);
        attach_hover_tracking(GetDlgItem(window, ID_HOLD_HOTKEY), ID_HOLD_HOTKEY);
        attach_hover_tracking(GetDlgItem(window, ID_BLOCK_LETTERS), ID_BLOCK_LETTERS);
        attach_hover_tracking(GetDlgItem(window, ID_BLOCK_LETTERS_TITLE), ID_BLOCK_LETTERS_TITLE);
        attach_hover_tracking(GetDlgItem(window, ID_AUTOSTART), ID_AUTOSTART);
        attach_hover_tracking(GetDlgItem(window, ID_AUTO_UPDATES), ID_AUTO_UPDATES);
        attach_hover_tracking(GetDlgItem(window, ID_UPDATE_LINK), ID_UPDATE_LINK);
        attach_hover_tracking(GetDlgItem(window, ID_MODE_BADGE), ID_MODE_BADGE);
        attach_hover_tracking(GetDlgItem(window, ID_RESET_KEYMAP), ID_RESET_KEYMAP);
        shortcut_tooltip = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                RESET_HINT_CLASS, L"", WS_POPUP,
                0, 0, scale(96), scale(28), window, NULL, instance, NULL);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT point;
            HWND hovered = (HWND)wparam;
            int id = GetParent(hovered) == window ? GetDlgCtrlID(hovered) : 0;
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            if (source_key_at(point) >= 0 ||
                id == ID_HOTKEY || id == ID_HOLD_HOTKEY ||
                id == ID_AUTOSTART || id == ID_BLOCK_LETTERS ||
                id == ID_AUTO_UPDATES || id == ID_UPDATE_LINK ||
                id == ID_MODE_BADGE || id == ID_RESET_KEYMAP)
                SetCursor(LoadCursorW(NULL, IDC_HAND));
            else
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT paint;
            RECT client;
            HDC dc = BeginPaint(window, &paint);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap;
            HGDIOBJ old_bitmap;
            GetClientRect(window, &client);
            bitmap = memory != NULL ? CreateCompatibleBitmap(dc, client.right, client.bottom) : NULL;
            if (bitmap != NULL) {
                old_bitmap = SelectObject(memory, bitmap);
                app_ui_paint_settings(memory, &client, paint_state());
                BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, old_bitmap);
                DeleteObject(bitmap);
            } else {
                app_ui_paint_settings(dc, &client, paint_state());
            }
            if (memory != NULL)
                DeleteDC(memory);
            EndPaint(window, &paint);
        }
        return 0;
    case WM_DRAWITEM:
        if (lparam != 0) {
            const DRAWITEMSTRUCT *item = (const DRAWITEMSTRUCT *)lparam;
            if (item->CtlType == ODT_STATIC) {
                if (item->CtlID == ID_BLOCK_LETTERS_TITLE)
                    return TRUE;
                app_ui_paint_setting_card(item, paint_state());
                return TRUE;
            }
            if (item->CtlType == ODT_BUTTON) {
                app_ui_paint_button(item, paint_state());
                return TRUE;
            }
        }
        break;
    case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, HOVER_DEFAULT};
            POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            int key_index;
            if (!mouse_leave_tracking && TrackMouseEvent(&tracking))
                mouse_leave_tracking = 1;
            key_index = source_key_at(point);
            if (hovered_source_key != key_index) {
                hovered_source_key = key_index;
                InvalidateRect(window, NULL, FALSE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        mouse_leave_tracking = 0;
        if (hovered_source_key >= 0) {
            hovered_source_key = -1;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        {
            POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
            int index = source_key_at(point);
            if (index >= 0) {
                keymap_cancel_capture();
                keymap_begin_source_capture((size_t)index);
                ui_refresh();
                return 0;
            }
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_HOTKEY:
            if (keymap_is_capturing())
                keymap_cancel_capture();
            else {
                hotkey_capture_status[0] = L'\0';
                keymap_begin_capture();
            }
            ui_refresh();
            return 0;
        case ID_HOLD_HOTKEY:
            if (keymap_is_hold_capturing())
                keymap_cancel_capture();
            else {
                hotkey_capture_status[0] = L'\0';
                keymap_begin_hold_capture();
            }
            ui_refresh();
            return 0;
        case ID_RESET_HOTKEY:
            {
                keymap_hotkey value = settings_default_hotkey();
                keymap_cancel_capture();
                hotkey_capture_status[0] = L'\0';
                if (!settings_save_hotkey(value))
                    show_error(L"无法保存快捷键设置。");
                else
                    keymap_set_hotkey(value);
                ui_refresh();
                return 0;
            }
        case ID_RESET_HOLD_HOTKEY:
            {
                keymap_hotkey value = settings_default_hold_hotkey();
                keymap_cancel_capture();
                hotkey_capture_status[0] = L'\0';
                if (!settings_save_hold_hotkey(value))
                    show_error(L"无法保存快捷键设置。");
                else
                    keymap_set_hold_hotkey(value);
                ui_refresh();
                return 0;
            }
        case ID_RESET_KEYMAP:
            {
                DWORD sources[KEYMAP_KEY_COUNT];
                keymap_cancel_capture();
                hotkey_capture_status[0] = L'\0';
                keymap_get_default_sources(sources);
                if (!settings_save_sources(sources))
                    show_error(L"\x65E0\x6CD5\x4FDD\x5B58\x6309\x952E\x6620\x5C04\x8BBE\x7F6E\x3002");
                else
                    keymap_set_sources(sources);
                ui_refresh();
                return 0;
            }
        case ID_MODE_BADGE:
            keymap_cancel_capture();
            actions.set_enabled(!keymap_is_enabled());
            return 0;
        case ID_BLOCK_LETTERS:
            {
                int enabled = !keymap_block_unmapped_enabled();
                keymap_cancel_capture();
                if (!settings_save_block_unmapped(enabled))
                    show_error(L"无法保存屏蔽未映射字符设置。");
                else
                    keymap_set_block_unmapped(enabled);
                ui_refresh();
                return 0;
            }
        case ID_AUTOSTART:
            keymap_cancel_capture();
            if (!settings_set_autostart(!settings_autostart_enabled()))
                show_error(L"无法保存开机时启动设置。");
            ui_refresh();
            return 0;
        case ID_UPDATE_LINK:
            ShellExecuteW(NULL, L"open",
                          app_tray_update_url(),
                          NULL, NULL, SW_SHOWNORMAL);
            return 0;
        case ID_AUTO_UPDATES:
            keymap_cancel_capture();
            if (!settings_save_auto_updates(!settings_load_auto_updates()))
                show_error(L"无法保存自动检查更新设置。");
            ui_refresh();
            return 0;
        }
        break;
    case WM_CLOSE:
        keymap_cancel_capture();
        DestroyWindow(window);
        actions.restart_background();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE &&
            (keymap_is_capturing() || keymap_is_hold_capturing() ||
             keymap_is_source_capturing())) {
            keymap_cancel_capture();
            ui_refresh();
        }
        return 0;
    case WM_DESTROY:
        if (shortcut_tooltip != NULL) {
            DestroyWindow(shortcut_tooltip);
            shortcut_tooltip = NULL;
            active_hint_id = 0;
        }
        settings_window = NULL;
        autostart_check = NULL;
        block_letters_check = NULL;
        hotkey_button = NULL;
        hold_hotkey_button = NULL;
        auto_updates_check = NULL;
        update_link_button = NULL;
        mode_badge_button = NULL;
        keymap_set_preview_enabled(0);
        pressed_source_keys = 0;
        hovered_source_key = -1;
        mouse_leave_tracking = 0;
        hovered_button_id = 0;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int ui_init(HINSTANCE app_instance, HWND window, HICON inactive_icon,
            HICON active_icon, HICON inactive_tray_icon,
            HICON active_tray_icon, const app_ui_actions *callbacks)
{
    WNDCLASSW definition = {0};
    WNDCLASSW hint_definition = {0};
    instance = app_instance;
    message_window = window;
    light_icon = inactive_icon;
    dark_icon = active_icon;
    actions = *callbacks;
    definition.lpfnWndProc = settings_proc;
    definition.hInstance = instance;
    definition.hIcon = current_icon();
    definition.hCursor = LoadCursorW(NULL, IDC_ARROW);
    definition.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    definition.lpszClassName = settings_class;
    if (!RegisterClassW(&definition))
        return 0;
    hint_definition.lpfnWndProc = reset_hint_proc;
    hint_definition.hInstance = instance;
    hint_definition.hCursor = LoadCursorW(NULL, IDC_ARROW);
    hint_definition.lpszClassName = RESET_HINT_CLASS;
    if (!RegisterClassW(&hint_definition)) {
        UnregisterClassW(settings_class, instance);
        return 0;
    }
    {
        app_tray_callbacks tray_callbacks = {
            ui_show, actions.set_enabled, actions.quit, ui_refresh
        };
        app_tray_init(message_window, WM_OFFSET_PAD_TRAY, inactive_tray_icon,
                      active_tray_icon, &tray_callbacks);
    }
    ui_refresh();
    return 1;
}

void ui_shutdown(void)
{
    if (settings_window != NULL)
        DestroyWindow(settings_window);
    app_tray_shutdown();
    UnregisterClassW(RESET_HINT_CLASS, instance);
    UnregisterClassW(settings_class, instance);
    if (title_font != NULL) DeleteObject(title_font);
    if (heading_font != NULL) DeleteObject(heading_font);
    if (body_font != NULL) DeleteObject(body_font);
    if (control_font != NULL) DeleteObject(control_font);
    if (small_font != NULL) DeleteObject(small_font);
    if (compact_font != NULL) DeleteObject(compact_font);
    if (symbol_font != NULL) DeleteObject(symbol_font);
    if (large_symbol_font != NULL) DeleteObject(large_symbol_font);
    if (keycap_font != NULL) DeleteObject(keycap_font);
    if (icon_font != NULL) DeleteObject(icon_font);
    title_font = NULL;
    heading_font = NULL;
    body_font = NULL;
    control_font = NULL;
    small_font = NULL;
    compact_font = NULL;
    symbol_font = NULL;
    large_symbol_font = NULL;
    keycap_font = NULL;
    icon_font = NULL;
    rounded_box_shutdown();
}

void ui_show(void)
{
    if (settings_window == NULL) {
        RECT rect;
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
        COLORREF caption_color = COLOR_WHITE;
        COLORREF caption_text = COLOR_INK;
        int width;
        int height;
        HDC screen = GetDC(NULL);
        dpi = screen != NULL ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
        if (screen != NULL)
            ReleaseDC(NULL, screen);
        if (dpi <= 0)
            dpi = 96;
        if (light_logo == NULL)
            light_logo = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON_LIGHT),
                                            IMAGE_ICON, scale(48), scale(48), LR_SHARED);
        if (dark_logo == NULL)
            dark_logo = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON_DARK),
                                           IMAGE_ICON, scale(48), scale(48), LR_SHARED);
        rounded_box_init();
        if (title_font == NULL)
            title_font = CreateFontW(-scale(29), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (heading_font == NULL)
            heading_font = CreateFontW(-scale(17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (body_font == NULL)
            body_font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (control_font == NULL)
            control_font = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (small_font == NULL)
            small_font = CreateFontW(-scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (compact_font == NULL)
            compact_font = CreateFontW(-scale(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (symbol_font == NULL)
            symbol_font = CreateFontW(-scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (large_symbol_font == NULL)
            large_symbol_font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (keycap_font == NULL)
            keycap_font = CreateFontW(-scale(9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (icon_font == NULL)
            icon_font = CreateFontW(-scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                    L"Segoe UI Symbol");
        rect = scaled_rect(0, 0, 424, 522);
        AdjustWindowRect(&rect, style, FALSE);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        settings_window = CreateWindowExW(WS_EX_APPWINDOW, settings_class, L"Offset Pad v" OFFSET_PAD_VERSION_W,
                                           style,
                                           (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                                           (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                                           width, height, NULL, NULL, instance, NULL);
        if (settings_window == NULL)
            return;
        DwmSetWindowAttribute(settings_window, 35,
                              &caption_color, sizeof(caption_color));
        DwmSetWindowAttribute(settings_window, 36,
                              &caption_text, sizeof(caption_text));
        SendMessageW(settings_window, WM_SETICON, ICON_SMALL, (LPARAM)current_icon());
        SendMessageW(settings_window, WM_SETICON, ICON_BIG, (LPARAM)current_logo());
    }
    keymap_set_preview_enabled(1);
    ui_refresh();
    ShowWindow(settings_window, SW_SHOWNORMAL);
    SetForegroundWindow(settings_window);
}

int ui_handle_dialog_message(MSG *message)
{
    return settings_window != NULL && IsDialogMessageW(settings_window, message);
}

void ui_tray_message(LPARAM message)
{
    app_tray_handle_message(message);
}

void ui_taskbar_created(void)
{
    app_tray_taskbar_created();
    ui_refresh();
}
