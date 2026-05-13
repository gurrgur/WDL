/*
  Optional Skia-backed raster surfaces for SWELL generic GDI.

  This file deliberately keeps the first Skia integration small: it provides a
  Skia-owned raster surface that also behaves as a LICE_IBitmap. Existing SWELL
  code can keep using the mature generic/LICE paint traversal while newer code
  can discover the SkCanvas via Extended(SWELL_SKIA_EXT_CANVAS, NULL).
*/

#ifdef SWELL_SKIA_GDI
#ifndef SWELL_PROVIDED_BY_APP

#include <chrono>
#include <dlfcn.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
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
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"
#if defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif

#include "swell.h"
#include "swell-internal.h"
#include "swell-gdi-skia.h"

class SWELL_SkiaGPUBitmap;

struct SWELL_SkiaGPUContext
{
  sk_sp<GrDirectContext> context;
};

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

class SWELL_SkiaGPUBitmap : public LICE_IBitmap
{
public:
  SWELL_SkiaGPUBitmap(SWELL_SkiaGPUContext *ctx, int w, int h)
    : m_context(ctx), m_fb(NULL), m_allocsize(0), m_width(0), m_height(0), m_span(0),
      m_cpu_dirty(false), m_gpu_dirty(false), m_window_w(0), m_window_h(0)
  {
    resize(w, h);
  }

  ~SWELL_SkiaGPUBitmap() override
  {
    // Skip syncCPUToGPU(): the surface is being destroyed immediately after,
    // so uploading CPU pixels to GPU would be wasted work.
    m_surface.reset();
    m_window_surface.reset();
    free(m_fb);
  }

  LICE_pixel *getBits() override
  {
    if (!ensureCPUBuffer()) return NULL;
    // Only read GPU→CPU when GPU has new content not yet in the CPU buffer.
    // Skipping the readback when m_gpu_dirty is false avoids the costly
    // GPU stall on every frame for callers that access getBits() repeatedly.
    if (!m_cpu_dirty && m_gpu_dirty) readGPUToCPU();
    m_cpu_dirty = true;
    return (LICE_pixel *)(((UINT_PTR)m_fb + LICE_MEMBITMAP_ALIGNAMT) & ~(UINT_PTR)LICE_MEMBITMAP_ALIGNAMT);
  }

  int getWidth() override { return m_width; }
  int getHeight() override { return m_height; }
  int getRowSpan() override { return m_span; }

  bool resize(int w, int h) override
  {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w == m_width && h == m_height && m_surface) return false;

    syncCPUToGPU();
    m_surface.reset();
    m_cpu_dirty = false;
    m_gpu_dirty = false;

    m_width = w > 0 && h > 0 ? w : 0;
    m_height = w > 0 && h > 0 ? h : 0;
    m_span = m_width > 0 ? ((m_width * 4 + 15) & ~15) / 4 : 0;

    if (m_width > 0 && m_height > 0 && m_context && m_context->context)
    {
      SkImageInfo info = SkImageInfo::Make(m_width, m_height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
      m_surface = SkSurfaces::RenderTarget(m_context->context.get(), skgpu::Budgeted::kNo,
                                           info, 0, kTopLeft_GrSurfaceOrigin, NULL);
      if (!m_surface)
      {
        m_width = m_height = m_span = 0;
        return false;
      }
      m_surface->getCanvas()->clear(SK_ColorTRANSPARENT);
      // GPU surface was just written to (cleared); CPU buffer is now stale.
      m_gpu_dirty = true;
    }
    return true;
  }

  INT_PTR Extended(int id, void *data) override
  {
    if (data) return 0;
    if (id == SWELL_SKIA_EXT_GPU_BITMAP) return 1;
    if (id == SWELL_SKIA_EXT_GPU_CONTEXT) return (INT_PTR)m_context;
    if (!m_surface) return 0;
    if (id == SWELL_SKIA_EXT_CANVAS)
    {
      syncCPUToGPU();
      m_gpu_dirty = true; // canvas is now live; assume draws will happen
      return (INT_PTR)m_surface->getCanvas();
    }
    if (id == SWELL_SKIA_EXT_SURFACE)
    {
      syncCPUToGPU();
      return (INT_PTR)m_surface.get();
    }
    return 0;
  }

