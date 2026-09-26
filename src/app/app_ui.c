#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wchar.h>

#include "app_ui.h"
#include "app_settings.h"
#include "rounded_box.h"
#include "keymap.h"

#define TRAY_ID 1
#define ID_TOGGLE 101
#define ID_AUTOSTART 102
#define ID_CLOSE 103
#define ID_HOTKEY 104
#define ID_BLOCK_LETTERS 105
#define ID_HOLD_HOTKEY 106
#define MENU_OPEN 201
#define MENU_TOGGLE 202
#define MENU_AUTOSTART 203
#define MENU_EXIT 204
#define MENU_DISABLE_HOTKEYS 205
#define IDI_APP_ICON_LIGHT 101
#define IDI_APP_ICON_DARK 102

static const wchar_t settings_class[] = L"OffsetPadSettingsWindow";
static HINSTANCE instance;
static HWND message_window;
static HWND settings_window;
static HWND toggle_button;
static HWND autostart_check;
static HWND block_letters_check;
static HWND hotkey_button;
static HWND hold_hotkey_button;
static HFONT title_font;
static HFONT heading_font;
static HFONT body_font;
static HFONT control_font;
static HFONT small_font;
static HFONT compact_font;
static HICON light_icon;
static HICON dark_icon;
static HICON tray_o_icon;
static HICON tray_9_icon;
static HICON light_logo;
static HICON dark_logo;
static app_ui_actions actions;
static int tray_added;
static int dpi = 96;

static HICON current_icon(void)
{
    return keymap_is_enabled() ? dark_icon : light_icon;
}

static HICON current_tray_icon(void)
{
    return keymap_is_enabled() ? tray_9_icon : tray_o_icon;
}

static HICON current_logo(void)
{
    HICON logo = keymap_is_enabled() ? dark_logo : light_logo;
    return logo != NULL ? logo : current_icon();
}

#define COLOR_WHITE RGB(255, 255, 255)
#define COLOR_INK RGB(34, 34, 34)
#define COLOR_MUTED RGB(112, 112, 112)
#define COLOR_ACCENT RGB(43, 43, 43)
#define COLOR_ACCENT_DOWN RGB(22, 22, 22)
#define COLOR_TINT RGB(246, 246, 246)
#define COLOR_BORDER RGB(222, 222, 222)
#define COLOR_KEYCAP RGB(252, 252, 252)
#define COLOR_KEYCAP_OUTPUT RGB(244, 244, 244)

static int scale(int value)
{
    return MulDiv(value, dpi, 96);
}

static RECT scaled_rect(int left, int top, int right, int bottom)
{
    RECT rect = {scale(left), scale(top), scale(right), scale(bottom)};
    return rect;
}

