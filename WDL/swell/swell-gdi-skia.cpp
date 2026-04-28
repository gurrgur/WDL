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
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSpan.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#if defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif

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

bool SWELL_SkiaDrawGlyphMask(LICE_IBitmap *bitmap, int x, int y, unsigned int lice_color,
                             const unsigned char *src, int w, int pitch, int h, bool mono)
{
  if (!bitmap || !src || w <= 0 || h <= 0 || pitch == 0) return false;

  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setBlendMode(SkBlendMode::kSrcOver);
  paint.setColor(swell_skia_color_from_lice(lice_color, 1.0f));

  const SkScalar dx = (SkScalar)(x + xoff);
  const SkScalar dy = (SkScalar)(y + yoff);
  const SkImageInfo a8info = SkImageInfo::Make(w, h, kAlpha_8_SkColorType, kPremul_SkAlphaType);

  if (!mono && pitch > 0 && pitch == w)
  {
    // Wrap the FreeType gray buffer directly — no allocation, no copy
    const SkPixmap pixmap(a8info, src, (size_t)w);
    sk_sp<SkImage> image = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
    if (image)
    {
      canvas->drawImage(image, dx, dy, SkSamplingOptions(), &paint);
      return true;
    }
  }

  // Unpack mono bits or non-contiguous gray rows into a contiguous alpha-8 buffer
  unsigned char *tmp = (unsigned char *)malloc((size_t)w * h);
  if (!tmp) return false;

  for (int yy = 0; yy < h; yy ++)
  {
    const unsigned char *in = pitch > 0 ? src + (size_t)yy * pitch : src + (size_t)(h - 1 - yy) * -pitch;
    unsigned char *out = tmp + (size_t)yy * w;
    if (mono)
    {
      for (int xx = 0; xx < w; xx ++)
        out[xx] = (in[xx >> 3] & (0x80 >> (xx & 7))) ? 255 : 0;
    }
    else
    {
      memcpy(out, in, w);
    }
  }

  const SkPixmap pixmap(a8info, tmp, (size_t)w);
  sk_sp<SkImage> image = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
  bool ok = false;
  if (image)
  {
    canvas->drawImage(image, dx, dy, SkSamplingOptions(), &paint);
    ok = true;
  }
  free(tmp);
  return ok;
}

bool SWELL_SkiaPushClipRegion(LICE_IBitmap *bitmap)
{
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, NULL, NULL, NULL, NULL);
  if (!canvas) return false;
  canvas->save();
  return true;
}

bool SWELL_SkiaSetClipRegion(LICE_IBitmap *bitmap, const RECT *r, int addx, int addy)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas || !r) return false;

  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);
  canvas->clipRect(SkRect::MakeLTRB((SkScalar)(r->left + addx + xoff),
                                    (SkScalar)(r->top + addy + yoff),
                                    (SkScalar)(r->right + addx + xoff),
                                    (SkScalar)(r->bottom + addy + yoff)));
  return true;
}

bool SWELL_SkiaPopClipRegion(LICE_IBitmap *bitmap)
{
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, NULL, NULL, NULL, NULL);
  if (!canvas) return false;
  canvas->restore();
  return true;
}

static sk_sp<SkFontMgr> SWELL_SkiaFontMgr()
{
#if defined(__linux__)
  static SkFontMgr* s_mgr =
    SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType()).release();
  return sk_ref_sp(s_mgr);

#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
  static SkFontMgr* s_mgr = SkFontMgr_New_Custom_Empty().release();
  return sk_ref_sp(s_mgr);

#else
  return nullptr;
