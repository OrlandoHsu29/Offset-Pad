#ifndef OFFSET_PAD_APP_UI_PAINT_H
#define OFFSET_PAD_APP_UI_PAINT_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct app_ui_paint_state {
    int dpi;
    HICON logo;
    HFONT title_font;
    HFONT heading_font;
    HFONT body_font;
    HFONT control_font;
    HFONT small_font;
    HFONT compact_font;
    HFONT icon_font;
    const wchar_t *capture_status;
    int hovered_source_key;
    unsigned int pressed_source_keys;
    int hovered_button_id;
    int hovered_reset_id;
    int pressed_reset_id;
    int active_hint_id;
    int tooltip_tail_center_x;
} app_ui_paint_state;

#define ID_AUTOSTART 102
#define ID_HOTKEY 104
#define ID_BLOCK_LETTERS 105
#define ID_HOLD_HOTKEY 106
#define ID_AUTOSTART_CARD 107
#define ID_BLOCK_LETTERS_CARD 108
#define ID_BLOCK_LETTERS_TITLE 114
#define ID_AUTO_UPDATES 109
#define ID_UPDATE_LINK 110
#define ID_MODE_BADGE 111
#define ID_RESET_HOTKEY 112
#define ID_RESET_HOLD_HOTKEY 113

void app_ui_paint_settings(HDC dc, const RECT *client, app_ui_paint_state state);
void app_ui_paint_hint(HDC dc, RECT rect, app_ui_paint_state state);
void app_ui_paint_setting_card(const DRAWITEMSTRUCT *item, app_ui_paint_state state);
void app_ui_paint_button(const DRAWITEMSTRUCT *item, app_ui_paint_state state);

#endif