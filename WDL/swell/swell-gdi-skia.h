/*
  Optional Skia-backed raster surfaces for the generic SWELL GDI path.

  These surfaces implement LICE_IBitmap so the existing generic SWELL paint
  traversal and SDL texture upload path can use them without a second windowing
  implementation. Skia access is exposed through Extended() for code that wants
  to draw with SkCanvas directly.
*/

#ifndef WDL_SWELL_GDI_SKIA_H
#define WDL_SWELL_GDI_SKIA_H

class LICE_IBitmap;

enum
{
  SWELL_SKIA_EXT_CANVAS = 0x534b4301,
  SWELL_SKIA_EXT_SURFACE = 0x534b5301
};

#ifdef SWELL_SKIA_GDI
LICE_IBitmap *SWELL_CreateSkiaRasterBitmap(int w, int h);
void *SWELL_GetSkiaCanvasFromBitmap(LICE_IBitmap *bitmap);
bool SWELL_SkiaFillRect(LICE_IBitmap *bitmap, int x, int y, int w, int h, unsigned int lice_color, float alpha);
bool SWELL_SkiaStrokeRect(LICE_IBitmap *bitmap, int x, int y, int w, int h, unsigned int lice_color, float alpha, int stroke_width);
bool SWELL_SkiaDrawLine(LICE_IBitmap *bitmap, float x1, float y1, float x2, float y2, unsigned int lice_color, float alpha, int stroke_width);
bool SWELL_SkiaDrawEllipse(LICE_IBitmap *bitmap, int l, int t, int r, int b,
                           bool do_fill, unsigned int fill_color, float fill_alpha,
                           bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                           int stroke_width);
bool SWELL_SkiaDrawBitmap(LICE_IBitmap *dst, LICE_IBitmap *src,
                          int x, int y, int w, int h,
                          int sx, int sy, int sw, int sh,
                          bool use_alpha, float opacity, bool filter);
bool SWELL_SkiaDrawPolygon(LICE_IBitmap *bitmap, const POINT *pts, int npts, int addx, int addy,
                           bool do_fill, unsigned int fill_color, float fill_alpha,
                           bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                           int stroke_width);
bool SWELL_SkiaDrawRoundRect(LICE_IBitmap *bitmap, int l, int t, int r, int b, int rx, int ry,
                             bool do_fill, unsigned int fill_color, float fill_alpha,
                             bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                             int stroke_width);
bool SWELL_SkiaDrawPolyBezierTo(LICE_IBitmap *bitmap, float startx, float starty,
                                const POINT *pts, int npts, int addx, int addy,
                                unsigned int stroke_color, float stroke_alpha, int stroke_width);
bool SWELL_SkiaDrawPolyPolyline(LICE_IBitmap *bitmap, const POINT *pts, const DWORD *cnts, int nseg,
                                int addx, int addy, unsigned int stroke_color, float stroke_alpha,
                                int stroke_width);
bool SWELL_SkiaPushClipRegion(LICE_IBitmap *bitmap);
bool SWELL_SkiaSetClipRegion(LICE_IBitmap *bitmap, const RECT *r, int addx, int addy);
bool SWELL_SkiaPopClipRegion(LICE_IBitmap *bitmap);
#endif

#endif
