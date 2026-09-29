#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "rounded_box.h"

/* GDI+ flat exports are declared here because the SDK's umbrella header is C++. */
typedef struct GpGraphics GpGraphics;
typedef struct GpPath GpPath;
typedef struct GpBrush GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpPen GpPen;
typedef struct gdiplus_startup_input {
    UINT32 version;
    void *debug_callback;
    BOOL suppress_background_thread;
    BOOL suppress_external_codecs;
} gdiplus_startup_input;

__declspec(dllimport) int WINAPI GdiplusStartup(ULONG_PTR *token,
                                                 const gdiplus_startup_input *input,
                                                 void *output);
__declspec(dllimport) void WINAPI GdiplusShutdown(ULONG_PTR token);
__declspec(dllimport) int WINAPI GdipCreateFromHDC(HDC dc, GpGraphics **graphics);
__declspec(dllimport) int WINAPI GdipDeleteGraphics(GpGraphics *graphics);
__declspec(dllimport) int WINAPI GdipSetSmoothingMode(GpGraphics *graphics, int mode);
__declspec(dllimport) int WINAPI GdipCreatePath(int fill_mode, GpPath **path);
__declspec(dllimport) int WINAPI GdipDeletePath(GpPath *path);
__declspec(dllimport) int WINAPI GdipAddPathArc(GpPath *path, float x, float y,
                                                float width, float height,
                                                float start_angle, float sweep_angle);
__declspec(dllimport) int WINAPI GdipAddPathLine(GpPath *path, float x1, float y1,
                                                 float x2, float y2);
__declspec(dllimport) int WINAPI GdipClosePathFigure(GpPath *path);
__declspec(dllimport) int WINAPI GdipCreateSolidFill(DWORD color, GpSolidFill **brush);
__declspec(dllimport) int WINAPI GdipDeleteBrush(GpBrush *brush);
__declspec(dllimport) int WINAPI GdipCreatePen1(DWORD color, float width,
                                                 int unit, GpPen **pen);
__declspec(dllimport) int WINAPI GdipDeletePen(GpPen *pen);
__declspec(dllimport) int WINAPI GdipFillPath(GpGraphics *graphics,
                                              GpBrush *brush, GpPath *path);
__declspec(dllimport) int WINAPI GdipDrawPath(GpGraphics *graphics,
                                              GpPen *pen, GpPath *path);

static ULONG_PTR gdiplus_token;

static DWORD argb(COLORREF color)
{
    return 0xFF000000U | ((DWORD)GetRValue(color) << 16) |
           ((DWORD)GetGValue(color) << 8) | GetBValue(color);
}

int rounded_box_init(void)
{
    gdiplus_startup_input input = {1, NULL, FALSE, FALSE};
    if (gdiplus_token != 0)
        return 1;
    return GdiplusStartup(&gdiplus_token, &input, NULL) == 0;
}

void rounded_box_shutdown(void)
{
    if (gdiplus_token != 0) {
        GdiplusShutdown(gdiplus_token);
        gdiplus_token = 0;
    }
}

static int add_round_path(GpPath *path, float left, float top,
                          float width, float height, float radius)
{
    float diameter = radius * 2.0f;
    if (GdipAddPathArc(path, left, top, diameter, diameter, 180.0f, 90.0f) != 0 ||
        GdipAddPathArc(path, left + width - diameter, top,
                       diameter, diameter, 270.0f, 90.0f) != 0 ||
        GdipAddPathArc(path, left + width - diameter, top + height - diameter,
                       diameter, diameter, 0.0f, 90.0f) != 0 ||
        GdipAddPathArc(path, left, top + height - diameter,
                       diameter, diameter, 90.0f, 90.0f) != 0)
        return 0;
    return GdipClosePathFigure(path) == 0;
}

void rounded_box_draw(HDC dc, RECT rect, COLORREF fill, COLORREF outline,
                      int radius_pixels)
{
    GpGraphics *graphics = NULL;
    GpPath *path = NULL;
    GpSolidFill *brush = NULL;
    GpPen *pen = NULL;
    float left = (float)rect.left + 0.5f;
    float top = (float)rect.top + 0.5f;
    float width = (float)(rect.right - rect.left) - 1.0f;
    float height = (float)(rect.bottom - rect.top) - 1.0f;
    float radius = (float)radius_pixels;
    int success = 0;

    if (radius > width / 2.0f) radius = width / 2.0f;
    if (radius > height / 2.0f) radius = height / 2.0f;
    if (gdiplus_token != 0 && radius > 0.0f &&
        GdipCreateFromHDC(dc, &graphics) == 0 &&
        GdipSetSmoothingMode(graphics, 4) == 0 &&
        GdipCreatePath(0, &path) == 0 &&
        add_round_path(path, left, top, width, height, radius) &&
        GdipCreateSolidFill(argb(fill), &brush) == 0 &&
        GdipCreatePen1(argb(outline), 1.0f, 2, &pen) == 0 &&
        GdipFillPath(graphics, (GpBrush *)brush, path) == 0 &&
        GdipDrawPath(graphics, pen, path) == 0)
        success = 1;

    if (pen != NULL) GdipDeletePen(pen);
    if (brush != NULL) GdipDeleteBrush((GpBrush *)brush);
    if (path != NULL) GdipDeletePath(path);
    if (graphics != NULL) GdipDeleteGraphics(graphics);
    if (!success) {
        HBRUSH old_brush;
        HPEN old_pen;
        HBRUSH fallback_brush = CreateSolidBrush(fill);
        HPEN fallback_pen = CreatePen(PS_SOLID, 1, outline);
        old_brush = (HBRUSH)SelectObject(dc, fallback_brush);
        old_pen = (HPEN)SelectObject(dc, fallback_pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
                  radius_pixels * 2, radius_pixels * 2);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(fallback_pen);
        DeleteObject(fallback_brush);
    }
}

