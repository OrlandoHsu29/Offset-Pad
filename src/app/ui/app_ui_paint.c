#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "app_ui_paint.h"
#include "app_settings.h"
#include "rounded_box.h"
#include "keymap.h"

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

static app_ui_paint_state current;

#define dpi (current.dpi)
#define title_font (current.title_font)
#define heading_font (current.heading_font)
#define body_font (current.body_font)
#define control_font (current.control_font)
#define small_font (current.small_font)
#define compact_font (current.compact_font)
#define icon_font (current.icon_font)
#define hotkey_capture_status (current.capture_status)
#define hovered_source_key (current.hovered_source_key)
#define pressed_source_keys (current.pressed_source_keys)
#define hovered_button_id (current.hovered_button_id)
#define hovered_reset_id (current.hovered_reset_id)
#define pressed_reset_id (current.pressed_reset_id)
#define active_hint_id (current.active_hint_id)
#define tooltip_tail_center_x (current.tooltip_tail_center_x)
#define current_logo() (current.logo)

static int scale(int value)
{
    return MulDiv(value, dpi, 96);
}

static RECT scaled_rect(int left, int top, int right, int bottom)
{
    RECT rect = {scale(left), scale(top), scale(right), scale(bottom)};
    return rect;
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
                        const wchar_t *label, int output, int selected, int hovered,
                        int pressed)
{
    int active = selected || pressed;
    RECT rect = scaled_rect(left, top, left + width, top + 24);
    COLORREF fill = active ? COLOR_ACCENT :
                    (hovered ? COLOR_KEYCAP_HOVER :
                     (output ? COLOR_KEYCAP_OUTPUT : COLOR_KEYCAP));
    rounded_box(dc, rect, fill,
                active ? COLOR_ACCENT : COLOR_BORDER, 7);
    draw_label(dc, label, rect,
               width <= 40 && wcslen(label) > 2 ? small_font : body_font,
               active ? COLOR_WHITE : COLOR_INK,
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
    COLORREF hint_color = hotkey_capture_status[0] != L'\0' ? RGB(180, 82, 82) : COLOR_MUTED;
    FillRect(dc, client, background);
    DeleteObject(background);
    if (keymap_is_capturing() || keymap_is_hold_capturing()) {
        lstrcpynW(hint, L"录入快捷键：Del/Back 清除，Esc 取消",
                  (int)(sizeof(hint) / sizeof(hint[0])));
    } else if (hotkey_capture_status[0] != L'\0') {
        lstrcpynW(hint, hotkey_capture_status,
                  (int)(sizeof(hint) / sizeof(hint[0])));
    } else if (keymap_is_visual_enabled()) {
        lstrcpynW(hint, L"小键盘模式下，顶部数字行输出对应符号",
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
                 L"按住 %ls 时输入数字，松手恢复  ", shortcut);
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
    rect = scaled_rect(43, 103, 332, 130);
    draw_label(dc, keymap_is_visual_enabled() ? L"数字小键盘已开启" : L"当前为普通键盘模式",
               rect, heading_font, COLOR_INK, DT_SINGLELINE | DT_VCENTER);
    rect = scaled_rect(43, 131, 386, 150);
    draw_label(dc, hint, rect, small_font,
               hint_color, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);


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
        int visual_enabled = keymap_is_visual_enabled();
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
                    int pressed = (pressed_source_keys & (1U << index)) != 0;
                    draw_keycap(dc, source_left + column * 37, top, 29,
                                selected ? L"?" :
                                keymap_get_source(index) == VK_CAPITAL ? L"CL" : source_label,
                                0, selected, hovered_source_key == (int)index,
                                !visual_enabled && pressed);
                    draw_keycap(dc, 275 + column * 37, top, 29,
                                target_keys[row][column], 1, 0, 0,
                                visual_enabled && pressed);
                }
            } else {
                int selected_space = keymap_is_source_capturing() &&
                                     keymap_capturing_source() == 9;
                int selected_decimal = keymap_is_source_capturing() &&
                                       keymap_capturing_source() == 10;
                wchar_t source_label[16];
                int pressed_space = (pressed_source_keys & (1U << 9)) != 0;
                int pressed_decimal = (pressed_source_keys & (1U << 10)) != 0;
                keymap_format_source(source_label,
                                     sizeof(source_label) / sizeof(source_label[0]),
                                     keymap_get_source(9));
                draw_keycap(dc, 74, top, 66,
                            selected_space ? L"\x6309\x952e" : source_label, 0,
                            selected_space, hovered_source_key == 9,
                            !visual_enabled && pressed_space);
                keymap_format_source(source_label,
                                     sizeof(source_label) / sizeof(source_label[0]),
                                     keymap_get_source(10));
                draw_keycap(dc, 148, top, 29,
                            selected_decimal ? L"?" : source_label, 0,
                            selected_decimal, hovered_source_key == 10,
                            !visual_enabled && pressed_decimal);
                draw_keycap(dc, 275, top, 66, L"0", 1, 0, 0,
                            visual_enabled && pressed_space);
                draw_keycap(dc, 349, top, 29, L".", 1, 0, 0,
                            visual_enabled && pressed_decimal);
            }
            rect = scaled_rect(215, top, 240, top + 24);
            draw_label(dc, L"→", rect, body_font, COLOR_MUTED,
                       DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
    }
    rect = scaled_rect(185, 469, 400, 488);
    draw_label(dc, L"关闭窗口后会继续在托盘运行", rect, small_font, COLOR_MUTED,
               DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
    draw_label(dc, L"自动检查更新", scaled_rect(46, 469, 155, 488), small_font, COLOR_MUTED,
               DT_SINGLELINE | DT_LEFT | DT_VCENTER);

}

static void draw_shortcut_value(HDC dc, const wchar_t *shortcut, RECT rect)
{
    draw_label(dc, shortcut, rect, compact_font != NULL ? compact_font : small_font,
               COLOR_MUTED,
               DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
}

static void draw_setting_card(const DRAWITEMSTRUCT *item)
{
    RECT rect = item->rcItem;
    wchar_t label[64];
    rounded_box(item->hDC, rect, COLOR_WHITE, COLOR_BORDER, 12);
    rect.left += scale(14);
    rect.right -= scale(58);
    GetWindowTextW(item->hwndItem, label,
                   (int)(sizeof(label) / sizeof(label[0])));
    draw_label(item->hDC, label, rect, control_font, COLOR_INK,
               DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void draw_button_content(const DRAWITEMSTRUCT *item)
{
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    int pressed = (item->itemState & ODS_SELECTED) != 0;
    int hovered = hovered_button_id == (int)item->CtlID;
    COLORREF fill = COLOR_WHITE;
    COLORREF border = COLOR_BORDER;
    COLORREF ink = COLOR_INK;

    if (item->CtlID == ID_HOTKEY || item->CtlID == ID_HOLD_HOTKEY) {
        wchar_t shortcut[96];
        wchar_t title[64];
        wchar_t *separator;
        wchar_t *tag = NULL;
        SIZE title_extent = {0};
        SIZE tag_extent = {0};
        RECT name_rect = rect;
        RECT value_rect = rect;
        int is_hold = item->CtlID == ID_HOLD_HOTKEY;
        int reset_id = is_hold ? ID_RESET_HOLD_HOTKEY : ID_RESET_HOTKEY;
        int reset_hovered = hovered_reset_id == reset_id;
        int capturing = is_hold ? keymap_is_hold_capturing() : keymap_is_capturing();
        fill = pressed ? COLOR_TINT :
               (hovered && !reset_hovered ? COLOR_HOVER : COLOR_WHITE);
        rounded_box(dc, rect, fill,
                    (pressed || capturing) ? COLOR_ACCENT : COLOR_BORDER, 12);
        name_rect.left += scale(12);
        name_rect.right -= scale(34);
        name_rect.top += scale(5);
        name_rect.bottom = name_rect.top + scale(18);
        GetWindowTextW(item->hwndItem, title,
                       (int)(sizeof(title) / sizeof(title[0])));
        separator = wcschr(title, (wchar_t)0x00B7);
        if (separator != NULL) {
            *separator = L'\0';
            tag = separator + 1;
            while (*tag == L' ')
                ++tag;
        }
        draw_label(dc, title, name_rect, control_font, COLOR_INK,
                   DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        if (tag != NULL) {
            HGDIOBJ old_font = SelectObject(dc, control_font);
            GetTextExtentPoint32W(dc, title, (int)wcslen(title), &title_extent);
            GetTextExtentPoint32W(dc, L"· ", 2, &tag_extent);
            SelectObject(dc, old_font);
            name_rect.left += title_extent.cx;
            name_rect.right = name_rect.left + tag_extent.cx;
            draw_label(dc, L"· ", name_rect, control_font, COLOR_MUTED,
                       DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            name_rect.left += tag_extent.cx;
            name_rect.right = rect.right - scale(34);
            draw_label(dc, tag, name_rect, control_font, COLOR_MUTED,
                       DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        value_rect.left += scale(12);
        value_rect.right -= scale(12);
        value_rect.top += scale(24);
        value_rect.bottom -= scale(5);
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
        {
            int diameter = scale(16);
            RECT circle = {rect.right - scale(24), rect.top + scale(5),
                           rect.right - scale(8), rect.top + scale(21)};
            COLORREF circle_fill = pressed_reset_id == reset_id ? COLOR_ACCENT_DOWN :
                                   (hovered_reset_id == reset_id ?
                                    COLOR_RESET_HOVER : COLOR_ACCENT);
            rounded_box(dc, circle, circle_fill, circle_fill, diameter / 2);
            OffsetRect(&circle, 0, -scale(1));
            draw_label(dc, L"\x21BB", circle, icon_font != NULL ? icon_font : body_font,
                       COLOR_WHITE, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        return;
    }

    if (item->CtlID == ID_UPDATE_LINK) {
        int diameter = scale(19);
        RECT circle = rect;
        COLORREF badge_fill = hovered ? COLOR_ACCENT_HOVER : COLOR_ACCENT;
        HPEN pen;
        HGDIOBJ old_pen;
        POINT arrow[3];
        circle.left += (rect.right - rect.left - diameter) / 2;
        circle.top += (rect.bottom - rect.top - diameter) / 2;
        circle.right = circle.left + diameter;
        circle.bottom = circle.top + diameter;
        rounded_box(dc, circle, badge_fill, badge_fill, diameter / 2);
        pen = CreatePen(PS_SOLID, scale(1) > 0 ? scale(1) : 1, COLOR_WHITE);
        old_pen = pen != NULL ? SelectObject(dc, pen) : NULL;
        if (pen != NULL) {
            arrow[0].x = circle.left + diameter / 2;
            arrow[0].y = circle.top + scale(5);
            arrow[1].x = circle.left + diameter / 2;
            arrow[1].y = circle.bottom - scale(5);
            Polyline(dc, arrow, 2);
            arrow[0].x = circle.left + scale(5);
            arrow[0].y = circle.top + scale(9);
            arrow[1].x = circle.left + diameter / 2;
            arrow[1].y = circle.top + scale(5);
            arrow[2].x = circle.right - scale(5);
            arrow[2].y = circle.top + scale(9);
            Polyline(dc, arrow, 3);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
        return;
    }
    if (item->CtlID == ID_AUTO_UPDATES) {
        int enabled = settings_load_auto_updates();
        int diameter = scale(14);
        RECT circle = rect;
        COLORREF circle_fill = enabled ?
                        (hovered ? COLOR_ACCENT_HOVER : COLOR_ACCENT) : COLOR_WHITE;
        COLORREF circle_outline = enabled ? circle_fill :
                                  (hovered ? COLOR_ACCENT_HOVER : COLOR_BORDER);
        circle.left += (rect.right - rect.left - diameter) / 2;
        circle.top += (rect.bottom - rect.top - diameter) / 2;
        circle.right = circle.left + diameter;
        circle.bottom = circle.top + diameter;
        rounded_box(dc, circle, circle_fill, circle_outline, diameter / 2);
        if (enabled) {
            POINT check[3];
            HPEN check_pen = CreatePen(PS_SOLID, scale(2), COLOR_WHITE);
            HGDIOBJ old_check_pen = check_pen != NULL ? SelectObject(dc, check_pen) : NULL;
            check[0].x = circle.left + scale(3);
            check[0].y = circle.top + scale(7);
            check[1].x = circle.left + scale(6);
            check[1].y = circle.top + scale(10);
            check[2].x = circle.left + scale(11);
            check[2].y = circle.top + scale(4);
            if (check_pen != NULL) {
                Polyline(dc, check, 3);
                SelectObject(dc, old_check_pen);
                DeleteObject(check_pen);
            }
        }
        return;
    }
    if (item->CtlID == ID_AUTOSTART || item->CtlID == ID_BLOCK_LETTERS) {
        int enabled = item->CtlID == ID_AUTOSTART ?
                      settings_autostart_enabled() : keymap_block_unmapped_enabled();
        COLORREF track = enabled ? COLOR_ACCENT : COLOR_BORDER;
        RECT knob = rect;
        if (hovered) {
            if (item->CtlID == ID_AUTOSTART)
                track = enabled ? COLOR_AUTOSTART_HOVER_ON : COLOR_AUTOSTART_HOVER_OFF;
            else
                track = enabled ? COLOR_ACCENT_HOVER : COLOR_SWITCH_HOVER;
        }
        rounded_box(dc, rect, track, track, 12);
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
    if (item->CtlID == ID_MODE_BADGE) {
        int enabled = keymap_is_visual_enabled();
        const wchar_t *label = enabled ? L"ON" : L"OFF";
        if (enabled) {
            fill = pressed ? COLOR_ACCENT_DOWN :
                   (hovered ? COLOR_ACCENT_HOVER : COLOR_ACCENT);
            border = fill;
            ink = COLOR_WHITE;
        } else {
            fill = pressed ? COLOR_TINT : (hovered ? COLOR_HOVER : COLOR_WHITE);
            border = hovered || pressed ? COLOR_MODE_BADGE_HOVER_BORDER : COLOR_BORDER;
            ink = COLOR_ACCENT;
        }
        rounded_box(dc, rect, fill, border, 12);
        draw_label(dc, label, rect, small_font, ink,
                   DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
}

static void draw_button(const DRAWITEMSTRUCT *item)
{
    DRAWITEMSTRUCT buffered_item = *item;
    RECT target = item->rcItem;
    int width = target.right - target.left;
    int height = target.bottom - target.top;
    HDC buffer_dc;
    HBITMAP buffer_bitmap;
    HGDIOBJ previous_bitmap;
    HBRUSH background_brush;
    RECT buffer_rect;

    if (width <= 0 || height <= 0) {
        draw_button_content(item);
        return;
    }

    buffer_dc = CreateCompatibleDC(item->hDC);
    if (!buffer_dc) {
        draw_button_content(item);
        return;
    }
    buffer_bitmap = CreateCompatibleBitmap(item->hDC, width, height);
    if (!buffer_bitmap) {
        DeleteDC(buffer_dc);
        draw_button_content(item);
        return;
    }

    previous_bitmap = SelectObject(buffer_dc, buffer_bitmap);
    buffer_rect.left = 0;
    buffer_rect.top = 0;
    buffer_rect.right = width;
    buffer_rect.bottom = height;
    background_brush = CreateSolidBrush(item->CtlID == ID_MODE_BADGE ?
                                       COLOR_TINT : COLOR_WHITE);
    FillRect(buffer_dc, &buffer_rect,
             background_brush != NULL ? background_brush :
             (HBRUSH)GetStockObject(WHITE_BRUSH));
    if (background_brush != NULL)
        DeleteObject(background_brush);
    buffered_item.hDC = buffer_dc;
    buffered_item.rcItem = buffer_rect;
    draw_button_content(&buffered_item);
    BitBlt(item->hDC, target.left, target.top, width, height,
           buffer_dc, 0, 0, SRCCOPY);

    SelectObject(buffer_dc, previous_bitmap);
    DeleteObject(buffer_bitmap);
    DeleteDC(buffer_dc);
}

void app_ui_paint_settings(HDC dc, const RECT *client, app_ui_paint_state state)
{
    current = state;
    paint_settings(dc, client);
}

void app_ui_paint_setting_card(const DRAWITEMSTRUCT *item, app_ui_paint_state state)
{
    current = state;
    draw_setting_card(item);
}

void app_ui_paint_button(const DRAWITEMSTRUCT *item, app_ui_paint_state state)
{
    current = state;
    draw_button(item);
}

void app_ui_paint_hint(HDC dc, RECT rect, app_ui_paint_state state)
{
    RECT text_rect;
    int tail_height;
    current = state;
    tail_height = scale(6);
    if (active_hint_id == ID_BLOCK_LETTERS_TITLE) {
        rounded_bubble_draw(dc, rect, COLOR_TINT, COLOR_BORDER, scale(7),
                            tooltip_tail_center_x, scale(12), tail_height);
        text_rect = rect;
        text_rect.bottom -= tail_height;
        InflateRect(&text_rect, -scale(9), -scale(2));
        {
            wchar_t hint[] = L"小键盘模式下拦截未映射字符键；\n顶部数字行仍输出对应符号。";
            wchar_t *second_line = wcschr(hint, L'\n');
            HGDIOBJ old_font;
            TEXTMETRIC metrics;
            RECT line_rect = text_rect;
            int line_height;
            int line_gap = scale(4);
            int group_height;
            int content_height;
            if (second_line != NULL)
                *second_line++ = L'\0';
            old_font = SelectObject(dc, compact_font != NULL ? compact_font :
                                    GetStockObject(DEFAULT_GUI_FONT));
            GetTextMetricsW(dc, &metrics);
            SelectObject(dc, old_font);
            line_height = metrics.tmHeight;
            content_height = text_rect.bottom - text_rect.top;
            group_height = line_height * 2 + line_gap;
            line_rect.top = text_rect.top + (content_height - group_height) / 2;
            line_rect.bottom = line_rect.top + line_height;
            draw_label(dc, hint, line_rect, compact_font, COLOR_INK,
                       DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            if (second_line != NULL) {
                line_rect.top += line_height + line_gap;
                line_rect.bottom = line_rect.top + line_height;
                draw_label(dc, second_line, line_rect, compact_font, COLOR_INK,
                           DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
        }
    } else {
        rounded_bubble_draw(dc, rect, COLOR_TINT, COLOR_BORDER, scale(7),
                            tooltip_tail_center_x, scale(12), tail_height);
        text_rect = rect;
        text_rect.bottom -= tail_height;
        InflateRect(&text_rect, -scale(9), -scale(2));
        draw_label(dc, L"重置快捷键", text_rect, compact_font, COLOR_INK,
                   DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
}