#endif
}
void *SWELL_SkiaFontFromFile(const char *path, int index, float pixel_size)
{
  if (!path || !path[0] || pixel_size <= 0.0f) return nullptr;

  sk_sp<SkFontMgr> fm = SWELL_SkiaFontMgr();
  if (!fm)
  {
    fprintf(stderr, "SWELL_SKIA: no SkFontMgr for font '%s'\n", path);
    return nullptr;
  }

  sk_sp<SkTypeface> tf = fm->makeFromFile(path, index);
  if (!tf)
  {
    fprintf(stderr, "SWELL_SKIA: failed to load font '%s' index=%d size=%f\n",
            path, index, pixel_size);
    return nullptr;
  }

  SkFont *font = new SkFont(std::move(tf), pixel_size);
  font->setEdging(SkFont::Edging::kAntiAlias);
  font->setHinting(SkFontHinting::kSlight);
  font->setSubpixel(false);

  return font;
}

void SWELL_SkiaReleaseFont(void *skia_font)
{
  delete (SkFont *)skia_font;
}

bool SWELL_SkiaGetFontMetrics(void *skia_font, int *ascent, int *descent, int *lineh, int *charw)
{
  if (!skia_font) return false;

  const SkFont *font = static_cast<const SkFont *>(skia_font);

  SkFontMetrics m;
  font->getMetrics(&m);

  const int asc = static_cast<int>(-m.fAscent + 0.5f);
  const int des = static_cast<int>( m.fDescent + 0.5f);

  if (ascent)  *ascent  = asc;
  if (descent) *descent = des;

  if (lineh)
    *lineh = static_cast<int>(-m.fAscent + m.fDescent + m.fLeading + 0.5f);

  if (charw)
  {
    SkGlyphID x_glyph = font->unicharToGlyph('x');
    SkScalar x_advance = x_glyph ? font->getWidth(x_glyph) : 0;

    *charw = wdl_max(1, static_cast<int>(x_advance + 0.5f));
  }

  return true;
}

uint16_t SWELL_SkiaMeasureUnichar(void *skia_font, int codepoint,
                                  float *advance, float *ink_l, float *ink_r)
{
  if (!skia_font)
  {
    if (advance) *advance = 0.0f;
    if (ink_l)   *ink_l   = 0.0f;
    if (ink_r)   *ink_r   = 0.0f;
    return 0;
  }

  const SkFont *font = static_cast<const SkFont *>(skia_font);

  SkGlyphID gid = font->unicharToGlyph(codepoint);
  SkRect bounds = SkRect::MakeEmpty();
  SkScalar adv = 0.0f;

  // Measure glyph 0 too. It is the font's .notdef glyph and may have a valid advance.
  font->getWidthsBounds(
      SkSpan<const SkGlyphID>(&gid, 1),
      SkSpan<SkScalar>(&adv, 1),
      SkSpan<SkRect>(&bounds, 1),
      nullptr);

  if (advance) *advance = adv;
  if (ink_l)   *ink_l   = bounds.fLeft;
  if (ink_r)   *ink_r   = bounds.fRight;

  return static_cast<uint16_t>(gid);
}

bool SWELL_SkiaDrawGlyphRun(LICE_IBitmap *bitmap, void *skia_font,
                             const uint16_t *glyphs, const float *xpos, int count,
                             float baseline_y, unsigned int lice_color)
{
  if (!bitmap || !skia_font || !glyphs || !xpos || count <= 0) return false;

  int xoff = 0, yoff = 0, clipw = 0, cliph = 0;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph);
  if (!canvas) return false;

  SkAutoCanvasRestore acr(canvas, true);
  swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

  SkPoint pos_buf[256];
  SkPoint *positions = count <= 256 ? pos_buf : new SkPoint[count];

  const float dy = baseline_y + (float)yoff;
  const float dx = (float)xoff;

  for (int i = 0; i < count; i++)
    positions[i] = SkPoint::Make(xpos[i] + dx, dy);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(swell_skia_color_from_lice(lice_color, 1.0f));
  paint.setBlendMode(SkBlendMode::kSrcOver);

  canvas->drawGlyphs(
      SkSpan<const SkGlyphID>((const SkGlyphID *)glyphs, count),
      SkSpan<const SkPoint>(positions, count),
      SkPoint::Make(0.0f, 0.0f),
      *(const SkFont *)skia_font,
      paint);

  if (positions != pos_buf) delete[] positions;
  return true;
}

#endif
#endif