void rounded_bubble_draw(HDC dc, RECT rect, COLORREF fill, COLORREF outline,
                         int radius_pixels, int tail_center_x,
                         int tail_width, int tail_height)
{
    GpGraphics *graphics = NULL;
    GpPath *path = NULL;
    GpSolidFill *brush = NULL;
    GpPen *pen = NULL;
    float left = (float)rect.left + 0.5f;
    float top = (float)rect.top + 0.5f;
    float right = (float)rect.right - 0.5f;
    float body_bottom = (float)rect.bottom - (float)tail_height - 0.5f;
    float bottom = (float)rect.bottom - 0.5f;
    float radius = (float)radius_pixels;
    float center = (float)rect.left + (float)tail_center_x;
    float half_tail = (float)tail_width / 2.0f;
    int success = 0;

    if (radius > (right - left) / 2.0f) radius = (right - left) / 2.0f;
    if (radius > (body_bottom - top) / 2.0f) radius = (body_bottom - top) / 2.0f;
    if (center - half_tail < left + radius) center = left + radius + half_tail;
    if (center + half_tail > right - radius) center = right - radius - half_tail;
    if (gdiplus_token != 0 && radius > 0.0f && tail_height > 0 &&
        GdipCreateFromHDC(dc, &graphics) == 0 &&
        GdipSetSmoothingMode(graphics, 4) == 0 &&
        GdipCreatePath(0, &path) == 0 &&
        GdipAddPathLine(path, left + radius, top, right - radius, top) == 0 &&
        GdipAddPathArc(path, right - radius * 2.0f, top, radius * 2.0f,
                       radius * 2.0f, 270.0f, 90.0f) == 0 &&
        GdipAddPathLine(path, right, top + radius, right, body_bottom - radius) == 0 &&
        GdipAddPathArc(path, right - radius * 2.0f, body_bottom - radius * 2.0f,
                       radius * 2.0f, radius * 2.0f, 0.0f, 90.0f) == 0 &&
        GdipAddPathLine(path, right - radius, body_bottom, center + half_tail,
                        body_bottom) == 0 &&
        GdipAddPathLine(path, center + half_tail, body_bottom, center, bottom) == 0 &&
        GdipAddPathLine(path, center, bottom, center - half_tail, body_bottom) == 0 &&
        GdipAddPathLine(path, center - half_tail, body_bottom, left + radius,
                        body_bottom) == 0 &&
        GdipAddPathArc(path, left, body_bottom - radius * 2.0f,
                       radius * 2.0f, radius * 2.0f, 90.0f, 90.0f) == 0 &&
        GdipAddPathLine(path, left, body_bottom - radius, left, top + radius) == 0 &&
        GdipAddPathArc(path, left, top, radius * 2.0f, radius * 2.0f,
                       180.0f, 90.0f) == 0 &&
        GdipClosePathFigure(path) == 0 &&
        GdipCreateSolidFill(argb(fill), &brush) == 0 &&
        GdipCreatePen1(argb(outline), 1.0f, 2, &pen) == 0 &&
        GdipFillPath(graphics, (GpBrush *)brush, path) == 0 &&
        GdipDrawPath(graphics, pen, path) == 0)
        success = 1;

    if (pen != NULL) GdipDeletePen(pen);
    if (brush != NULL) GdipDeleteBrush((GpBrush *)brush);
    if (path != NULL) GdipDeletePath(path);
    if (graphics != NULL) GdipDeleteGraphics(graphics);
    if (!success) {
        RECT body = rect;
        POINT triangle[3];
        HBRUSH brush_fallback = CreateSolidBrush(fill);
        HPEN pen_fallback = CreatePen(PS_SOLID, 1, outline);
        HGDIOBJ old_brush;
        HGDIOBJ old_pen;
        body.bottom -= tail_height;
        rounded_box_draw(dc, body, fill, outline, radius_pixels);
        triangle[0].x = rect.left + tail_center_x - tail_width / 2;
        triangle[0].y = body.bottom - 1;
        triangle[1].x = rect.left + tail_center_x;
        triangle[1].y = rect.bottom - 1;
        triangle[2].x = rect.left + tail_center_x + tail_width / 2;
        triangle[2].y = body.bottom - 1;
        old_brush = SelectObject(dc, brush_fallback);
        old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, triangle, 3);
        SelectObject(dc, old_pen);
        old_pen = SelectObject(dc, pen_fallback);
        MoveToEx(dc, triangle[0].x, triangle[0].y, NULL);
        LineTo(dc, triangle[1].x, triangle[1].y);
        LineTo(dc, triangle[2].x, triangle[2].y);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(pen_fallback);
        DeleteObject(brush_fallback);
    }
}