  bool syncCPUToGPU()
  {
    if (!m_cpu_dirty || !m_surface || !m_fb || m_width <= 0 || m_height <= 0) return true;
    const LICE_pixel *bits = (const LICE_pixel *)(((UINT_PTR)m_fb + LICE_MEMBITMAP_ALIGNAMT) & ~(UINT_PTR)LICE_MEMBITMAP_ALIGNAMT);
    const SkImageInfo info = SkImageInfo::Make(m_width, m_height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    const SkPixmap pixmap(info, bits, (size_t)m_span * 4);
    m_surface->writePixels(pixmap, 0, 0);
    m_cpu_dirty = false;
    return true;
  }

  bool presentToGLFramebuffer(SWELL_SkiaGPUContext *ctx, int w, int h)
  {
    if (!ctx || !ctx->context || !m_surface || w <= 0 || h <= 0) return false;
    syncCPUToGPU();

    // Query the actual stencil depth once and cache it (doesn't change per-frame).
    static int s_stencil_bits = -1;
    if (s_stencil_bits < 0)
    {
      s_stencil_bits = 8;
      typedef void (*PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)(
        unsigned target, unsigned attachment, unsigned pname, int *params);
      auto fn = (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)
        dlsym(RTLD_DEFAULT, "glGetFramebufferAttachmentParameteriv");
      if (fn)
      {
        int bits = 0;
        fn(0x8D40 /*GL_FRAMEBUFFER*/, 0x8212 /*GL_DEPTH_STENCIL_ATTACHMENT*/,
           0x8217 /*GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE*/, &bits);
        if (bits > 0) s_stencil_bits = bits;
      }
    }

    // Cache the SkSurface wrapping FBO 0 across frames. WrapBackendRenderTarget
    // allocates Skia-side objects but no new GPU memory. Reusing it avoids the
    // per-frame allocation. kSrc blend overwrites all pixels so the undefined
    // back-buffer content after SDL_GL_SwapWindow doesn't matter.
    if (!m_window_surface || m_window_w != w || m_window_h != h)
    {
      GrGLFramebufferInfo fbinfo;
      fbinfo.fFBOID = 0;
      fbinfo.fFormat = 0x8058; // GL_RGBA8

      GrBackendRenderTarget target = GrBackendRenderTargets::MakeGL(w, h, 0, s_stencil_bits, fbinfo);
      m_window_surface = SkSurfaces::WrapBackendRenderTarget(ctx->context.get(), target,
                                                             kBottomLeft_GrSurfaceOrigin,
                                                             kRGBA_8888_SkColorType, NULL, NULL);
      m_window_w = m_window_surface ? w : 0;
      m_window_h = m_window_surface ? h : 0;
    }
    if (!m_window_surface) return false;

    SkCanvas *canvas = m_window_surface->getCanvas();

    // Use draw() instead of makeImageSnapshot() + drawImageRect().
    // makeImageSnapshot() makes m_surface copy-on-write: the next frame's
    // readGPUToCPU() reads from the new empty backing texture, then
    // syncCPUToGPU() writes those black pixels back, wiping all glyphs.
    // draw() reads directly from the surface without stealing its texture.
    //
    // Use kSrc blend to directly overwrite the framebuffer, avoiding the
    // separate clear pass that kSrcOver (the default) would require.
    SkPaint blit_paint;
    blit_paint.setBlendMode(SkBlendMode::kSrc);

    if (m_width != w || m_height != h)
    {
      canvas->save();
      canvas->scale((SkScalar)w / m_width, (SkScalar)h / m_height);
      m_surface->draw(canvas, 0, 0, SkSamplingOptions(), &blit_paint);
      canvas->restore();
    }
    else
    {
      m_surface->draw(canvas, 0, 0, SkSamplingOptions(), &blit_paint);
    }
    ctx->context->flushAndSubmit(m_window_surface.get());
    return true;
  }

private:
  bool ensureCPUBuffer()
  {
    if (m_width <= 0 || m_height <= 0 || m_span <= 0) return false;
    const int sz = m_height * m_span * (int)sizeof(LICE_pixel) + LICE_MEMBITMAP_ALIGNAMT;
    if (!m_fb || m_allocsize < sz)
    {
      void *p = realloc(m_fb, sz);
      if (!p) return false;
      if (m_allocsize < sz) memset((char *)p + m_allocsize, 0, sz - m_allocsize);
      m_fb = (LICE_pixel *)p;
      m_allocsize = sz;
    }
    return true;
  }

  bool readGPUToCPU()
  {
    if (!m_surface || !m_fb) return false;
    LICE_pixel *bits = (LICE_pixel *)(((UINT_PTR)m_fb + LICE_MEMBITMAP_ALIGNAMT) & ~(UINT_PTR)LICE_MEMBITMAP_ALIGNAMT);
    const SkImageInfo info = SkImageInfo::Make(m_width, m_height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    const bool ok = m_surface->readPixels(info, bits, (size_t)m_span * 4, 0, 0);
    if (ok) m_gpu_dirty = false;
    return ok;
  }

  SWELL_SkiaGPUContext *m_context;
  LICE_pixel *m_fb;
  int m_allocsize;
  int m_width;
  int m_height;
  int m_span;
  bool m_cpu_dirty;
  bool m_gpu_dirty;
  sk_sp<SkSurface> m_surface;
  sk_sp<SkSurface> m_window_surface; // cached FBO 0 wrapper for present
  int m_window_w, m_window_h;
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

void *SWELL_CreateSkiaGLGPUContext()
{
  SWELL_SkiaGPUContext *ctx = new SWELL_SkiaGPUContext;
  if (!ctx) return NULL;
  sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
  ctx->context = interface ? GrDirectContexts::MakeGL(interface) : NULL;
  if (!ctx->context)
  {
    delete ctx;
    return NULL;
  }
  return ctx;
}

void SWELL_SkiaGPUContextPeriodicCleanup(void *gpu_context)
{
  SWELL_SkiaGPUContext *ctx = (SWELL_SkiaGPUContext *)gpu_context;
  if (!ctx || !ctx->context) return;
  // Evict GPU resources (glyph atlas entries, cached paths, etc.) that have
  // not been used for > 2 seconds to keep VRAM use bounded.
  ctx->context->performDeferredCleanup(std::chrono::seconds(2));
}

void SWELL_DestroySkiaGPUContext(void *gpu_context)
{
  SWELL_SkiaGPUContext *ctx = (SWELL_SkiaGPUContext *)gpu_context;
  if (!ctx) return;
  if (ctx->context) ctx->context->flushAndSubmit();
  delete ctx;
}

LICE_IBitmap *SWELL_CreateSkiaGPUBitmap(void *gpu_context, int w, int h)
{
  SWELL_SkiaGPUContext *ctx = (SWELL_SkiaGPUContext *)gpu_context;
  if (!ctx || !ctx->context) return NULL;
  SWELL_SkiaGPUBitmap *bm = new SWELL_SkiaGPUBitmap(ctx, w, h);
  if (!bm) return NULL;
  if (w > 0 && h > 0 && (bm->getWidth() != w || bm->getHeight() != h))
  {
    delete bm;
    return NULL;
  }
  return bm;
}

// Walk the SubBitmap chain and return the GPU context if the root is a GPU bitmap.
void *SWELL_GetGPUContextFromBitmap(LICE_IBitmap *bitmap)
{
  while (bitmap && bitmap->Extended(LICE_SubBitmap::LICE_GET_SUBBITMAP_VERSION, NULL) == LICE_SubBitmap::LICE_SUBBITMAP_VERSION)
    bitmap = ((LICE_SubBitmap *)bitmap)->m_parent;
  return bitmap ? (void *)bitmap->Extended(SWELL_SKIA_EXT_GPU_CONTEXT, NULL) : NULL;
}

bool SWELL_IsSkiaGPUBitmap(LICE_IBitmap *bitmap)
{
  while (bitmap && bitmap->Extended(LICE_SubBitmap::LICE_GET_SUBBITMAP_VERSION, NULL) == LICE_SubBitmap::LICE_SUBBITMAP_VERSION)
    bitmap = ((LICE_SubBitmap *)bitmap)->m_parent;
  return bitmap && bitmap->Extended(SWELL_SKIA_EXT_GPU_BITMAP, NULL) != 0;
}

bool SWELL_PresentSkiaGPUBitmapToGLFramebuffer(void *gpu_context, LICE_IBitmap *bitmap, int w, int h)
{
  while (bitmap && bitmap->Extended(LICE_SubBitmap::LICE_GET_SUBBITMAP_VERSION, NULL) == LICE_SubBitmap::LICE_SUBBITMAP_VERSION)
    bitmap = ((LICE_SubBitmap *)bitmap)->m_parent;
  SWELL_SkiaGPUBitmap *gpu_bitmap = bitmap && bitmap->Extended(SWELL_SKIA_EXT_GPU_BITMAP, NULL) ?
    static_cast<SWELL_SkiaGPUBitmap *>(bitmap) : NULL;
  return gpu_bitmap && gpu_bitmap->presentToGLFramebuffer((SWELL_SkiaGPUContext *)gpu_context, w, h);
}

void *SWELL_GetSkiaCanvasFromBitmap(LICE_IBitmap *bitmap)
{
  return bitmap ? (void *)bitmap->Extended(SWELL_SKIA_EXT_CANVAS, NULL) : NULL;
}

// Returns the Skia canvas for bitmap, accumulating SubBitmap offsets into
// *xoff/*yoff and setting *clipw/*cliph to the SubBitmap's clipped dimensions.
// *has_offset is set to true when the bitmap is a SubBitmap (xoff/yoff != 0
// or the clip is smaller than the root surface) — used to skip the unnecessary
// save/restore + clipRect overhead when drawing directly to the root surface.
static SkCanvas *swell_skia_canvas_from_bitmap(LICE_IBitmap *bitmap,
    int *xoff, int *yoff, int *clipw, int *cliph, bool *has_offset = nullptr)
{
  if (xoff) *xoff = 0;
  if (yoff) *yoff = 0;
  if (clipw) *clipw = bitmap ? bitmap->getWidth() : 0;
  if (cliph) *cliph = bitmap ? bitmap->getHeight() : 0;

  bool sub = false;
  while (bitmap && bitmap->Extended(LICE_SubBitmap::LICE_GET_SUBBITMAP_VERSION, NULL) == LICE_SubBitmap::LICE_SUBBITMAP_VERSION)
  {
    LICE_SubBitmap *sb = (LICE_SubBitmap *)bitmap;
    if (xoff) *xoff += sb->m_x;
    if (yoff) *yoff += sb->m_y;
    bitmap = sb->m_parent;
    sub = true;
  }

  if (has_offset) *has_offset = sub;
  return bitmap ? (SkCanvas *)bitmap->Extended(SWELL_SKIA_EXT_CANVAS, NULL) : NULL;
}

static LICE_IBitmap *swell_skia_root_bitmap_from_bitmap(LICE_IBitmap *bitmap,
                                                        int *xoff = NULL,
                                                        int *yoff = NULL)
{
  if (xoff) *xoff = 0;
  if (yoff) *yoff = 0;

  while (bitmap && bitmap->Extended(LICE_SubBitmap::LICE_GET_SUBBITMAP_VERSION, NULL) == LICE_SubBitmap::LICE_SUBBITMAP_VERSION)
  {
    LICE_SubBitmap *sb = (LICE_SubBitmap *)bitmap;
    if (xoff) *xoff += sb->m_x;
    if (yoff) *yoff += sb->m_y;
    bitmap = sb->m_parent;
  }
  return bitmap;
}

static void swell_skia_clip_to_bitmap(SkCanvas *canvas, int xoff, int yoff, int clipw, int cliph)
{
  canvas->clipRect(SkRect::MakeXYWH((SkScalar)xoff, (SkScalar)yoff, (SkScalar)clipw, (SkScalar)cliph));
}

// RAII guard that saves the canvas and sets the SubBitmap clip only when the
// drawing target is a SubBitmap (has_offset == true). Direct root-surface draws
// skip the save/clipRect/restore cycle entirely.
struct swell_canvas_guard
{
  SkCanvas *canvas;
  bool      saved;
  swell_canvas_guard(SkCanvas *c, bool has_offset, int xoff, int yoff, int clipw, int cliph)
    : canvas(c), saved(has_offset)
  {
    if (has_offset) { canvas->save(); swell_skia_clip_to_bitmap(c, xoff, yoff, clipw, cliph); }
  }
  ~swell_canvas_guard() { if (saved) canvas->restore(); }
};

static SkColor swell_skia_color_from_lice(unsigned int c, float alpha)
{
  if (alpha < 0.0f) alpha = 0.0f;
  else if (alpha > 1.0f) alpha = 1.0f;
  const int a = (int)(255.0f * alpha + 0.5f);
  return SkColorSetARGB(a, LICE_GETR(c), LICE_GETG(c), LICE_GETB(c));
}

bool SWELL_SkiaFillRect(LICE_IBitmap *bitmap, int x, int y, int w, int h, unsigned int lice_color, float alpha)
{
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || w <= 0 || h <= 0) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || w <= 0 || h <= 0) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || r <= l || b <= t || (!do_fill && !do_stroke)) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(dst, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return false;
  LICE_IBitmap *dst_root = swell_skia_root_bitmap_from_bitmap(dst);
  SkSurface *dst_surf = dst_root ? (SkSurface *)dst_root->Extended(SWELL_SKIA_EXT_SURFACE, NULL) : NULL;

  if (opacity < 0.0f) opacity = 0.0f;
  else if (opacity > 1.0f) opacity = 1.0f;

  SkPaint paint;
  paint.setAlphaf(opacity);
  paint.setBlendMode(use_alpha || opacity < 1.0f ? SkBlendMode::kSrcOver : SkBlendMode::kSrc);
  const SkSamplingOptions sampling(filter ? SkFilterMode::kLinear : SkFilterMode::kNearest);

  // GPU/Skia surface fast path: walk the src SubBitmap chain to find a Skia
  // surface and blit via SkSurface::draw() — no copy-on-write on the source.
  // The GPU-to-GPU block always needs a save/restore for its CTM changes.
  {
    int src_sub_x = 0, src_sub_y = 0;
    LICE_IBitmap *b = swell_skia_root_bitmap_from_bitmap(src, &src_sub_x, &src_sub_y);
    SkSurface *src_surf = b ? (SkSurface *)b->Extended(SWELL_SKIA_EXT_SURFACE, NULL) : NULL;
    if (src_surf)
    {
      SkAutoCanvasRestore acr(canvas, true);
      if (has_offset) swell_skia_clip_to_bitmap(canvas, xoff, yoff, clipw, cliph);

      // Clip to destination rect, then map source rect to dest rect via CTM.
      canvas->clipRect(SkRect::MakeXYWH((SkScalar)(x + xoff), (SkScalar)(y + yoff), (SkScalar)w, (SkScalar)h));
      canvas->translate((SkScalar)(x + xoff), (SkScalar)(y + yoff));
      if (w != sw || h != sh)
        canvas->scale((SkScalar)w / sw, (SkScalar)h / sh);

      if (src_surf == dst_surf)
      {
        sk_sp<SkSurface> tmp = src_surf->makeSurface(sw, sh);
        if (!tmp) return false;

        SkCanvas *tmp_canvas = tmp->getCanvas();
        tmp_canvas->clear(SK_ColorTRANSPARENT);
        tmp_canvas->translate(-(SkScalar)(sx + src_sub_x), -(SkScalar)(sy + src_sub_y));
        src_surf->draw(tmp_canvas, 0, 0, sampling, NULL);
        tmp->draw(canvas, 0, 0, sampling, &paint);
      }
      else
      {
        canvas->translate(-(SkScalar)(sx + src_sub_x), -(SkScalar)(sy + src_sub_y));
        src_surf->draw(canvas, 0, 0, sampling, &paint);
      }
      return true;
    }
  }

  // CPU fallback: wrap the src pixels without copying (RasterFromPixmap keeps
  // a pointer rather than copying, so the image must not outlive this call).
  if (!src->getBits()) return false;
  const SkAlphaType alpha_type = use_alpha ? kUnpremul_SkAlphaType : kOpaque_SkAlphaType;
  SkImageInfo info = SkImageInfo::Make(src->getWidth(), src->getHeight(), kBGRA_8888_SkColorType, alpha_type);
  SkPixmap pixmap(info, src->getBits(), (size_t)src->getRowSpan() * 4);
  sk_sp<SkImage> image = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
  if (!image) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);
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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || !pts || npts < 2 || (!do_fill && !do_stroke)) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || r <= l || b <= t || (!do_fill && !do_stroke)) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas || !pts || npts < 3) return false;

  SkPath path;
  path.moveTo((SkScalar)(startx + addx + xoff), (SkScalar)(starty + addy + yoff));
  for (int x = 0; x < npts - 2; x += 3)
    path.cubicTo((SkScalar)(pts[x].x + addx + xoff), (SkScalar)(pts[x].y + addy + yoff),
                 (SkScalar)(pts[x+1].x + addx + xoff), (SkScalar)(pts[x+1].y + addy + yoff),
                 (SkScalar)(pts[x+2].x + addx + xoff), (SkScalar)(pts[x+2].y + addy + yoff));

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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
  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
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

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

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

  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setBlendMode(SkBlendMode::kSrcOver);
  paint.setColor(swell_skia_color_from_lice(lice_color, 1.0f));

  const SkScalar dx = (SkScalar)(x + xoff);
  const SkScalar dy = (SkScalar)(y + yoff);
  const SkImageInfo a8info = SkImageInfo::Make(w, h, kAlpha_8_SkColorType, kPremul_SkAlphaType);

  if (!mono && pitch > 0)
  {
    // Wrap the FreeType gray buffer directly with the actual row stride —
    // SkPixmap handles non-tight packing, so no copy is needed regardless of pitch.
    const SkPixmap pixmap(a8info, src, (size_t)pitch);
    sk_sp<SkImage> image = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
    if (image)
    {
      canvas->drawImage(image, dx, dy, SkSamplingOptions(), &paint);
      return true;
    }
  }

  // Unpack mono bits or negative-pitch (bottom-up) gray rows into a temp buffer.
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

