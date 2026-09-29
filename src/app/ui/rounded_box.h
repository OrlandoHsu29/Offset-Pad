#ifndef OFFSET_PAD_ROUNDED_BOX_H
#define OFFSET_PAD_ROUNDED_BOX_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int rounded_box_init(void);
void rounded_box_shutdown(void);
void rounded_box_draw(HDC dc, RECT rect, COLORREF fill, COLORREF outline,
                      int radius_pixels);
void rounded_bubble_draw(HDC dc, RECT rect, COLORREF fill, COLORREF outline,
                         int radius_pixels, int tail_center_x,
                         int tail_width, int tail_height);

#endif
