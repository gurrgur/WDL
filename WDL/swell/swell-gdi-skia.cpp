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
#include "include/core/SkPath.h"
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

static SkCanvas *swell_skia_canvas_from_bitmap(LICE_IBitmap *bitmap, int *xoff, int *yoff, int *clipw, int *cliph)
{
  if (xoff) *xoff = 0;
  if (yoff) *yoff = 0;
  if (clipw) *clipw = bitmap ? bitmap->getWidth() : 0;
  if (cliph) *cliph = bitmap ? bitmap->getHeight() : 0;

  while (bitmap && bitmap->Extended(LICE_SubBitmap::LICE_GET_SUBBITMAP_VERSION, NULL) == LICE_SubBitmap::LICE_SUBBITMAP_VERSION)
  {
    LICE_SubBitmap *sb = (LICE_SubBitmap *)bitmap;
    if (xoff) *xoff += sb->m_x;
    if (yoff) *yoff += sb->m_y;
    bitmap = sb->m_parent;
  }

  return bitmap ? (SkCanvas *)bitmap->Extended(SWELL_SKIA_EXT_CANVAS, NULL) : NULL;
}

static void swell_skia_clip_to_bitmap(SkCanvas *canvas, int xoff, int yoff, int clipw, int cliph)
{
  canvas->clipRect(SkRect::MakeXYWH((SkScalar)xoff, (SkScalar)yoff, (SkScalar)clipw, (SkScalar)cliph));
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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || w <= 0 || h <= 0) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(swell_skia_color_from_lice(lice_color, alpha));
  paint.setBlendMode(SkBlendMode::kSrc);
  canvas->drawRect(SkRect::MakeXYWH((SkScalar)(x + xoff), (SkScalar)(y + yoff), (SkScalar)w, (SkScalar)h), paint);
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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || w <= 0 || h <= 0) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPaint paint;
  swell_skia_setup_stroke(&paint, lice_color, alpha, stroke_width);
  const SkScalar inset = paint.getStrokeWidth() * 0.5f;
  canvas->drawRect(SkRect::MakeLTRB((SkScalar)(x + xoff) + inset,
                                    (SkScalar)(y + yoff) + inset,
                                    (SkScalar)(x + xoff + w) - inset,
                                    (SkScalar)(y + yoff + h) - inset), paint);
  return true;
}

bool SWELL_SkiaDrawLine(LICE_IBitmap *bitmap, float x1, float y1, float x2, float y2, unsigned int lice_color, float alpha, int stroke_width)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPaint paint;
  swell_skia_setup_stroke(&paint, lice_color, alpha, stroke_width);
  canvas->drawLine((SkScalar)(x1 + xoff), (SkScalar)(y1 + yoff),
                   (SkScalar)(x2 + xoff), (SkScalar)(y2 + yoff), paint);
  return true;
}

bool SWELL_SkiaDrawEllipse(LICE_IBitmap *bitmap, int l, int t, int r, int b,
                           bool do_fill, unsigned int fill_color, float fill_alpha,
                           bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                           int stroke_width)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || r <= l || b <= t || (!do_fill && !do_stroke)) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkRect oval = SkRect::MakeLTRB((SkScalar)(l + xoff), (SkScalar)(t + yoff),
                                 (SkScalar)(r + xoff), (SkScalar)(b + yoff));
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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(dst, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || !src || !src->getBits() || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

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
                        SkRect::MakeXYWH((SkScalar)(x + xoff), (SkScalar)(y + yoff), (SkScalar)w, (SkScalar)h),
                        sampling, &paint, SkCanvas::kStrict_SrcRectConstraint);
  return true;
}

bool SWELL_SkiaDrawPolygon(LICE_IBitmap *bitmap, const POINT *pts, int npts, int addx, int addy,
                           bool do_fill, unsigned int fill_color, float fill_alpha,
                           bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                           int stroke_width)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || !pts || npts < 2 || (!do_fill && !do_stroke)) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPath path;
  path.moveTo((SkScalar)(pts[0].x + addx + xoff), (SkScalar)(pts[0].y + addy + yoff));
  for (int x = 1; x < npts; x ++)
    path.lineTo((SkScalar)(pts[x].x + addx + xoff), (SkScalar)(pts[x].y + addy + yoff));
  path.close();

  if (do_fill)
  {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(swell_skia_color_from_lice(fill_color, fill_alpha));
    paint.setBlendMode(SkBlendMode::kSrc);
    canvas->drawPath(path, paint);
  }
  if (do_stroke)
  {
    SkPaint paint;
    swell_skia_setup_stroke(&paint, stroke_color, stroke_alpha, stroke_width);
    paint.setAntiAlias(true);
    canvas->drawPath(path, paint);
  }
  return true;
}