struct SWELL_SkiaFont
{
  SkFont font;

  struct GlyphEntry
  {
    uint16_t gid;
    float    advance, ink_l, ink_r;
  };

  // Direct-map cache for codepoints [0, 255] (ASCII + Latin-1 Supplement).
  // Populated lazily; avoids Skia's mutex-guarded strike cache in the tight
  // per-character DrawText loop. 256 entries covers all single-byte Unicode.
  static const int GLYPH_CACHE_SIZE = 256;
  GlyphEntry cache[GLYPH_CACHE_SIZE];
  bool       cache_valid[GLYPH_CACHE_SIZE];

  explicit SWELL_SkiaFont(SkFont &&f) : font(std::move(f))
  {
    memset(cache_valid, 0, sizeof(cache_valid));
  }

  // Returns glyph metrics for codepoint, using the cache for [0, 255].
  uint16_t measure(int codepoint, float *adv_out, float *ink_l_out, float *ink_r_out)
  {
    if ((unsigned)codepoint < (unsigned)GLYPH_CACHE_SIZE && cache_valid[codepoint])
    {
      const GlyphEntry &e = cache[codepoint];
      if (adv_out)   *adv_out   = e.advance;
      if (ink_l_out) *ink_l_out = e.ink_l;
      if (ink_r_out) *ink_r_out = e.ink_r;
      return e.gid;
    }

    SkGlyphID gid = font.unicharToGlyph(codepoint);
    SkRect    bounds = SkRect::MakeEmpty();
    SkScalar  adv = 0.0f;

    font.getWidthsBounds(
        SkSpan<const SkGlyphID>(&gid, 1),
        SkSpan<SkScalar>(&adv, 1),
        SkSpan<SkRect>(&bounds, 1),
        nullptr);

    if ((unsigned)codepoint < (unsigned)GLYPH_CACHE_SIZE)
    {
      GlyphEntry &e = cache[codepoint];
      e.gid     = (uint16_t)gid;
      e.advance = adv;
      e.ink_l   = bounds.fLeft;
      e.ink_r   = bounds.fRight;
      cache_valid[codepoint] = true;
    }

    if (adv_out)   *adv_out   = adv;
    if (ink_l_out) *ink_l_out = bounds.fLeft;
    if (ink_r_out) *ink_r_out = bounds.fRight;
    return (uint16_t)gid;
  }
};

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

  SkFont skfont(std::move(tf), pixel_size);
  skfont.setEdging(SkFont::Edging::kAntiAlias);
  skfont.setHinting(SkFontHinting::kFull);
  skfont.setSubpixel(false);

  SWELL_SkiaFont *sf = new SWELL_SkiaFont(std::move(skfont));

  // Pre-warm the entire 256-entry glyph metric cache with two batch Skia calls
  // (one unicharToGlyphs + one getWidthsBounds) rather than hitting the strike
  // cache lazily per character on the first paint of each glyph.
  {
    SkUnichar codepoints[SWELL_SkiaFont::GLYPH_CACHE_SIZE];
    SkGlyphID gids[SWELL_SkiaFont::GLYPH_CACHE_SIZE];
    SkScalar  advances[SWELL_SkiaFont::GLYPH_CACHE_SIZE];
    SkRect    bounds[SWELL_SkiaFont::GLYPH_CACHE_SIZE];

    for (int i = 0; i < SWELL_SkiaFont::GLYPH_CACHE_SIZE; i++)
      codepoints[i] = (SkUnichar)i;

    sf->font.unicharsToGlyphs(
        SkSpan<const SkUnichar>(codepoints, SWELL_SkiaFont::GLYPH_CACHE_SIZE),
        SkSpan<SkGlyphID>(gids, SWELL_SkiaFont::GLYPH_CACHE_SIZE));
    sf->font.getWidthsBounds(
        SkSpan<const SkGlyphID>(gids, SWELL_SkiaFont::GLYPH_CACHE_SIZE),
        SkSpan<SkScalar>(advances, SWELL_SkiaFont::GLYPH_CACHE_SIZE),
        SkSpan<SkRect>(bounds, SWELL_SkiaFont::GLYPH_CACHE_SIZE),
        nullptr);

    for (int i = 0; i < SWELL_SkiaFont::GLYPH_CACHE_SIZE; i++)
    {
      sf->cache[i] = { gids[i], advances[i], bounds[i].fLeft, bounds[i].fRight };
      sf->cache_valid[i] = true;
    }
  }

  return sf;
}

