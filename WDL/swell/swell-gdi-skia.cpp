/*
  Optional Skia-backed raster surfaces for SWELL generic GDI.

  This file deliberately keeps the first Skia integration small: it provides a
  Skia-owned raster surface that also behaves as a LICE_IBitmap. Existing SWELL
  code can keep using the mature generic/LICE paint traversal while newer code
  can discover the SkCanvas via Extended(SWELL_SKIA_EXT_CANVAS, NULL).
*/

#ifdef SWELL_SKIA_GDI
#ifndef SWELL_PROVIDED_BY_APP

#include "include/core/SkCanvas.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"

#include "swell.h"
#include "swell-internal.h"
#include "swell-gdi-skia.h"

class SWELL_SkiaRasterBitmap : public LICE_IBitmap
{
public:
  SWELL_SkiaRasterBitmap()
    : m_fb(NULL), m_allocsize(0), m_width(0), m_height(0), m_span(0)
  {
  }

  explicit SWELL_SkiaRasterBitmap(int w, int h)
    : m_fb(NULL), m_allocsize(0), m_width(0), m_height(0), m_span(0)
  {
    resize(w, h);
  }

  ~SWELL_SkiaRasterBitmap() override
  {
    m_surface.reset();
    free(m_fb);
  }

  LICE_pixel *getBits() override
  {
    const UINT_PTR extra = LICE_MEMBITMAP_ALIGNAMT;
    return (LICE_pixel *)(((UINT_PTR)m_fb + extra) & ~extra);
  }

  int getWidth() override { return m_width; }
  int getHeight() override { return m_height; }
  int getRowSpan() override { return m_span; }

  bool resize(int w, int h) override
  {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w == m_width && h == m_height && m_surface) return false;

    m_surface.reset();

    const int rowbytes = w > 0 ? ((w * 4 + 15) & ~15) : 0;
    const int span = rowbytes / 4;
    const int sz = h > 0 ? h * rowbytes + LICE_MEMBITMAP_ALIGNAMT : 0;

    if (sz > 0 && (!m_fb || m_allocsize < sz || sz < m_allocsize / 4))
    {
      const int newalloc = m_allocsize < sz ? (sz * 3) / 2 : sz;
      void *p = realloc(m_fb, newalloc);
      if (!p) return false;
      m_fb = (LICE_pixel *)p;
      m_allocsize = newalloc;
    }

    m_width = w && h ? w : 0;
    m_height = w && h ? h : 0;
    m_span = m_width ? span : 0;

    if (m_width > 0 && m_height > 0)
    {
      SkImageInfo info = SkImageInfo::Make(
        m_width, m_height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
      SkPixmap pixmap(info, getBits(), (size_t)m_span * 4);
      m_surface = SkSurfaces::WrapPixels(pixmap);
      if (!m_surface)
      {
        m_width = m_height = m_span = 0;
        return false;
      }
    }

    return true;
  }

  INT_PTR Extended(int id, void *data) override
  {
    if (data) return 0;
    if (!m_surface) return 0;
    if (id == SWELL_SKIA_EXT_CANVAS) return (INT_PTR)m_surface->getCanvas();
    if (id == SWELL_SKIA_EXT_SURFACE) return (INT_PTR)m_surface.get();
    return 0;
  }

private:
  LICE_pixel *m_fb;
  int m_allocsize;
  int m_width;
  int m_height;
  int m_span;
  sk_sp<SkSurface> m_surface;
};

LICE_IBitmap *SWELL_CreateSkiaRasterBitmap(int w, int h)
{
  SWELL_SkiaRasterBitmap *bm = new SWELL_SkiaRasterBitmap;
  if (!bm) return NULL;
  if (!bm->resize(w, h) && (w > 0 && h > 0))
  {
    delete bm;
    return NULL;
  }
  return bm;
}

void *SWELL_GetSkiaCanvasFromBitmap(LICE_IBitmap *bitmap)
{
  return bitmap ? (void *)bitmap->Extended(SWELL_SKIA_EXT_CANVAS, NULL) : NULL;
}

static SkColor swell_skia_color_from_lice(unsigned int c, float alpha)
{
  if (alpha < 0.0f) alpha = 0.0f;
  else if (alpha > 1.0f) alpha = 1.0f;
  const int a = (int)(255.0f * alpha + 0.5f);
  return SkColorSetARGB(a, LICE_GETR(c), LICE_GETG(c), LICE_GETB(c));
}

bool SWELL_SkiaFillRect(LICE_IBitmap *bitmap, int x, int y, int w, int h, unsigned int lice_color, float alpha)
{
  SkCanvas *canvas = (SkCanvas *)SWELL_GetSkiaCanvasFromBitmap(bitmap);
  if (!canvas || w <= 0 || h <= 0) return false;

  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(swell_skia_color_from_lice(lice_color, alpha));
  paint.setBlendMode(SkBlendMode::kSrc);
  canvas->drawRect(SkRect::MakeXYWH((SkScalar)x, (SkScalar)y, (SkScalar)w, (SkScalar)h), paint);
  return true;
}

