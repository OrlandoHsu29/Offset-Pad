#ifndef OFFSET_PAD_APP_UI_LAYOUT_H
#define OFFSET_PAD_APP_UI_LAYOUT_H

#include <stddef.h>
#include <windows.h>

/* Logical (96 DPI) keycap bounds shared by painting and pointer hit testing. */
static inline RECT app_ui_keycap_rect(size_t index, int output)
{
    int row = (int)index / 3;
    int column = (int)index % 3;
    int left;
    int top = 221 + row * 34;
    int width = 29;
    RECT rect;

    if (index >= 9) {
        top = 323;
        width = index == 9 ? 63 : 29;
        left = output ? (index == 9 ? 260 : 328) :
                        (index == 9 ? 76 : 144);
    } else {
        left = (output ? 260 : 65 + (row == 1 ? 11 : 0)) + column * 34;
    }
    rect.left = left;
    rect.top = top;
    rect.right = left + width;
    rect.bottom = top + 29;
    return rect;
}

#endif