void SWELL_SkiaReleaseFont(void *skia_font)
{
  delete (SWELL_SkiaFont *)skia_font;
}

bool SWELL_SkiaGetFontMetrics(void *skia_font, int *ascent, int *descent, int *lineh, int *charw)
{
  if (!skia_font) return false;

  const SkFont &font = ((SWELL_SkiaFont *)skia_font)->font;

  SkFontMetrics m;
  font.getMetrics(&m);

  const int asc = static_cast<int>(-m.fAscent + 0.5f);
  const int des = static_cast<int>( m.fDescent + 0.5f);

  if (ascent)  *ascent  = asc;
  if (descent) *descent = des;

  if (lineh)
    *lineh = static_cast<int>(-m.fAscent + m.fDescent + m.fLeading + 0.5f);

  if (charw)
  {
    SkGlyphID x_glyph = font.unicharToGlyph('x');
    SkScalar x_advance = x_glyph ? font.getWidth(x_glyph) : 0;

    *charw = wdl_max(1, static_cast<int>(x_advance + 0.5f));
  }

  return true;
}

bool SWELL_SkiaMeasureTextRun(void *skia_font, const char *utf8, int utf8_len,
                              float *advance, float *ink_l, float *ink_r)
{
  if (!skia_font || !utf8 || utf8_len <= 0)
  {
    if (advance) *advance = 0.0f;
    if (ink_l)   *ink_l   = 0.0f;
    if (ink_r)   *ink_r   = 0.0f;
    return false;
  }

  const SkFont &font = ((SWELL_SkiaFont *)skia_font)->font;
  SkRect bounds = SkRect::MakeEmpty();
  const SkScalar adv = font.measureText(utf8, (size_t)utf8_len, SkTextEncoding::kUTF8,
                                        (ink_l || ink_r) ? &bounds : nullptr);
  if (advance) *advance = adv;
  if (ink_l)   *ink_l   = bounds.fLeft;
  if (ink_r)   *ink_r   = bounds.fRight;
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

  return ((SWELL_SkiaFont *)skia_font)->measure(codepoint, advance, ink_l, ink_r);
}