static void swell_skia_setup_stroke(SkPaint *paint, unsigned int lice_color, float alpha, int stroke_width)
{
  paint->setAntiAlias(false);
  paint->setStyle(SkPaint::kStroke_Style);
  paint->setColor(swell_skia_color_from_lice(lice_color, alpha));
  paint->setBlendMode(SkBlendMode::kSrc);
  paint->setStrokeWidth((SkScalar)(stroke_width > 0 ? stroke_width : 1));
}

bool SWELL_SkiaStrokeRect(LICE_IBitmap *bitmap, int x, int y, int w, int h, unsigned int lice_color, float alpha, int stroke_width)
{
  SkCanvas *canvas = (SkCanvas *)SWELL_GetSkiaCanvasFromBitmap(bitmap);
  if (!canvas || w <= 0 || h <= 0) return false;

  SkPaint paint;
  swell_skia_setup_stroke(&paint, lice_color, alpha, stroke_width);
  const SkScalar inset = paint.getStrokeWidth() * 0.5f;
  canvas->drawRect(SkRect::MakeLTRB((SkScalar)x + inset,
                                    (SkScalar)y + inset,
                                    (SkScalar)(x + w) - inset,
                                    (SkScalar)(y + h) - inset), paint);
  return true;
}

bool SWELL_SkiaDrawLine(LICE_IBitmap *bitmap, float x1, float y1, float x2, float y2, unsigned int lice_color, float alpha, int stroke_width)
{
  SkCanvas *canvas = (SkCanvas *)SWELL_GetSkiaCanvasFromBitmap(bitmap);
  if (!canvas) return false;

  SkPaint paint;
  swell_skia_setup_stroke(&paint, lice_color, alpha, stroke_width);
  canvas->drawLine((SkScalar)x1, (SkScalar)y1, (SkScalar)x2, (SkScalar)y2, paint);
  return true;
}

bool SWELL_SkiaDrawEllipse(LICE_IBitmap *bitmap, int l, int t, int r, int b,
                           bool do_fill, unsigned int fill_color, float fill_alpha,
                           bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                           int stroke_width)
{
  SkCanvas *canvas = (SkCanvas *)SWELL_GetSkiaCanvasFromBitmap(bitmap);
  if (!canvas || r <= l || b <= t || (!do_fill && !do_stroke)) return false;

  SkRect oval = SkRect::MakeLTRB((SkScalar)l, (SkScalar)t, (SkScalar)r, (SkScalar)b);
  if (do_fill)
  {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(swell_skia_color_from_lice(fill_color, fill_alpha));
    paint.setBlendMode(SkBlendMode::kSrc);
    canvas->drawOval(oval, paint);
  }
  if (do_stroke)
  {
    SkPaint paint;
    swell_skia_setup_stroke(&paint, stroke_color, stroke_alpha, stroke_width);
    paint.setAntiAlias(true);
    const SkScalar inset = paint.getStrokeWidth() * 0.5f;
    oval.inset(inset, inset);
    canvas->drawOval(oval, paint);
  }
  return true;
}

bool SWELL_SkiaDrawBitmap(LICE_IBitmap *dst, LICE_IBitmap *src,
                          int x, int y, int w, int h,
                          int sx, int sy, int sw, int sh,
                          bool use_alpha, float opacity, bool filter)
{
  SkCanvas *canvas = (SkCanvas *)SWELL_GetSkiaCanvasFromBitmap(dst);
  if (!canvas || !src || !src->getBits() || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return false;

  const SkAlphaType alpha_type = use_alpha ? kUnpremul_SkAlphaType : kOpaque_SkAlphaType;
  SkImageInfo info = SkImageInfo::Make(src->getWidth(), src->getHeight(), kBGRA_8888_SkColorType, alpha_type);
  SkPixmap pixmap(info, src->getBits(), (size_t)src->getRowSpan() * 4);
  sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);
  if (!image) return false;

  if (opacity < 0.0f) opacity = 0.0f;
  else if (opacity > 1.0f) opacity = 1.0f;

  SkPaint paint;
  paint.setAlphaf(opacity);
  paint.setBlendMode(use_alpha || opacity < 1.0f ? SkBlendMode::kSrcOver : SkBlendMode::kSrc);
  const SkSamplingOptions sampling(filter ? SkFilterMode::kLinear : SkFilterMode::kNearest);
  canvas->drawImageRect(image,
                        SkRect::MakeXYWH((SkScalar)sx, (SkScalar)sy, (SkScalar)sw, (SkScalar)sh),
                        SkRect::MakeXYWH((SkScalar)x, (SkScalar)y, (SkScalar)w, (SkScalar)h),
                        sampling, &paint, SkCanvas::kStrict_SrcRectConstraint);
  return true;
}

#endif
#endif