bool SWELL_SkiaDrawRoundRect(LICE_IBitmap *bitmap, int l, int t, int r, int b, int rx, int ry,
                             bool do_fill, unsigned int fill_color, float fill_alpha,
                             bool do_stroke, unsigned int stroke_color, float stroke_alpha,
                             int stroke_width)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || r <= l || b <= t || (!do_fill && !do_stroke)) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  const SkScalar sx = (SkScalar)(rx > 0 ? rx : 0);
  const SkScalar sy = (SkScalar)(ry > 0 ? ry : 0);
  SkRect rect = SkRect::MakeLTRB((SkScalar)(l + xoff), (SkScalar)(t + yoff),
                                 (SkScalar)(r + xoff), (SkScalar)(b + yoff));
  if (do_fill)
  {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(swell_skia_color_from_lice(fill_color, fill_alpha));
    paint.setBlendMode(SkBlendMode::kSrc);
    canvas->drawRoundRect(rect, sx, sy, paint);
  }
  if (do_stroke)
  {
    SkPaint paint;
    swell_skia_setup_stroke(&paint, stroke_color, stroke_alpha, stroke_width);
    paint.setAntiAlias(true);
    const SkScalar inset = paint.getStrokeWidth() * 0.5f;
    rect.inset(inset, inset);
    canvas->drawRoundRect(rect, sx, sy, paint);
  }
  return true;
}

bool SWELL_SkiaDrawPolyBezierTo(LICE_IBitmap *bitmap, float startx, float starty,
                                const POINT *pts, int npts, int addx, int addy,
                                unsigned int stroke_color, float stroke_alpha, int stroke_width)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || !pts || npts < 3) return false;

  SkPath path;
  path.moveTo((SkScalar)(startx + addx + xoff), (SkScalar)(starty + addy + yoff));
  for (int x = 0; x < npts - 2; x += 3)
    path.cubicTo((SkScalar)(pts[x].x + addx + xoff), (SkScalar)(pts[x].y + addy + yoff),
                 (SkScalar)(pts[x+1].x + addx + xoff), (SkScalar)(pts[x+1].y + addy + yoff),
                 (SkScalar)(pts[x+2].x + addx + xoff), (SkScalar)(pts[x+2].y + addy + yoff));

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPaint paint;
  swell_skia_setup_stroke(&paint, stroke_color, stroke_alpha, stroke_width);
  paint.setAntiAlias(true);
  canvas->drawPath(path, paint);
  return true;
}

bool SWELL_SkiaDrawPolyPolyline(LICE_IBitmap *bitmap, const POINT *pts, const DWORD *cnts, int nseg,
                                int addx, int addy, unsigned int stroke_color, float stroke_alpha,
                                int stroke_width)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || !pts || !cnts || nseg < 1) return false;

  SkPath path;
  bool has_path = false;
  for (int seg = 0; seg < nseg; seg ++)
  {
    DWORD cnt = cnts[seg];
    if (!cnt) continue;
    if (cnt < 2)
    {
      pts++;
      continue;
    }

    path.moveTo((SkScalar)(pts->x + addx + xoff), (SkScalar)(pts->y + addy + yoff));
    pts++;
    for (DWORD x = 1; x < cnt; x ++, pts++)
      path.lineTo((SkScalar)(pts->x + addx + xoff), (SkScalar)(pts->y + addy + yoff));
    has_path = true;
  }
  if (!has_path) return true;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPaint paint;
  swell_skia_setup_stroke(&paint, stroke_color, stroke_alpha, stroke_width);
  paint.setAntiAlias(true);
  canvas->drawPath(path, paint);
  return true;
}

#endif
#endif