bool SWELL_SkiaDrawTextRun(LICE_IBitmap *bitmap, void *skia_font,
                           const char *utf8, int utf8_len,
                           float x, float baseline_y, unsigned int lice_color)
{
  if (!bitmap || !skia_font || !utf8 || utf8_len <= 0) return false;

  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(swell_skia_color_from_lice(lice_color, 1.0f));
  paint.setBlendMode(SkBlendMode::kSrcOver);

  const SkFont &font = ((SWELL_SkiaFont *)skia_font)->font;
  canvas->drawSimpleText(utf8, (size_t)utf8_len, SkTextEncoding::kUTF8,
                         (SkScalar)(x + xoff), (SkScalar)(baseline_y + yoff), font, paint);
  return true;
}

bool SWELL_SkiaDrawGlyphRun(LICE_IBitmap *bitmap, void *skia_font,
                             const uint16_t *glyphs, const float *xpos, int count,
                             float baseline_y, unsigned int lice_color)
{
  if (!bitmap || !skia_font || !glyphs || !xpos || count <= 0) return false;

  int xoff = 0, yoff = 0, clipw = 0, cliph = 0; bool has_offset = false;
  SkCanvas *canvas = swell_skia_canvas_from_bitmap(bitmap, &xoff, &yoff, &clipw, &cliph, &has_offset);
  if (!canvas) return false;

  swell_canvas_guard guard(canvas, has_offset, xoff, yoff, clipw, cliph);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(swell_skia_color_from_lice(lice_color, 1.0f));
  paint.setBlendMode(SkBlendMode::kSrcOver);

  SWELL_SkiaFont *sf = (SWELL_SkiaFont *)skia_font;

  // Build SkPoint positions on the stack for short runs, heap for longer ones.
  SkPoint stack_pts[256];
  SkPoint *pts = count <= 256 ? stack_pts : (SkPoint *)malloc((size_t)count * sizeof(SkPoint));
  if (!pts) return false;

  for (int i = 0; i < count; i++)
    pts[i] = SkPoint::Make((SkScalar)(xpos[i] + xoff), 0.0f);

  canvas->drawGlyphs(SkSpan<const SkGlyphID>((const SkGlyphID *)glyphs, count),
                     SkSpan<const SkPoint>(pts, count),
                     SkPoint::Make(0.0f, (SkScalar)(baseline_y + yoff)),
                     sf->font, paint);

  if (pts != stack_pts) free(pts);
  return true;
}

#endif
#endif