static RECT source_key_rect(size_t index)
{
    int row;
    int column;
    int left;
    if (index == 9)
        return scaled_rect(74, 307, 177, 331);
    row = (int)index / 3;
    column = (int)index % 3;
    left = 74 + (row == 1 ? 11 : 0) + column * 37;
    return scaled_rect(left, 220 + row * 29, left + 29, 244 + row * 29);
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

static void rounded_box(HDC dc, RECT rect, COLORREF fill, COLORREF outline, int radius)
{
    rounded_box_draw(dc, rect, fill, outline, scale(radius));
}

static void draw_label(HDC dc, const wchar_t *label, RECT rect,
                       HFONT font, COLORREF color, UINT format)
{
    HGDIOBJ old_font = SelectObject(dc, font != NULL ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, label, -1, &rect, format | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

static void draw_keycap(HDC dc, int left, int top, int width,
                        const wchar_t *label, int output, int selected)
{
    RECT rect = scaled_rect(left, top, left + width, top + 24);
    rounded_box(dc, rect,
                selected ? COLOR_ACCENT : (output ? COLOR_KEYCAP_OUTPUT : COLOR_KEYCAP),
                selected ? COLOR_ACCENT : COLOR_BORDER, 7);
    draw_label(dc, label, rect,
               width <= 40 && wcslen(label) > 2 ? small_font : body_font,
               selected ? COLOR_WHITE : COLOR_INK,
               DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

static int hotkey_is_set(keymap_hotkey hotkey)
{
    return hotkey.modifiers != 0 || hotkey.key != 0;
}

static void paint_settings(HDC dc, const RECT *client)
{
    RECT rect;
    wchar_t shortcut[96];
    wchar_t hint[192];
    HBRUSH background = CreateSolidBrush(COLOR_WHITE);
    COLORREF accent = keymap_is_enabled() ? COLOR_ACCENT : COLOR_MUTED;
    FillRect(dc, client, background);
    DeleteObject(background);
    if (keymap_is_capturing() || keymap_is_hold_capturing()) {
        lstrcpynW(hint, L"录入快捷键：Del/Back 清除，Esc 取消",
                  (int)(sizeof(hint) / sizeof(hint[0])));
    } else if (hotkey_is_set(keymap_get_hotkey())) {
        keymap_format_hotkey(shortcut, sizeof(shortcut) / sizeof(shortcut[0]),
                             keymap_get_hotkey());
        swprintf(hint, sizeof(hint) / sizeof(hint[0]),
                 L"按 %ls 随时切换模式", shortcut);
    } else if (hotkey_is_set(keymap_get_hold_hotkey())) {
        keymap_format_hotkey(shortcut, sizeof(shortcut) / sizeof(shortcut[0]),
                             keymap_get_hold_hotkey());
        swprintf(hint, sizeof(hint) / sizeof(hint[0]),
                 L"按住 %ls 后切换为小键盘模式", shortcut);
    } else {
        lstrcpynW(hint, L"请先设置一个快捷键",
                  (int)(sizeof(hint) / sizeof(hint[0])));
    }

    DrawIconEx(dc, scale(24), scale(20), current_logo(), scale(48), scale(48),
               0, NULL, DI_NORMAL);
    rect = scaled_rect(84, 18, 310, 51);
    draw_label(dc, L"Offset Pad", rect, title_font, COLOR_INK,
               DT_SINGLELINE | DT_VCENTER);
    rect = scaled_rect(85, 51, 390, 73);
    draw_label(dc, L"Developed by OrlandoHsu29", rect, small_font,
               COLOR_MUTED, DT_SINGLELINE | DT_VCENTER);

    rect = scaled_rect(24, 89, 400, 164);
    rounded_box(dc, rect, COLOR_TINT, COLOR_TINT, 14);
    rect = scaled_rect(43, 107, 332, 134);
    draw_label(dc, keymap_is_enabled() ? L"数字小键盘已开启" : L"当前为普通键盘模式",
               rect, heading_font, COLOR_INK, DT_SINGLELINE | DT_VCENTER);
    rect = scaled_rect(43, 135, 386, 154);
    draw_label(dc, hint, rect, small_font,
               COLOR_MUTED, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    rect = scaled_rect(341, 112, 381, 137);
    rounded_box(dc, rect, keymap_is_enabled() ? COLOR_ACCENT : COLOR_WHITE,
                keymap_is_enabled() ? COLOR_ACCENT : COLOR_BORDER, 12);
    draw_label(dc, keymap_is_enabled() ? L"ON" : L"OFF", rect, small_font,
               keymap_is_enabled() ? COLOR_WHITE : accent,
               DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    rect = scaled_rect(24, 179, 250, 205);
    draw_label(dc, L"按键映射", rect, heading_font, COLOR_INK,
               DT_SINGLELINE | DT_VCENTER);
    rect = scaled_rect(202, 185, 400, 203);
    draw_label(dc, keymap_is_source_capturing()
                   ? L"按新按键 · Esc 取消" : L"点击左侧键帽图按键可修改映射",
               rect, small_font, COLOR_MUTED,
               DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    rect = scaled_rect(24, 211, 400, 341);
    rounded_box(dc, rect, COLOR_WHITE, COLOR_BORDER, 12);
    {
        static const wchar_t *target_keys[3][3] = {
            {L"7", L"8", L"9"},
            {L"4", L"5", L"6"},
            {L"1", L"2", L"3"}
        };
        int row;
        int column;
        for (row = 0; row < 4; ++row) {
            int top = 220 + row * 29;
            if (row < 3) {
                int source_left = 74 + (row == 1 ? 11 : 0);
                for (column = 0; column < 3; ++column) {
                    size_t index = (size_t)(row * 3 + column);
                    wchar_t source_label[16];
                    int selected = keymap_is_source_capturing() &&
                                   keymap_capturing_source() == index;
                    keymap_format_source(source_label,
                                         sizeof(source_label) / sizeof(source_label[0]),
                                         keymap_get_source(index));
                    draw_keycap(dc, source_left + column * 37, top, 29,
                                selected ? L"?" :
                                keymap_get_source(index) == VK_CAPITAL ? L"CL" : source_label,
                                0, selected);
                    draw_keycap(dc, 275 + column * 37, top, 29,
                                target_keys[row][column], 1, 0);
                }
            } else {
                int selected = keymap_is_source_capturing() &&
                               keymap_capturing_source() == 9;
                wchar_t source_label[16];
                keymap_format_source(source_label,
                                     sizeof(source_label) / sizeof(source_label[0]),
                                     keymap_get_source(9));
                draw_keycap(dc, 74, top, 103,
                            selected ? L"按键" : source_label, 0, selected);
                draw_keycap(dc, 294, top, 66, L"0", 1, 0);
            }
            rect = scaled_rect(215, top, 240, top + 24);
            draw_label(dc, L"→", rect, body_font, COLOR_MUTED,
                       DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
    }
    rect = scaled_rect(24, 514, 400, 533);
    draw_label(dc, L"关闭窗口后继续在托盘运行", rect, small_font, COLOR_MUTED,
               DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

static void draw_shortcut_value(HDC dc, const wchar_t *shortcut, RECT rect)
{
    wchar_t buffer[96];
    wchar_t lines[2][96] = {{0}};
    wchar_t candidate[96];
    wchar_t *part;
    int part_count = 0;
    int line_count;
    int line_height;
    int top;
    int index;
    int use_compact = 0;
    SIZE extent;
    HFONT value_font;
    HGDIOBJ old_font = SelectObject(dc, small_font != NULL ? small_font :
                                    GetStockObject(DEFAULT_GUI_FONT));
    lstrcpynW(buffer, shortcut, (int)(sizeof(buffer) / sizeof(buffer[0])));
    part = buffer;
    while (*part != L'\0' && part_count < 5) {
        wchar_t *separator = wcsstr(part, L" + ");
        int line = part_count < 2 ? 0 : 1;
        if (separator != NULL)
            *separator = L'\0';
        if (part_count == 0) {
            lstrcpynW(lines[0], part,
                      (int)(sizeof(lines[0]) / sizeof(lines[0][0])));
        } else if (part_count == 2) {
            swprintf(lines[1], sizeof(lines[1]) / sizeof(lines[1][0]),
                     L"+ %ls", part);
        } else {
            swprintf(candidate, sizeof(candidate) / sizeof(candidate[0]),
                     L"%ls + %ls", lines[line], part);
            lstrcpynW(lines[line], candidate,
                      (int)(sizeof(lines[line]) / sizeof(lines[line][0])));
        }
        ++part_count;
        if (separator == NULL)
            break;
        part = separator + 3;
    }
    line_count = part_count > 2 ? 2 : part_count;
    for (index = 0; index < line_count; ++index) {
        GetTextExtentPoint32W(dc, lines[index], (int)wcslen(lines[index]),
                              &extent);
        if (extent.cx > rect.right - rect.left)
            use_compact = 1;
    }
    SelectObject(dc, old_font);
    value_font = use_compact && compact_font != NULL ? compact_font : small_font;
    line_height = scale(15);
    top = rect.top + (rect.bottom - rect.top - line_count * line_height) / 2;
    for (index = 0; index < line_count; ++index) {
        RECT line_rect = {rect.left, top + index * line_height,
                          rect.right, top + (index + 1) * line_height};
        draw_label(dc, lines[index], line_rect, value_font, COLOR_MUTED,
                   DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

static void draw_button(const DRAWITEMSTRUCT *item)
{
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    int pressed = (item->itemState & ODS_SELECTED) != 0;
    int focused = (item->itemState & ODS_FOCUS) != 0;
    COLORREF fill = COLOR_WHITE;
    COLORREF border = COLOR_BORDER;
    COLORREF ink = COLOR_INK;
    const wchar_t *label;
    FillRect(dc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

    if (item->CtlID == ID_HOTKEY || item->CtlID == ID_HOLD_HOTKEY) {
        wchar_t shortcut[96];
        RECT name_rect = rect;
        RECT value_rect = rect;
        int is_hold = item->CtlID == ID_HOLD_HOTKEY;
        fill = pressed ? COLOR_TINT : COLOR_WHITE;
        rounded_box(dc, rect, fill, focused ? COLOR_ACCENT : COLOR_BORDER, 12);
        name_rect.left += scale(14);
        name_rect.right = name_rect.left + scale(is_hold ? 80 : 92);
        draw_label(dc, is_hold ? L"按住时输入" : L"按下切换模式",
                   name_rect, control_font, COLOR_INK,
                   DT_SINGLELINE | DT_VCENTER);
        value_rect.left += scale(is_hold ? 100 : 112);
        value_rect.right -= scale(14);
        if (is_hold && keymap_is_hold_capturing())
            lstrcpynW(shortcut, L"录入中",
                      (int)(sizeof(shortcut) / sizeof(shortcut[0])));
        else if (!is_hold && keymap_is_capturing())
            lstrcpynW(shortcut, L"录入中",
                      (int)(sizeof(shortcut) / sizeof(shortcut[0])));
        else if (!hotkey_is_set(is_hold ? keymap_get_hold_hotkey() :
                                keymap_get_hotkey()))
            lstrcpynW(shortcut, L"未设置",
                      (int)(sizeof(shortcut) / sizeof(shortcut[0])));
        else
            keymap_format_hotkey(shortcut, sizeof(shortcut) / sizeof(shortcut[0]),
                                 is_hold ? keymap_get_hold_hotkey() : keymap_get_hotkey());
        draw_shortcut_value(dc, shortcut, value_rect);
        return;
    }

    if (item->CtlID == ID_AUTOSTART || item->CtlID == ID_BLOCK_LETTERS) {
        int enabled = item->CtlID == ID_AUTOSTART ?
                      settings_autostart_enabled() : keymap_block_letters_enabled();
        RECT switch_rect = rect;
        RECT knob;
        fill = pressed ? COLOR_TINT : COLOR_WHITE;
        rounded_box(dc, rect, fill, focused ? COLOR_ACCENT : COLOR_BORDER, 12);
        rect.left += scale(14);
        rect.right -= scale(58);
        draw_label(dc, item->CtlID == ID_AUTOSTART ? L"开机时启动" : L"屏蔽字母防误触",
                   rect, control_font, COLOR_INK,
                   DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        switch_rect.left = switch_rect.right - scale(54);
        switch_rect.right -= scale(12);
        switch_rect.top += scale(11);
        switch_rect.bottom -= scale(11);
        rounded_box(dc, switch_rect, enabled ? COLOR_ACCENT : COLOR_BORDER,
                    enabled ? COLOR_ACCENT : COLOR_BORDER, 24);
        knob = switch_rect;
        knob.top += scale(3);
        knob.bottom -= scale(3);
        if (enabled) {
            knob.right -= scale(3);
            knob.left = knob.right - scale(17);
        } else {
            knob.left += scale(3);
            knob.right = knob.left + scale(17);
        }
        rounded_box(dc, knob, COLOR_WHITE, COLOR_WHITE, 17);
        return;
    }

    if (item->CtlID == ID_TOGGLE) {
        fill = pressed ? COLOR_ACCENT_DOWN : COLOR_ACCENT;
        border = fill;
        ink = COLOR_WHITE;
        label = keymap_is_latched() ? L"关闭小键盘" : L"开启小键盘";
    } else {
        fill = pressed ? COLOR_TINT : COLOR_WHITE;
        label = L"关闭";
    }
    rounded_box(dc, rect, fill, focused ? COLOR_ACCENT_DOWN : border, 11);
    draw_label(dc, label, rect, body_font, ink,
               DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

static void show_error(const wchar_t *message)
{
    MessageBoxW(settings_window, message, L"Offset Pad", MB_OK | MB_ICONERROR);
}

static void add_tray(void)
{
    NOTIFYICONDATAW data = {0};
    if (tray_added)
        return;
    data.cbSize = sizeof(data);
    data.hWnd = message_window;
    data.uID = TRAY_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_OFFSET_PAD_TRAY;
    data.hIcon = current_tray_icon();
    lstrcpynW(data.szTip, L"Offset Pad", sizeof(data.szTip) / sizeof(data.szTip[0]));
    tray_added = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (tray_added) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

void ui_show_mode_reminder(void)
{
    NOTIFYICONDATAW data = {0};
    wchar_t shortcut[96];
    if (!tray_added || !keymap_is_enabled())
        return;
    keymap_format_hotkey(shortcut, sizeof(shortcut) / sizeof(shortcut[0]),
                         keymap_get_hotkey());
    data.cbSize = sizeof(data);
    data.hWnd = message_window;
    data.uID = TRAY_ID;
    data.uFlags = NIF_INFO | NIF_REALTIME;
    data.dwInfoFlags = NIIF_INFO;
    lstrcpynW(data.szInfoTitle, L"Offset Pad",
              (int)(sizeof(data.szInfoTitle) / sizeof(data.szInfoTitle[0])));
    if (keymap_is_latched())
        swprintf(data.szInfo, sizeof(data.szInfo) / sizeof(data.szInfo[0]),
                 L"当前处于小键盘模式。按 %ls 切回普通键盘。", shortcut);
    else
        lstrcpynW(data.szInfo, L"当前处于临时小键盘模式。松开按住快捷键即可恢复普通键盘。",
                  (int)(sizeof(data.szInfo) / sizeof(data.szInfo[0])));
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void ui_refresh(void)
{
    NOTIFYICONDATAW data = {0};
    if (settings_window != NULL) {
        InvalidateRect(settings_window, NULL, FALSE);
        SendMessageW(settings_window, WM_SETICON, ICON_SMALL, (LPARAM)current_icon());
        SendMessageW(settings_window, WM_SETICON, ICON_BIG, (LPARAM)current_logo());
    }
    if (toggle_button != NULL)
        InvalidateRect(toggle_button, NULL, FALSE);
    if (autostart_check != NULL)
        InvalidateRect(autostart_check, NULL, FALSE);
    if (block_letters_check != NULL)
        InvalidateRect(block_letters_check, NULL, FALSE);
    if (hotkey_button != NULL)
        InvalidateRect(hotkey_button, NULL, FALSE);
    if (hold_hotkey_button != NULL)
        InvalidateRect(hold_hotkey_button, NULL, FALSE);
    if (tray_added) {
        data.cbSize = sizeof(data);
        data.hWnd = message_window;
        data.uID = TRAY_ID;
        data.uFlags = NIF_TIP | NIF_ICON | NIF_SHOWTIP;
        data.hIcon = current_tray_icon();
        lstrcpynW(data.szTip, L"Offset Pad",
                  sizeof(data.szTip) / sizeof(data.szTip[0]));
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
}

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        hotkey_button = CreateWindowExW(0, L"BUTTON", L"按下切换模式",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                         scale(24), scale(353), scale(188), scale(46),
                                         window, (HMENU)(INT_PTR)ID_HOTKEY, instance, NULL);
        hold_hotkey_button = CreateWindowExW(0, L"BUTTON", L"按住输入",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                              scale(224), scale(353), scale(176), scale(46),
                                              window, (HMENU)(INT_PTR)ID_HOLD_HOTKEY, instance, NULL);
        block_letters_check = CreateWindowExW(0, L"BUTTON", L"屏蔽字母防误触",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                              scale(24), scale(409), scale(200), scale(46),
                                              window, (HMENU)(INT_PTR)ID_BLOCK_LETTERS, instance, NULL);
        autostart_check = CreateWindowExW(0, L"BUTTON", L"开机时启动",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                           scale(236), scale(409), scale(164), scale(46),
                                           window, (HMENU)(INT_PTR)ID_AUTOSTART, instance, NULL);
        toggle_button = CreateWindowExW(0, L"BUTTON", L"开启小键盘",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                         scale(24), scale(467), scale(180), scale(39),
                                         window, (HMENU)(INT_PTR)ID_TOGGLE, instance, NULL);
        CreateWindowExW(0, L"BUTTON", L"关闭",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        scale(220), scale(467), scale(180), scale(39),
                        window, (HMENU)(INT_PTR)ID_CLOSE, instance, NULL);
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
                id == ID_TOGGLE || id == ID_CLOSE)
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
                paint_settings(memory, &client);
                BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, old_bitmap);
                DeleteObject(bitmap);
            } else {
                paint_settings(dc, &client);
            }
            if (memory != NULL)
                DeleteDC(memory);
            EndPaint(window, &paint);
        }
        return 0;
    case WM_DRAWITEM:
        if (lparam != 0 && ((DRAWITEMSTRUCT *)lparam)->CtlType == ODT_BUTTON) {
            draw_button((const DRAWITEMSTRUCT *)lparam);
            return TRUE;
        }
        break;
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
            else
                keymap_begin_capture();
            ui_refresh();
            return 0;
        case ID_HOLD_HOTKEY:
            if (keymap_is_hold_capturing())
                keymap_cancel_capture();
            else
                keymap_begin_hold_capture();
            ui_refresh();
            return 0;
        case ID_TOGGLE:
            keymap_cancel_capture();
            actions.set_enabled(!keymap_is_latched());
            return 0;
        case ID_BLOCK_LETTERS:
            {
                int enabled = !keymap_block_letters_enabled();
                keymap_cancel_capture();
                if (!settings_save_block_letters(enabled))
                    show_error(L"无法保存屏蔽字母防误触设置。");
                else
                    keymap_set_block_letters(enabled);
                ui_refresh();
                return 0;
            }
        case ID_AUTOSTART:
            keymap_cancel_capture();
            if (!settings_set_autostart(!settings_autostart_enabled()))
                show_error(L"无法保存开机时启动设置。");
            ui_refresh();
            return 0;
        case ID_CLOSE:
            PostMessageW(window, WM_CLOSE, 0, 0);
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
        settings_window = NULL;
        toggle_button = NULL;
        autostart_check = NULL;
        block_letters_check = NULL;
        hotkey_button = NULL;
        hold_hotkey_button = NULL;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int ui_init(HINSTANCE app_instance, HWND window, HICON inactive_icon,
            HICON active_icon, HICON inactive_tray_icon,
            HICON active_tray_icon, const app_ui_actions *callbacks)
{
    WNDCLASSW definition = {0};
    instance = app_instance;
    message_window = window;
    light_icon = inactive_icon;
    dark_icon = active_icon;
    tray_o_icon = inactive_tray_icon;
    tray_9_icon = active_tray_icon;
    actions = *callbacks;
    definition.lpfnWndProc = settings_proc;
    definition.hInstance = instance;
    definition.hIcon = current_icon();
    definition.hCursor = LoadCursorW(NULL, IDC_ARROW);
    definition.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    definition.lpszClassName = settings_class;
    if (!RegisterClassW(&definition))
        return 0;
    add_tray();
    ui_refresh();
    return 1;
}

void ui_shutdown(void)
{
    NOTIFYICONDATAW data = {0};
    if (settings_window != NULL)
        DestroyWindow(settings_window);
    if (tray_added) {
        data.cbSize = sizeof(data);
        data.hWnd = message_window;
        data.uID = TRAY_ID;
        Shell_NotifyIconW(NIM_DELETE, &data);
        tray_added = 0;
    }
    UnregisterClassW(settings_class, instance);
    if (title_font != NULL) DeleteObject(title_font);
    if (heading_font != NULL) DeleteObject(heading_font);
    if (body_font != NULL) DeleteObject(body_font);
    if (control_font != NULL) DeleteObject(control_font);
    if (small_font != NULL) DeleteObject(small_font);
    if (compact_font != NULL) DeleteObject(compact_font);
    title_font = NULL;
    heading_font = NULL;
    body_font = NULL;
    control_font = NULL;
    small_font = NULL;
    compact_font = NULL;
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
            compact_font = CreateFontW(-scale(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        rect = scaled_rect(0, 0, 424, 542);
        AdjustWindowRect(&rect, style, FALSE);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        settings_window = CreateWindowExW(WS_EX_APPWINDOW, settings_class, L"Offset Pad 设置",
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
    ui_refresh();
    ShowWindow(settings_window, SW_SHOWNORMAL);
    SetForegroundWindow(settings_window);
}

int ui_handle_dialog_message(MSG *message)
{
    return settings_window != NULL && IsDialogMessageW(settings_window, message);
}

static void show_tray_menu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT cursor;
    UINT choice;
    if (menu == NULL)
        return;
    AppendMenuW(menu, MF_STRING, MENU_OPEN, L"打开设置");
    AppendMenuW(menu, MF_STRING, MENU_TOGGLE,
                keymap_is_latched() ? L"关闭小键盘" : L"开启小键盘");
    AppendMenuW(menu, MF_STRING |
                (keymap_hotkeys_enabled() ? 0 : MF_CHECKED),
                MENU_DISABLE_HOTKEYS, L"禁用快捷键");
    AppendMenuW(menu, MF_STRING | (settings_autostart_enabled() ? MF_CHECKED : 0),
                MENU_AUTOSTART, L"开机时启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"退出程序");
    GetCursorPos(&cursor);
    SetForegroundWindow(message_window);
    choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                            cursor.x, cursor.y, 0, message_window, NULL);
    PostMessageW(message_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (choice) {
    case MENU_OPEN: ui_show(); break;
    case MENU_TOGGLE: actions.set_enabled(!keymap_is_latched()); break;
    case MENU_DISABLE_HOTKEYS:
        {
            int enabled = !keymap_hotkeys_enabled();
            if (!settings_save_hotkeys_enabled(enabled))
                show_error(L"无法保存快捷键设置。");
            else
                keymap_set_hotkeys_enabled(enabled);
            ui_refresh();
            break;
        }
    case MENU_AUTOSTART:
        if (!settings_set_autostart(!settings_autostart_enabled()))
            show_error(L"无法保存开机时启动设置。");
        ui_refresh();
        break;
    case MENU_EXIT: actions.quit(); break;
    }
}

void ui_tray_message(LPARAM message)
{
    UINT event = LOWORD(message);
    if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)
        show_tray_menu();
    else if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
        ui_show();
}

void ui_taskbar_created(void)
{
    tray_added = 0;
    add_tray();
    ui_refresh();
}
