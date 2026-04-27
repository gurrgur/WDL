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
#endif

#endif
