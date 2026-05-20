/*
  SWELL2 GDI module — headless build
  Implements HDC lifecycle, GDI object management, drawing functions
  (Skia backend), text metrics, color conversion, and the GDI object pool.
*/

#include "swell-gdi-internalpool.h"

// swell-functions.h defines Polygon(a,b,c) as SWELL_Polygon which clashes
// with SkPath::Polygon. Undefine it here — our SWELL_Polygon is a real
// function, not a macro.
#undef Polygon

#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

#include <core/SkPath.h>
#include <core/SkRRect.h>
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkTypeface.h>
#include <core/SkFontMgr.h>
#include <core/SkBlendMode.h>
#include <ports/SkFontMgr_fontconfig.h>
#include <ports/SkFontScanner_FreeType.h>

// Image decoding (for LoadNamedImage)
#include <core/SkData.h>
#include <codec/SkCodec.h>
#include <codec/SkPngDecoder.h>
#include <codec/SkJpegDecoder.h>
#include <codec/SkBmpDecoder.h>
#include <codec/SkGifDecoder.h>
#include <codec/SkWebpDecoder.h>

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

swell_theme g_swell_theme;
int g_swell_theme_mode = SWELL_THEME_LIGHT;
const char *g_swell_deffont_face = "Arial";

static HFONT g_swell_default_font_instance = nullptr;
HFONT g_swell_default_font = nullptr; // extern, may alias the static

int g_swell_ui_scale = 256;
bool g_swell_subpixel_text = true;

// ---------------------------------------------------------------------------
// Pool infrastructure
// ---------------------------------------------------------------------------

static std::mutex g_hdc_pool_mutex;
static HDC__ *g_hdc_free_list = nullptr;
static int g_hdc_pool_count = 0;

static std::mutex g_gdiobj_pool_mutex;
static HGDIOBJ__ *g_gdiobj_free_list = nullptr;
static int g_gdiobj_pool_count = 0;

// Sentinel pattern: low ints cast to HGDIOBJ represent "nothing selected"
#define GDI_NULL_SENTINEL(type) ((HGDIOBJ)(INT_PTR)(type))

HDC__ *SWELL_GDP_CTX_NEW()
{
  std::lock_guard<std::mutex> lock(g_hdc_pool_mutex);
  HDC__ *p = nullptr;
  if (g_hdc_free_list) {
    p = g_hdc_free_list;
    g_hdc_free_list = p->_next;
    g_hdc_pool_count--;
    p->~HDC__();
    new(p) HDC__();
  } else {
    p = new HDC__();
  }
  p->_infreelist = false;
  return p;
}

void SWELL_GDP_CTX_DELETE(HDC__ *hdc)
{
  if (!hdc) return;
  std::lock_guard<std::mutex> lock(g_hdc_pool_mutex);
  if (g_hdc_pool_count < SWELL_MAX_HDC_POOL) {
    hdc->~HDC__();
    new(hdc) HDC__();
    hdc->_infreelist = true;
    hdc->_next = g_hdc_free_list;
    g_hdc_free_list = hdc;
    g_hdc_pool_count++;
  } else {
    delete hdc;
  }
}

HGDIOBJ__ *GDP_OBJECT_NEW()
{
  std::lock_guard<std::mutex> lock(g_gdiobj_pool_mutex);
  HGDIOBJ__ *p = nullptr;
  if (g_gdiobj_free_list) {
    p = g_gdiobj_free_list;
    g_gdiobj_free_list = p->_next;
    g_gdiobj_pool_count--;
  } else {
    p = new HGDIOBJ__();
  }
  memset(p, 0, sizeof(HGDIOBJ__));
  p->_infreelist = false;
  return p;
}

void GDP_OBJECT_DELETE(HGDIOBJ__ *obj)
{
  if (!obj) return;
  {
    std::lock_guard<std::mutex> lock(g_gdiobj_pool_mutex);
    if (!HGDIOBJ_VALID(obj)) return;
    if (g_gdiobj_pool_count < SWELL_MAX_HGDIOBJ_POOL) {
      memset(obj, 0, sizeof(HGDIOBJ__));
      obj->_infreelist = true;
      obj->_next = g_gdiobj_free_list;
      g_gdiobj_free_list = obj;
      g_gdiobj_pool_count++;
      return;
    }
  }
  delete obj;
}

bool HGDIOBJ_VALID(HGDIOBJ__ *p, int reqType)
{
  if (!p) return false;
  if (p->_infreelist) return false;
  if (reinterpret_cast<INT_PTR>(p) < 256) return false; // sentinels
  if (reqType && p->type != reqType) return false;
  return true;
}

bool HDC_VALID(HDC__ *ct)
{
  if (!ct) return false;
  if (ct->_infreelist) return false;
  return true;
}

// ---------------------------------------------------------------------------
// Stock null objects (static singletons — never freed)
// ---------------------------------------------------------------------------

static HGDIOBJ__ g_null_pen_obj  = { TYPE_PEN,   0, 0, -1, 0.0f, nullptr, false, nullptr };
static HGDIOBJ__ g_null_brush_obj = { TYPE_BRUSH, 0, 0, -1, 0.0f, nullptr, false, nullptr };

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void swell_DirtyContext(HDC__ *ctx, int l, int t, int r, int b)
{
  if (!ctx) return;
  if (ctx->dirty_rect_valid) {
    if (l < ctx->dirty_rect.left)   ctx->dirty_rect.left = l;
    if (t < ctx->dirty_rect.top)    ctx->dirty_rect.top = t;
    if (r > ctx->dirty_rect.right)  ctx->dirty_rect.right = r;
    if (b > ctx->dirty_rect.bottom) ctx->dirty_rect.bottom = b;
  } else {
    ctx->dirty_rect.left = l;
    ctx->dirty_rect.top = t;
    ctx->dirty_rect.right = r;
    ctx->dirty_rect.bottom = b;
    ctx->dirty_rect_valid = true;
  }
}

// Check brush validity for fill ops
static inline bool brush_valid(HDC__ *c)
{
  return HGDIOBJ_VALID(c->curbrush, TYPE_BRUSH) && c->curbrush->wid >= 0;
}

// Check pen validity for stroke ops
static inline bool pen_valid(HDC__ *c)
{
  return HGDIOBJ_VALID(c->curpen, TYPE_PEN) && c->curpen->wid >= 0;
}

// ---------------------------------------------------------------------------
// HDC lifecycle
// ---------------------------------------------------------------------------

HDC SWELL_CreateMemContext(HDC hdc, int w, int h)
{
  (void)hdc;
  HDC__ *ctx = SWELL_GDP_CTX_NEW();
  if (!ctx) return nullptr;

  ctx->surface = SkSurfaces::Raster(
    SkImageInfo::Make(w, h, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
    &g_swell_surfprops);
  if (ctx->surface) {
    ctx->canvas = ctx->surface->getCanvas();
    if (ctx->canvas) {
      ctx->canvas->clear(SK_ColorTRANSPARENT);
    }
  } else {
    ctx->canvas = nullptr;
  }

  ctx->surface_offs.x = 0;
  ctx->surface_offs.y = 0;
  ctx->dirty_rect_valid = false;
  ctx->clip_save_count = 0;
  ctx->curpen = nullptr;
  ctx->curbrush = nullptr;
  ctx->curfont = nullptr;
  ctx->cached_font_ptr = nullptr;
  ctx->cached_fm_valid = false;
  ctx->cur_text_color_int = SK_ColorBLACK;
  ctx->curbkmode = TRANSPARENT;
  ctx->curbkcol = 0;
  ctx->lastpos_x = 0.0f;
  ctx->lastpos_y = 0.0f;

  return ctx;
}

void SWELL_DeleteGfxContext(HDC ctx)
{
  if (!ctx || !HDC_VALID(ctx)) return;
  ctx->surface.reset();
  ctx->canvas = nullptr;
  SWELL_GDP_CTX_DELETE(ctx);
}

HDC BeginPaint(HWND hwnd, PAINTSTRUCT *ps)
{
  if (!hwnd || !ps) return nullptr;
  memset(ps, 0, sizeof(PAINTSTRUCT));
  if (!hwnd->m_paintctx) return nullptr;
  HDC__ *ctx = &hwnd->m_paintctx->ctx;
  ps->hdc = ctx;
  ps->rcPaint = hwnd->m_paintctx->clipr;
  ps->fErase = TRUE;
  return ctx;
}

BOOL EndPaint(HWND hwnd, PAINTSTRUCT *ps)
{
  (void)hwnd;
  (void)ps;
  return TRUE;
}

HDC GetDC(HWND hwnd)
{
  if (!hwnd) return nullptr;

  // Walk up to find ancestor with backing store (matching original SWELL_internalGetWindowDC).
  // First: apply NCCALCSIZE on the starting window to get client dimensions.
  int xoffs = 0, yoffs = 0;
  int wndw = hwnd->m_position.right - hwnd->m_position.left;
  int wndh = hwnd->m_position.bottom - hwnd->m_position.top;

  {
    RECT r = { 0, 0, wndw, wndh };
    NCCALCSIZE_PARAMS p = {{{ r }}};
    SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&p);
    wndw = p.rgrc[0].right - p.rgrc[0].left;
    wndh = p.rgrc[0].bottom - p.rgrc[0].top;
    xoffs += p.rgrc[0].left - r.left;
    yoffs += p.rgrc[0].top - r.top;
  }

  // Add hwnd's own position within its parent only if hwnd does NOT
  // own the backing store (i.e. it's a child). For top-level windows
  // m_position is the screen position, not a parent-relative offset.
  if (!hwnd->m_backingstore && !hwnd->m_oswindow) {
    xoffs += hwnd->m_position.left;
    yoffs += hwnd->m_position.top;
  }

  // Walk parent chain — skip hwnd to avoid double-counting NCCALCSIZE.
  HWND h = (HWND)hwnd->m_parent;
  int ltrim = 0, ttrim = 0, rtrim = 0, btrim = 0;

  while (h && !h->m_backingstore && !h->m_oswindow)
  {
    xoffs += h->m_position.left;
    yoffs += h->m_position.top;

    RECT r = h->m_position;
    NCCALCSIZE_PARAMS p = {{{ 0, 0, r.right - r.left, r.bottom - r.top }}};
    SendMessage(h, WM_NCCALCSIZE, FALSE, (LPARAM)&p);
    yoffs += p.rgrc[0].top;
    xoffs += p.rgrc[0].left;

    ltrim = (ltrim > -xoffs) ? ltrim : -xoffs;
    ttrim = (ttrim > -yoffs) ? ttrim : -yoffs;
    rtrim = (rtrim > (xoffs + wndw - h->m_position.right)) ?
            rtrim : (xoffs + wndw - h->m_position.right);
    btrim = (btrim > (yoffs + wndh - h->m_position.bottom)) ?
            btrim : (yoffs + wndh - h->m_position.bottom);

    h = (HWND)h->m_parent;
  }

  // Find the backing store owner (h may be NULL if no match found)
  HWND bsowner = h;
  if (!bsowner) {
    bsowner = hwnd;
    while (bsowner->m_parent) bsowner = (HWND)bsowner->m_parent;
  }

  // Also apply NCCALCSIZE to the backing store owner if different from starting window
  if (bsowner != hwnd && bsowner->m_wndproc)
  {
    RECT r = bsowner->m_position;
    NCCALCSIZE_PARAMS p = {{{ 0, 0, r.right - r.left, r.bottom - r.top }}};
    bsowner->m_wndproc(bsowner, WM_NCCALCSIZE, FALSE, (LPARAM)&p);
    yoffs += p.rgrc[0].top;
    xoffs += p.rgrc[0].left;
  }

  HDC__ *ctx = SWELL_GDP_CTX_NEW();
  if (!ctx) return nullptr;

  if (bsowner && bsowner->m_backingstore)
  {
    ctx->surface = bsowner->m_backingstore;
    ctx->canvas = bsowner->m_backingstore->getCanvas();
  }
  else
  {
    sk_sp<SkSurface> bs = hwnd->m_backingstore;
    ctx->surface = bs;
    ctx->canvas = bs ? bs->getCanvas() : nullptr;
  }

  if (ctx->canvas)
  {
    ctx->clip_save_count = 0;
    ctx->getdc_savecount = ctx->canvas->save();

    // Reset matrix: when GetDC is called from inside a WM_PAINT cycle
    // the canvas already has ancestor translates accumulated. Without
    // reset, our translate(xoffs,yoffs) stacks on top → double offset.
    // xoffs/yoffs are absolute surface-pixel coords, so identity is correct.
    ctx->canvas->resetMatrix();

    // Clip is in surface coords (matrix is identity now).
    SkRect clipr = SkRect::MakeXYWH((float)(xoffs + ltrim), (float)(yoffs + ttrim),
        (float)(wndw - ltrim - rtrim), (float)(wndh - ttrim - btrim));
    if (clipr.width() > 0.0f && clipr.height() > 0.0f)
      ctx->canvas->clipRect(clipr);

    ctx->canvas->translate((float)xoffs, (float)yoffs);
  }

  ctx->surface_offs.x = xoffs;
  ctx->surface_offs.y = yoffs;
  ctx->dirty_rect_valid = false;
  ctx->curpen = nullptr;
  ctx->curbrush = nullptr;
  ctx->curfont = hwnd->m_font;
  ctx->cached_font_ptr = nullptr;
  ctx->cached_fm_valid = false;
  ctx->cur_text_color_int = SK_ColorBLACK;
  ctx->curbkmode = TRANSPARENT;
  ctx->curbkcol = 0;
  ctx->lastpos_x = 0.0f;
  ctx->lastpos_y = 0.0f;

  return ctx;
}

// GetWindowDC: returns HDC for full window rect (including NC area).
// Differs from GetDC which returns client-area only (applies NCCALCSIZE on target).
HDC GetWindowDC(HWND hwnd)
{
  if (!hwnd) return nullptr;

  // Walk up to find ancestor with backing store, but do NOT
  // apply NCCALCSIZE on the starting window — give full window area.
  int xoffs = 0, yoffs = 0;
  int wndw = hwnd->m_position.right - hwnd->m_position.left;
  int wndh = hwnd->m_position.bottom - hwnd->m_position.top;

  // skip NCCALCSIZE on target — full window area, not client only
  int ltrim = 0, ttrim = 0, rtrim = 0, btrim = 0;
  HWND h = hwnd;

  for (;;)
  {
    if (h->m_backingstore || h->m_oswindow || !h->m_parent) break;

    xoffs += h->m_position.left;
    yoffs += h->m_position.top;

    // apply NCCALCSIZE on ancestors only (not the target)
    if (h != hwnd) {
      RECT r2 = h->m_position;
      NCCALCSIZE_PARAMS p = {{{ 0, 0, r2.right - r2.left, r2.bottom - r2.top }}};
      SendMessage(h, WM_NCCALCSIZE, FALSE, (LPARAM)&p);
      yoffs += p.rgrc[0].top;
      xoffs += p.rgrc[0].left;
    }

    ltrim = (ltrim > -xoffs) ? ltrim : -xoffs;
    ttrim = (ttrim > -yoffs) ? ttrim : -yoffs;
    rtrim = (rtrim > (xoffs + wndw - h->m_position.right)) ?
            rtrim : (xoffs + wndw - h->m_position.right);
    btrim = (btrim > (yoffs + wndh - h->m_position.bottom)) ?
            btrim : (yoffs + wndh - h->m_position.bottom);

    h = (HWND)h->m_parent;
  }

  // NCCALCSIZE on backing store owner if different
  if (h != hwnd && h->m_wndproc) {
    RECT r3 = h->m_position;
    NCCALCSIZE_PARAMS p = {{{ 0, 0, r3.right - r3.left, r3.bottom - r3.top }}};
    h->m_wndproc(h, WM_NCCALCSIZE, FALSE, (LPARAM)&p);
    yoffs += p.rgrc[0].top;
    xoffs += p.rgrc[0].left;
  }

  HDC__ *ctx = SWELL_GDP_CTX_NEW();
  if (!ctx) return nullptr;

  if (h && h->m_backingstore) {
    ctx->surface = h->m_backingstore;
    ctx->canvas = h->m_backingstore->getCanvas();
  } else {
    sk_sp<SkSurface> bs = hwnd->m_backingstore;
    ctx->surface = bs;
    ctx->canvas = bs ? bs->getCanvas() : nullptr;
  }

  if (ctx->canvas) {
    ctx->clip_save_count = 0;
    ctx->getdc_savecount = ctx->canvas->save();

    // See GetDC: reset matrix to identity so our absolute-coord translate
    // does not stack on top of any paint-pipeline accumulated translate.
    ctx->canvas->resetMatrix();

    SkRect clipr = SkRect::MakeXYWH((float)(xoffs + ltrim), (float)(yoffs + ttrim),
        (float)(wndw - ltrim - rtrim), (float)(wndh - ttrim - btrim));
    if (clipr.width() > 0.0f && clipr.height() > 0.0f)
      ctx->canvas->clipRect(clipr);

    ctx->canvas->translate((float)xoffs, (float)yoffs);
  }

  ctx->surface_offs.x = xoffs;
  ctx->surface_offs.y = yoffs;
  ctx->dirty_rect_valid = false;
  ctx->curpen = nullptr;
  ctx->curbrush = nullptr;
  ctx->curfont = hwnd->m_font;
  ctx->cached_font_ptr = nullptr;
  ctx->cached_fm_valid = false;
  ctx->cur_text_color_int = SK_ColorBLACK;
  ctx->curbkmode = TRANSPARENT;
  ctx->curbkcol = 0;
  ctx->lastpos_x = 0.0f;
  ctx->lastpos_y = 0.0f;

  return ctx;
}

void ReleaseDC(HWND hwnd, HDC ctx)
{
  if (!ctx || !HDC_VALID(ctx)) return;

  // If not inside a WM_PAINT cycle, blit the dirty region to screen
  if (hwnd && !hwnd->m_paintctx && ctx->dirty_rect_valid)
  {
    RECT r = ctx->dirty_rect;
    r.left   += ctx->surface_offs.x;
    r.top    += ctx->surface_offs.y;
    r.right  += ctx->surface_offs.x;
    r.bottom += ctx->surface_offs.y;

    // Find the window that owns the backing store
    HWND par = hwnd;
    while (par && !par->m_backingstore) par = (HWND)par->m_parent;
    if (par && r.top < r.bottom && r.left < r.right)
      swell_oswindow_updatetoscreen(par, &r);
  }

  // Restore canvas to pre-GetDC state
  if (ctx->canvas && ctx->getdc_savecount > 0)
  {
    ctx->canvas->restoreToCount(ctx->getdc_savecount);
    ctx->getdc_savecount = 0;
    ctx->clip_save_count = 0;
  }

  ctx->surface.reset();
  ctx->canvas = nullptr;
  SWELL_GDP_CTX_DELETE(ctx);
}

// ---------------------------------------------------------------------------
// GDI objects
// ---------------------------------------------------------------------------

HPEN CreatePen(int attr, int wid, int col)
{
  (void)attr;
  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) return nullptr;
  obj->type = TYPE_PEN;
  obj->color = SWELL_TO_SKCOLOR(col, 255);
  obj->wid = wid < 0 ? 0 : wid;
  obj->alpha = 1.0f;
  obj->additional_refcnt = 0;
  return obj;
}

HPEN CreatePenAlpha(int attr, int wid, int col, float alpha)
{
  HPEN p = CreatePen(attr, wid, col);
  if (p) p->alpha = alpha;
  return p;
}

HBRUSH CreateSolidBrush(int col)
{
  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) return nullptr;
  obj->type = TYPE_BRUSH;
  obj->color = SWELL_TO_SKCOLOR(col, 255);
  obj->wid = 0;
  obj->alpha = 1.0f;
  obj->additional_refcnt = 0;
  return obj;
}

HBRUSH CreateSolidBrushAlpha(int col, float alpha)
{
  HBRUSH br = CreateSolidBrush(col);
  if (br) br->alpha = alpha;
  return br;
}

HFONT CreateFont(int lfHeight, int lfWidth, int lfEscapement, int lfOrientation,
                 int lfWeight, char lfItalic, char lfUnderline, char lfStrikeOut,
                 char lfCharSet, char lfOutPrecision, char lfClipPrecision,
                 char lfQuality, char lfPitchAndFamily, const char *lfFaceName)
{
  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) return nullptr;
  obj->type = TYPE_FONT;

  if (lfHeight == 0) lfHeight = -12;
  lfWidth = lfWidth < 2 || lfWidth > 8192 ? 0 : lfWidth;

  LOGFONT *lf = new LOGFONT();
  memset(lf, 0, sizeof(LOGFONT));
  lf->lfHeight = lfHeight;
  lf->lfWidth = lfWidth;
  lf->lfEscapement = lfEscapement;
  lf->lfOrientation = lfOrientation;
  lf->lfWeight = lfWeight;
  lf->lfItalic = lfItalic;
  lf->lfUnderline = lfUnderline;
  lf->lfStrikeOut = lfStrikeOut;
  lf->lfCharSet = lfCharSet;
  lf->lfOutPrecision = lfOutPrecision;
  lf->lfClipPrecision = lfClipPrecision;
  lf->lfQuality = lfQuality;
  lf->lfPitchAndFamily = lfPitchAndFamily;
  if (lfFaceName) {
    strncpy(lf->lfFaceName, lfFaceName, sizeof(lf->lfFaceName) - 1);
    lf->lfFaceName[sizeof(lf->lfFaceName) - 1] = 0;
  }

  obj->typedata = lf;
  obj->additional_refcnt = 0;
  return obj;
}

HFONT CreateFontIndirect(const LOGFONT *lf)
{
  if (!lf) return nullptr;
  return CreateFont(lf->lfHeight, lf->lfWidth, lf->lfEscapement, lf->lfOrientation,
                    lf->lfWeight, lf->lfItalic, lf->lfUnderline, lf->lfStrikeOut,
                    lf->lfCharSet, lf->lfOutPrecision, lf->lfClipPrecision,
                    lf->lfQuality, lf->lfPitchAndFamily,
                    lf->lfFaceName[0] ? lf->lfFaceName : nullptr);
}

HBITMAP CreateBitmap(int width, int height, int numplanes, int bitsperpixel,
                     unsigned char *bits)
{
  (void)numplanes;
  if (width <= 0 || height <= 0) return nullptr;
  if (!bits || bitsperpixel != 32) return nullptr;

  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) return nullptr;
  obj->type = TYPE_BITMAP;

  SkBitmap *bm = new SkBitmap();
  if (!bm->tryAllocN32Pixels(width, height)) {
    delete bm;
    GDP_OBJECT_DELETE(obj);
    return nullptr;
  }
  if (bits && bitsperpixel == 32) {
    memcpy(bm->getPixels(), bits, width * height * 4);
  }

  obj->typedata = bm;
  obj->additional_refcnt = 0;
  return obj;
}

HICON CreateIconIndirect(const ICONINFO *iconinfo)
{
  if (!iconinfo) return nullptr;

  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) return nullptr;
  obj->type = TYPE_BITMAP;

  int w = 0, h = 0;
  // Try to get dimensions from the color bitmap
  if (iconinfo->hbmColor && HGDIOBJ_VALID(iconinfo->hbmColor, TYPE_BITMAP)) {
    SkBitmap *src = static_cast<SkBitmap *>(iconinfo->hbmColor->typedata);
    if (src) { w = src->width(); h = src->height(); }
  }
  // Fall back to mask bitmap
  if ((w <= 0 || h <= 0) && iconinfo->hbmMask &&
      HGDIOBJ_VALID(iconinfo->hbmMask, TYPE_BITMAP)) {
    SkBitmap *src = static_cast<SkBitmap *>(iconinfo->hbmMask->typedata);
    if (src) { w = src->width(); h = src->height(); }
  }
  if (w <= 0) w = 32;
  if (h <= 0) h = 32;

  SkBitmap *bm = new SkBitmap();
  if (!bm->tryAllocN32Pixels(w, h)) {
    delete bm;
    GDP_OBJECT_DELETE(obj);
    return nullptr;
  }

  // Copy source pixel data from hbmColor if available
  SkBitmap *srcBm = nullptr;
  if (iconinfo->hbmColor && HGDIOBJ_VALID(iconinfo->hbmColor, TYPE_BITMAP))
    srcBm = static_cast<SkBitmap *>(iconinfo->hbmColor->typedata);
  if (srcBm && srcBm->getPixels()) {
    SkCanvas canvas(*bm);
    canvas.drawImage(srcBm->asImage(), 0, 0);
  }

  obj->typedata = bm;
  obj->additional_refcnt = 0;
  return obj;
}

HICON LoadNamedImage(const char *name, bool alphaFromMask)
{
  (void)alphaFromMask;
  if (!name || !name[0]) return nullptr;

  sk_sp<SkData> data = SkData::MakeFromFileName(name);
  if (!data || data->size() == 0) return nullptr;

  // Try decoders in order of popularity for REAPER assets
  using DecodeFn = std::unique_ptr<SkCodec> (*)(sk_sp<const SkData>,
                                                 SkCodec::Result *,
                                                 SkCodecs::DecodeContext);
  struct { bool (*is)(const void *, size_t); DecodeFn dec; } decoders[] = {
    { SkPngDecoder::IsPng,   SkPngDecoder::Decode  },
    { SkJpegDecoder::IsJpeg, SkJpegDecoder::Decode },
    { SkBmpDecoder::IsBmp,   SkBmpDecoder::Decode  },
    { SkWebpDecoder::IsWebp, SkWebpDecoder::Decode },
    { SkGifDecoder::IsGif,   SkGifDecoder::Decode  },
  };

  std::unique_ptr<SkCodec> codec;
  for (auto &d : decoders) {
    if (d.is(data->data(), data->size())) {
      SkCodec::Result r;
      codec = d.dec(sk_sp<const SkData>(data), &r, nullptr);
      if (codec && r == SkCodec::kSuccess) break;
      codec.reset();
    }
  }
  if (!codec) return nullptr;

  SkImageInfo info = codec->getInfo()
                          .makeColorType(kBGRA_8888_SkColorType)
                          .makeAlphaType(kPremul_SkAlphaType);
  SkBitmap *bm = new SkBitmap();
  if (!bm->tryAllocPixels(info)) { delete bm; return nullptr; }

  SkCodec::Result res = codec->getPixels(bm->info(), bm->getPixels(), bm->rowBytes());
  if (res != SkCodec::kSuccess && res != SkCodec::kIncompleteInput) {
    delete bm;
    return nullptr;
  }

  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) { delete bm; return nullptr; }
  obj->type     = TYPE_BITMAP;
  obj->typedata = bm;
  return (HICON)obj;
}

HGDIOBJ SelectObject(HDC ctx, HGDIOBJ pen)
{
  if (!ctx || !HDC_VALID(ctx)) return nullptr;

  if (!pen) return nullptr;

  // Sentinel handling: restore "nothing" for a slot
  if (reinterpret_cast<INT_PTR>(pen) == TYPE_PEN ||
      reinterpret_cast<INT_PTR>(pen) == TYPE_BRUSH ||
      reinterpret_cast<INT_PTR>(pen) == TYPE_FONT) {
    int sentinelType = static_cast<int>(reinterpret_cast<INT_PTR>(pen));
    HGDIOBJ__ **slot = nullptr;
    if (sentinelType == TYPE_PEN)       slot = &ctx->curpen;
    else if (sentinelType == TYPE_BRUSH) slot = &ctx->curbrush;
    else if (sentinelType == TYPE_FONT)  slot = &ctx->curfont;
    if (slot) {
      HGDIOBJ old = *slot;
      *slot = nullptr;
      return HGDIOBJ_VALID(old, sentinelType) ? old : GDI_NULL_SENTINEL(sentinelType);
    }
    return nullptr;
  }

  if (!HGDIOBJ_VALID(pen)) return nullptr;

  int t = pen->type;
  HGDIOBJ__ **slot = nullptr;

  if (t == TYPE_PEN) {
    slot = &ctx->curpen;
  } else if (t == TYPE_BRUSH) {
    slot = &ctx->curbrush;
  } else if (t == TYPE_FONT) {
    slot = &ctx->curfont;
  } else {
    // TYPE_BITMAP — not selectable
    return nullptr;
  }

  HGDIOBJ old = *slot;
  *slot = pen;
  return HGDIOBJ_VALID(old, t) ? old : GDI_NULL_SENTINEL(t);
}

void DeleteObject(HGDIOBJ obj)
{
  if (!obj) return;

  // Never delete stock objects (null pen/brush)
  if (obj == &g_null_pen_obj || obj == &g_null_brush_obj) return;

  // Never delete sentinel values
  if (reinterpret_cast<INT_PTR>(obj) < 256) return;

  if (!HGDIOBJ_VALID(obj)) return;

  {
    std::lock_guard<std::mutex> lock(g_hdc_pool_mutex);
    obj->additional_refcnt--;
    if (obj->additional_refcnt < 0) {
      // Free typedata
      if (obj->type == TYPE_FONT && obj->typedata) {
        delete static_cast<LOGFONT *>(obj->typedata);
        obj->typedata = nullptr;
      } else if (obj->type == TYPE_BITMAP && obj->typedata) {
        delete static_cast<SkBitmap *>(obj->typedata);
        obj->typedata = nullptr;
      }
      GDP_OBJECT_DELETE(obj);
    }
  }
}

HGDIOBJ GetStockObject(int wh)
{
  if (wh == NULL_PEN)  return &g_null_pen_obj;
  if (wh == NULL_BRUSH) return &g_null_brush_obj;
  return nullptr;
}

HGDIOBJ SWELL_CloneGDIObject(HGDIOBJ a)
{
  if (!a || !HGDIOBJ_VALID(a)) return nullptr;
  {
    std::lock_guard<std::mutex> lock(g_hdc_pool_mutex);
    a->additional_refcnt++;
  }
  return a;
}

BOOL GetObject(HICON icon, int bmsz, void *_bm)
{
  if (!icon || !_bm || bmsz < (int)sizeof(BITMAP)) return FALSE;
  if (!HGDIOBJ_VALID(icon, TYPE_BITMAP)) return FALSE;

  SkBitmap *bm = static_cast<SkBitmap *>(icon->typedata);
  BITMAP *out = static_cast<BITMAP *>(_bm);

  if (bm) {
    out->bmWidth = bm->width();
    out->bmHeight = bm->height();
    out->bmWidthBytes = bm->rowBytes();
    out->bmPlanes = 1;
    out->bmBitsPixel = 32;
    out->bmBits = bm->getPixels();
  } else {
    memset(out, 0, sizeof(BITMAP));
  }

  return TRUE;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Rectangle(HDC ctx, int l, int t, int r, int b)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, l, t, r, b);

  SkRect rect = SkRect::MakeLTRB((float)l, (float)t, (float)r, (float)b);

  if (brush_valid(ctx)) {
    SkPaint fillPaint;
    fillPaint.setStyle(SkPaint::kFill_Style);
    fillPaint.setColor(ctx->curbrush->color);
    fillPaint.setAlphaf(ctx->curbrush->alpha);
    ctx->canvas->drawRect(rect, fillPaint);
  }

  if (pen_valid(ctx)) {
    SkPaint strokePaint;
    strokePaint.setStyle(SkPaint::kStroke_Style);
    strokePaint.setColor(ctx->curpen->color);
    strokePaint.setAlphaf(ctx->curpen->alpha);
    float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
    strokePaint.setStrokeWidth(sw);
    strokePaint.setAntiAlias(true);
    float hsw = sw * 0.5f;
    SkRect strokeRect = SkRect::MakeLTRB((float)l + hsw, (float)t + hsw,
                                         (float)r - hsw, (float)b - hsw);
    ctx->canvas->drawRect(strokeRect, strokePaint);
  }
}

void Ellipse(HDC ctx, int l, int t, int r, int b)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, l, t, r, b);

  SkRect rect = SkRect::MakeLTRB((float)l, (float)t, (float)r, (float)b);

  if (brush_valid(ctx)) {
    SkPaint fillPaint;
    fillPaint.setStyle(SkPaint::kFill_Style);
    fillPaint.setColor(ctx->curbrush->color);
    fillPaint.setAlphaf(ctx->curbrush->alpha);
    fillPaint.setAntiAlias(true);
    ctx->canvas->drawOval(rect, fillPaint);
  }

  if (pen_valid(ctx)) {
    SkPaint strokePaint;
    strokePaint.setStyle(SkPaint::kStroke_Style);
    strokePaint.setColor(ctx->curpen->color);
    strokePaint.setAlphaf(ctx->curpen->alpha);
    float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
    strokePaint.setStrokeWidth(sw);
    strokePaint.setAntiAlias(true);
    float hsw = sw * 0.5f;
    SkRect ovalRect = SkRect::MakeLTRB((float)l + hsw, (float)t + hsw,
                                       (float)r - hsw, (float)b - hsw);
    ctx->canvas->drawOval(ovalRect, strokePaint);
  }
}

void SWELL_DrawArc(HDC ctx, int l, int t, int r, int b,
                   float start_deg, float sweep_deg)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!pen_valid(ctx)) return;
  swell_DirtyContext(ctx, l, t, r, b);

  SkPaint strokePaint;
  strokePaint.setStyle(SkPaint::kStroke_Style);
  strokePaint.setColor(ctx->curpen->color);
  strokePaint.setAlphaf(ctx->curpen->alpha);
  float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
  strokePaint.setStrokeWidth(sw);
  strokePaint.setAntiAlias(true);

  float hsw = sw * 0.5f;
  SkRect oval = SkRect::MakeLTRB((float)l + hsw, (float)t + hsw,
                                 (float)r - hsw, (float)b - hsw);
  ctx->canvas->drawArc(oval, start_deg, sweep_deg, false, strokePaint);

  const float pi = 3.14159265358979323846f;
  float end_rad = (start_deg + sweep_deg) * pi / 180.0f;
  ctx->lastpos_x = oval.centerX() + cosf(end_rad) * oval.width() * 0.5f;
  ctx->lastpos_y = oval.centerY() + sinf(end_rad) * oval.height() * 0.5f;
}

void Arc(HDC ctx, int l, int t, int r, int b,
         int xstart, int ystart, int xend, int yend)
{
  if (r <= l || b <= t) return;
  const float cx = ((float)l + (float)r) * 0.5f;
  const float cy = ((float)t + (float)b) * 0.5f;
  const float pi = 3.14159265358979323846f;
  float start = atan2f((float)ystart - cy, (float)xstart - cx) * 180.0f / pi;
  float end = atan2f((float)yend - cy, (float)xend - cx) * 180.0f / pi;
  float sweep = end - start;
  while (sweep <= 0.0f) sweep += 360.0f;
  SWELL_DrawArc(ctx, l, t, r, b, start, sweep);
}

static void swell_clamp_round_rect_pair(float &a, float &b, float lim)
{
  float s = a + b;
  if (s > lim && s > 0.0f) {
    float k = lim / s;
    a *= k;
    b *= k;
  }
}

static void swell_draw_rrect_fill(HDC ctx, const SkRRect &rr,
                                  SkColor color, float alpha)
{
  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(color);
  paint.setAlphaf(alpha);
  paint.setAntiAlias(true);
  ctx->canvas->drawRRect(rr, paint);
}

static void swell_draw_rrect_border(HDC ctx, const SkRRect &outer,
                                    const SkRRect *inner,
                                    SkColor color, float alpha)
{
  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(color);
  paint.setAlphaf(alpha);
  paint.setAntiAlias(true);

  if (inner) {
    SkPath path;
    path.setFillType(SkPathFillType::kEvenOdd);
    path.addRRect(outer);
    path.addRRect(*inner);
    ctx->canvas->drawPath(path, paint);
  } else {
    ctx->canvas->drawRRect(outer, paint);
  }
}

void RoundRect(HDC ctx, int x, int y, int x2, int y2, int xrnd, int yrnd)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  const float w = (float)(x2 - x);
  const float h = (float)(y2 - y);
  if (w <= 0.0f || h <= 0.0f) return;
  swell_DirtyContext(ctx, x, y, x2, y2);

  // Win32 RoundRect receives the corner ellipse dimensions, not radii.
  // Skia wants radii, so halve the values before drawing.
  float rx = (float)(xrnd > 0 ? xrnd : 0) * 0.5f;
  float ry = (float)(yrnd > 0 ? yrnd : 0) * 0.5f;
  if (rx > w * 0.5f) rx = w * 0.5f;
  if (ry > h * 0.5f) ry = h * 0.5f;

  SkRect rect = SkRect::MakeLTRB((float)x, (float)y, (float)x2, (float)y2);
  SkRRect rr = SkRRect::MakeRectXY(rect, rx, ry);

  if (pen_valid(ctx) && brush_valid(ctx)) {
    float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
    SkRRect innerRR;
    SkRect inner = SkRect::MakeLTRB((float)x + sw, (float)y + sw,
                                    (float)x2 - sw, (float)y2 - sw);

    swell_draw_rrect_fill(ctx, rr, ctx->curpen->color, ctx->curpen->alpha);

    if (inner.width() > 0.0f && inner.height() > 0.0f) {
      float irx = rx - sw;
      float iry = ry - sw;
      if (irx < 0.0f) irx = 0.0f;
      if (iry < 0.0f) iry = 0.0f;
      if (irx > inner.width() * 0.5f) irx = inner.width() * 0.5f;
      if (iry > inner.height() * 0.5f) iry = inner.height() * 0.5f;
      innerRR = SkRRect::MakeRectXY(inner, irx, iry);
      swell_draw_rrect_fill(ctx, innerRR,
                            ctx->curbrush->color, ctx->curbrush->alpha);
    }
  } else if (brush_valid(ctx)) {
    swell_draw_rrect_fill(ctx, rr, ctx->curbrush->color, ctx->curbrush->alpha);
  } else if (pen_valid(ctx)) {
    float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
    SkRRect innerRR;
    SkRRect *innerPtr = nullptr;
    SkRect inner = SkRect::MakeLTRB((float)x + sw, (float)y + sw,
                                    (float)x2 - sw, (float)y2 - sw);
    if (inner.width() > 0.0f && inner.height() > 0.0f) {
      float irx = rx - sw;
      float iry = ry - sw;
      if (irx < 0.0f) irx = 0.0f;
      if (iry < 0.0f) iry = 0.0f;
      if (irx > inner.width() * 0.5f) irx = inner.width() * 0.5f;
      if (iry > inner.height() * 0.5f) iry = inner.height() * 0.5f;
      innerRR = SkRRect::MakeRectXY(inner, irx, iry);
      innerPtr = &innerRR;
    }
    swell_draw_rrect_border(ctx, rr, innerPtr,
                            ctx->curpen->color, ctx->curpen->alpha);
  }
}

void SWELL_DrawRoundRectEx(HDC ctx, int x, int y, int x2, int y2,
                           int rTL, int rTR, int rBR, int rBL)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, x, y, x2, y2);

  // Clamp per-corner radii so opposing pairs never exceed available extent.
  float w = (float)(x2 - x), h = (float)(y2 - y);
  if (w <= 0.0f || h <= 0.0f) return;
  float fTL = (float)(rTL < 0 ? 0 : rTL);
  float fTR = (float)(rTR < 0 ? 0 : rTR);
  float fBR = (float)(rBR < 0 ? 0 : rBR);
  float fBL = (float)(rBL < 0 ? 0 : rBL);
  swell_clamp_round_rect_pair(fTL, fTR, w);
  swell_clamp_round_rect_pair(fBL, fBR, w);
  swell_clamp_round_rect_pair(fTL, fBL, h);
  swell_clamp_round_rect_pair(fTR, fBR, h);

  SkVector radii[4] = {
    { fTL, fTL }, { fTR, fTR }, { fBR, fBR }, { fBL, fBL }
  };
  SkRect rect = SkRect::MakeLTRB((float)x, (float)y, (float)x2, (float)y2);
  SkRRect rr;
  rr.setRectRadii(rect, radii);

  if (pen_valid(ctx) && brush_valid(ctx)) {
    float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
    SkRRect innerRR;
    SkRect sr = SkRect::MakeLTRB((float)x + sw, (float)y + sw,
                                 (float)x2 - sw, (float)y2 - sw);

    swell_draw_rrect_fill(ctx, rr, ctx->curpen->color, ctx->curpen->alpha);

    if (sr.width() > 0.0f && sr.height() > 0.0f) {
      float sTL = fTL - sw, sTR = fTR - sw;
      float sBR = fBR - sw, sBL = fBL - sw;
      if (sTL < 0.0f) sTL = 0.0f; if (sTR < 0.0f) sTR = 0.0f;
      if (sBR < 0.0f) sBR = 0.0f; if (sBL < 0.0f) sBL = 0.0f;
      swell_clamp_round_rect_pair(sTL, sTR, sr.width());
      swell_clamp_round_rect_pair(sBL, sBR, sr.width());
      swell_clamp_round_rect_pair(sTL, sBL, sr.height());
      swell_clamp_round_rect_pair(sTR, sBR, sr.height());
      SkVector sradii[4] = {
        { sTL, sTL }, { sTR, sTR }, { sBR, sBR }, { sBL, sBL }
      };
      innerRR.setRectRadii(sr, sradii);
      swell_draw_rrect_fill(ctx, innerRR,
                            ctx->curbrush->color, ctx->curbrush->alpha);
    }
  } else if (brush_valid(ctx)) {
    swell_draw_rrect_fill(ctx, rr, ctx->curbrush->color, ctx->curbrush->alpha);
  } else if (pen_valid(ctx)) {
    float sw = ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f;
    SkRRect innerRR;
    SkRRect *innerPtr = nullptr;
    SkRect sr = SkRect::MakeLTRB((float)x + sw, (float)y + sw,
                                 (float)x2 - sw, (float)y2 - sw);
    if (sr.width() > 0.0f && sr.height() > 0.0f) {
      float sTL = fTL - sw, sTR = fTR - sw;
      float sBR = fBR - sw, sBL = fBL - sw;
      if (sTL < 0.0f) sTL = 0.0f; if (sTR < 0.0f) sTR = 0.0f;
      if (sBR < 0.0f) sBR = 0.0f; if (sBL < 0.0f) sBL = 0.0f;
      swell_clamp_round_rect_pair(sTL, sTR, sr.width());
      swell_clamp_round_rect_pair(sBL, sBR, sr.width());
      swell_clamp_round_rect_pair(sTL, sBL, sr.height());
      swell_clamp_round_rect_pair(sTR, sBR, sr.height());
      SkVector sradii[4] = {
        { sTL, sTL }, { sTR, sTR }, { sBR, sBR }, { sBL, sBL }
      };
      innerRR.setRectRadii(sr, sradii);
      innerPtr = &innerRR;
    }
    swell_draw_rrect_border(ctx, rr, innerPtr,
                            ctx->curpen->color, ctx->curpen->alpha);
  }
}

void SWELL_FillRect(HDC ctx, const RECT *r, HBRUSH br)
{
  if (!HDC_VALID(ctx) || !ctx->canvas || !r) return;

  HGDIOBJ__ *useBrush = nullptr;
  if (br && HGDIOBJ_VALID(br, TYPE_BRUSH) && br->wid >= 0)
    useBrush = br;
  else if (brush_valid(ctx))
    useBrush = ctx->curbrush;
  else
    return;

  swell_DirtyContext(ctx, r->left, r->top, r->right, r->bottom);

  SkRect rect = SkRect::MakeLTRB((float)r->left, (float)r->top,
                                 (float)r->right, (float)r->bottom);

  SkPaint fillPaint;
  fillPaint.setStyle(SkPaint::kFill_Style);
  fillPaint.setColor(useBrush->color);
  fillPaint.setAlphaf(useBrush->alpha);
  ctx->canvas->drawRect(rect, fillPaint);
}

void SWELL_Polygon(HDC ctx, POINT *pts, int npts)
{
  if (!HDC_VALID(ctx) || !ctx->canvas || !pts || npts < 2) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;

  // Compute bounding box of points for dirty rect
  int l = pts[0].x, t = pts[0].y, r2 = pts[0].x, b2 = pts[0].y;
  for (int i = 1; i < npts; i++) {
    if (pts[i].x < l) l = pts[i].x;
    if (pts[i].y < t) t = pts[i].y;
    if (pts[i].x > r2) r2 = pts[i].x;
    if (pts[i].y > b2) b2 = pts[i].y;
  }
  swell_DirtyContext(ctx, l, t, r2, b2);

  SkPath path;
  path.moveTo((float)pts[0].x, (float)pts[0].y);
  for (int i = 1; i < npts; i++)
    path.lineTo((float)pts[i].x, (float)pts[i].y);
  path.close();

  if (brush_valid(ctx)) {
    SkPaint fillPaint;
    fillPaint.setStyle(SkPaint::kFill_Style);
    fillPaint.setColor(ctx->curbrush->color);
    fillPaint.setAlphaf(ctx->curbrush->alpha);
    fillPaint.setAntiAlias(true);
    ctx->canvas->drawPath(path, fillPaint);
  }

  if (pen_valid(ctx)) {
    SkPaint strokePaint;
    strokePaint.setStyle(SkPaint::kStroke_Style);
    strokePaint.setColor(ctx->curpen->color);
    strokePaint.setAlphaf(ctx->curpen->alpha);
    strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
    strokePaint.setAntiAlias(true);
    ctx->canvas->drawPath(path, strokePaint);
  }
}

void MoveToEx(HDC ctx, int x, int y, POINT *op)
{
  if (!HDC_VALID(ctx)) return;
  if (op) {
    op->x = (LONG)ctx->lastpos_x;
    op->y = (LONG)ctx->lastpos_y;
  }
  ctx->lastpos_x = (float)x;
  ctx->lastpos_y = (float)y;
}

void LineTo(HDC ctx, int x, int y)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!pen_valid(ctx)) {
    ctx->lastpos_x = (float)x;
    ctx->lastpos_y = (float)y;
    return;
  }

  int l = (int)ctx->lastpos_x, t = (int)ctx->lastpos_y;
  int r2 = x, b2 = y;
  if (l > r2) { int tmp = l; l = r2; r2 = tmp; }
  if (t > b2) { int tmp = t; t = b2; b2 = tmp; }
  int sw = ctx->curpen->wid > 0 ? (int)(ctx->curpen->wid * 0.5f + 1.5f) : 1;
  swell_DirtyContext(ctx, l - sw, t - sw, r2 + sw, b2 + sw);

  SkPaint strokePaint;
  strokePaint.setStyle(SkPaint::kStroke_Style);
  strokePaint.setColor(ctx->curpen->color);
  strokePaint.setAlphaf(ctx->curpen->alpha);
  strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
  strokePaint.setAntiAlias(true);
  ctx->canvas->drawLine(ctx->lastpos_x, ctx->lastpos_y, (float)x, (float)y, strokePaint);

  ctx->lastpos_x = (float)x;
  ctx->lastpos_y = (float)y;
}

void SetPixel(HDC ctx, int x, int y, int c)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  swell_DirtyContext(ctx, x, y, x + 1, y + 1);

  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(SWELL_TO_SKCOLOR(c, 255));
  ctx->canvas->drawPoint((float)x + 0.5f, (float)y + 0.5f, paint);
}

void PolyBezierTo(HDC ctx, POINT *pts, int np)
{
  if (!HDC_VALID(ctx) || !ctx->canvas || !pts || np < 3) return;
  if (!pen_valid(ctx)) {
    ctx->lastpos_x = (float)pts[np - 1].x;
    ctx->lastpos_y = (float)pts[np - 1].y;
    return;
  }

  int l = (int)ctx->lastpos_x, t = (int)ctx->lastpos_y;
  int r2 = l, b2 = t;
  for (int i = 0; i < np; i++) {
    if (pts[i].x < l) l = pts[i].x;
    if (pts[i].y < t) t = pts[i].y;
    if (pts[i].x > r2) r2 = pts[i].x;
    if (pts[i].y > b2) b2 = pts[i].y;
  }
  int sw = ctx->curpen->wid > 0 ? (int)(ctx->curpen->wid * 0.5f + 1.5f) : 1;
  swell_DirtyContext(ctx, l - sw, t - sw, r2 + sw, b2 + sw);

  SkPath path;
  path.moveTo(ctx->lastpos_x, ctx->lastpos_y);
  for (int i = 0; i + 2 < np; i += 3) {
    path.cubicTo((float)pts[i].x, (float)pts[i].y,
                 (float)pts[i + 1].x, (float)pts[i + 1].y,
                 (float)pts[i + 2].x, (float)pts[i + 2].y);
  }

  SkPaint strokePaint;
  strokePaint.setStyle(SkPaint::kStroke_Style);
  strokePaint.setColor(ctx->curpen->color);
  strokePaint.setAlphaf(ctx->curpen->alpha);
  strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
  strokePaint.setAntiAlias(true);
  ctx->canvas->drawPath(path, strokePaint);

  ctx->lastpos_x = (float)pts[np - 1].x;
  ctx->lastpos_y = (float)pts[np - 1].y;
}

void PolyPolyline(HDC ctx, const POINT *pts, const DWORD *cnts, int nseg)
{
  if (!HDC_VALID(ctx) || !ctx->canvas || !pts || !cnts || nseg < 1) return;
  if (!pen_valid(ctx)) return;

  const POINT *p = pts;
  int l = 0, t = 0, r2 = 0, b2 = 0;
  bool first = true;
  SkPath path;

  for (int i = 0; i < nseg; i++) {
    DWORD n = cnts[i];
    if (n < 1) { p += n; continue; }

    path.moveTo((float)p[0].x, (float)p[0].y);
    for (DWORD j = 1; j < n; j++) {
      path.lineTo((float)p[j].x, (float)p[j].y);
    }

    for (DWORD j = 0; j < n; j++) {
      if (first) {
        l = r2 = p[j].x;
        t = b2 = p[j].y;
        first = false;
      } else {
        if (p[j].x < l) l = p[j].x;
        if (p[j].y < t) t = p[j].y;
        if (p[j].x > r2) r2 = p[j].x;
        if (p[j].y > b2) b2 = p[j].y;
      }
    }
    p += n;
  }

  if (!first) {
    swell_DirtyContext(ctx, l, t, r2, b2);

    SkPaint strokePaint;
    strokePaint.setStyle(SkPaint::kStroke_Style);
    strokePaint.setColor(ctx->curpen->color);
    strokePaint.setAlphaf(ctx->curpen->alpha);
    strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
    strokePaint.setAntiAlias(true);
    ctx->canvas->drawPath(path, strokePaint);
  }
}

// ---------------------------------------------------------------------------
// Blit
// ---------------------------------------------------------------------------

void BitBlt(HDC hdcOut, int x, int y, int w, int h,
            HDC hdcIn, int xin, int yin, int mode)
{
  if (!HDC_VALID(hdcOut) || !hdcOut->canvas || !HDC_VALID(hdcIn) || !hdcIn->canvas) return;
  if (w <= 0 || h <= 0) return;

  swell_DirtyContext(hdcOut, x, y, x + w, y + h);

  SkSurface *srcSurf = hdcIn->canvas->getSurface();
  if (!srcSurf) return;

  int sx = xin + hdcIn->surface_offs.x;
  int sy = yin + hdcIn->surface_offs.y;
  sk_sp<SkImage> img = srcSurf->makeImageSnapshot();
  if (!img) return;

  SkRect srcRect = SkRect::MakeXYWH((float)sx, (float)sy, (float)w, (float)h);
  SkRect dstRect = SkRect::MakeXYWH((float)x, (float)y, (float)w, (float)h);

  SkPaint paint;
  if (mode == SRCCOPY) {
    paint.setBlendMode(SkBlendMode::kSrc);
  } else if (mode == (int)SRCCOPY_USEALPHACHAN) {
    paint.setBlendMode(SkBlendMode::kSrcOver);
  }

  hdcOut->canvas->drawImageRect(img, srcRect, dstRect,
                                SkSamplingOptions(), &paint,
                                SkCanvas::kStrict_SrcRectConstraint);
}

void StretchBlt(HDC hdcOut, int x, int y, int w, int h,
                HDC hdcIn, int xin, int yin, int srcw, int srch, int mode)
{
  if (!HDC_VALID(hdcOut) || !hdcOut->canvas || !HDC_VALID(hdcIn) || !hdcIn->canvas) return;
  if (w <= 0 || h <= 0 || srcw <= 0 || srch <= 0) return;

  swell_DirtyContext(hdcOut, x, y, x + w, y + h);

  SkSurface *srcSurf = hdcIn->canvas->getSurface();
  if (!srcSurf) return;

  int sx = xin + hdcIn->surface_offs.x;
  int sy = yin + hdcIn->surface_offs.y;
  sk_sp<SkImage> img = srcSurf->makeImageSnapshot();
  if (!img) return;

  SkRect srcRect = SkRect::MakeXYWH((float)sx, (float)sy,
                                    (float)srcw, (float)srch);
  SkRect dstRect = SkRect::MakeXYWH((float)x, (float)y, (float)w, (float)h);

  SkPaint paint;
  if (mode == SRCCOPY) {
    paint.setBlendMode(SkBlendMode::kSrc);
  } else if (mode == (int)SRCCOPY_USEALPHACHAN) {
    paint.setBlendMode(SkBlendMode::kSrcOver);
  }

  hdcOut->canvas->drawImageRect(img, srcRect, dstRect,
                                SkSamplingOptions(), &paint,
                                SkCanvas::kStrict_SrcRectConstraint);
}

#ifndef SWELL_TARGET_OSX
void StretchBltFromMem(HDC hdcOut, int x, int y, int w, int h,
                       const void *bits, int srcw, int srch, int srcspan)
{
  if (!HDC_VALID(hdcOut) || !hdcOut->canvas || !bits) return;
  if (w <= 0 || h <= 0 || srcw <= 0 || srch <= 0) return;

  swell_DirtyContext(hdcOut, x, y, x + w, y + h);

  SkImageInfo info = SkImageInfo::Make(srcw, srch,
      kBGRA_8888_SkColorType, kPremul_SkAlphaType);
  SkPixmap pixmap(info, bits, (size_t)srcspan);
  sk_sp<SkImage> img = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
  if (!img) return;

  SkRect dstRect = SkRect::MakeXYWH((float)x, (float)y, (float)w, (float)h);

  SkPaint cpPaint;
  cpPaint.setBlendMode(SkBlendMode::kSrc);
  hdcOut->canvas->drawImageRect(img, dstRect, SkSamplingOptions(), &cpPaint);
}

int SWELL_GetScaling256(void)
{
  return g_swell_ui_scale;
}
#endif

void DrawImageInRect(HDC ctx, HICON img, const RECT *r)
{
  if (!HDC_VALID(ctx) || !ctx->canvas || !img || !r) return;
  if (!HGDIOBJ_VALID(img, TYPE_BITMAP)) return;

  swell_DirtyContext(ctx, r->left, r->top, r->right, r->bottom);

  SkBitmap *bm = static_cast<SkBitmap *>(img->typedata);
  if (!bm) return;

  sk_sp<SkImage> image = SkImages::RasterFromBitmap(*bm);
  if (!image) return;

  SkRect dstRect = SkRect::MakeLTRB((float)r->left, (float)r->top,
                                    (float)r->right, (float)r->bottom);
  ctx->canvas->drawImageRect(image, dstRect, SkSamplingOptions());
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void SetTextColor(HDC ctx, int col)
{
  if (!HDC_VALID(ctx)) return;
  ctx->cur_text_color_int = SWELL_TO_SKCOLOR(col, 255);
}

int GetTextColor(HDC ctx)
{
  if (!HDC_VALID(ctx)) return 0;
  int sk = ctx->cur_text_color_int;
  return RGB(SkColorGetR(sk), SkColorGetG(sk), SkColorGetB(sk));
}

void SetBkColor(HDC ctx, int col)
{
  if (!HDC_VALID(ctx)) return;
  ctx->curbkcol = SWELL_TO_SKCOLOR(col, 255);
}

void SetBkMode(HDC ctx, int col)
{
  if (!HDC_VALID(ctx)) return;
  ctx->curbkmode = col;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static sk_sp<SkTypeface> swell_get_typeface(const char *family, int weight, bool italic)
{
  static sk_sp<SkFontMgr> s_fontmgr;
  static std::unordered_map<std::string, sk_sp<SkTypeface>> s_cache;
  static std::mutex s_cache_mutex;
  static std::once_flag s_fontmgr_once;
  std::call_once(s_fontmgr_once, []() {
    s_fontmgr = SkFontMgr_New_FontConfig(nullptr,
        SkFontScanner_Make_FreeType());
  });
  if (!s_fontmgr || !family || !family[0]) return nullptr;

  char key[128];
  snprintf(key, sizeof(key), "%s:%d:%d", family, weight, italic ? 1 : 0);

  {
    std::lock_guard<std::mutex> lock(s_cache_mutex);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return it->second;
  }

  SkFontStyle style(weight, SkFontStyle::kNormal_Width,
      italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant);
  sk_sp<SkTypeface> tf = s_fontmgr->matchFamilyStyle(family, style);
  if (!tf) tf = s_fontmgr->legacyMakeTypeface(family, style);

  // Reject placeholder typefaces with no glyph data. legacyMakeTypeface (and
  // some matchFamilyStyle implementations) can return a non-null typeface
  // that has zero glyphs / empty family name when the requested face is not
  // available — using it makes measureText return 0 and DT_CALCRECT collapse
  // tooltip / control widths to 0. Returning null here causes the caller to
  // fall through to the swell default font (Arial).
  if (tf && tf->countGlyphs() <= 0) tf.reset();

  if (tf) {
    std::lock_guard<std::mutex> lock(s_cache_mutex);
    s_cache[key] = tf;
  }
  return tf;
}

static unsigned int swell_cp1252_to_unicode(unsigned char c)
{
  static const unsigned int cp1252_80_9f[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
  };
  if (c >= 0x80 && c <= 0x9f) return cp1252_80_9f[c - 0x80];
  return c;
}

static void swell_append_utf8_codepoint(WDL_FastString &out, unsigned int cp)
{
  char tmp[4];
  int len = 0;
  if (cp <= 0x7f) {
    tmp[len++] = (char)cp;
  } else if (cp <= 0x7ff) {
    tmp[len++] = (char)(0xc0 | (cp >> 6));
    tmp[len++] = (char)(0x80 | (cp & 0x3f));
  } else if (cp <= 0xffff) {
    tmp[len++] = (char)(0xe0 | (cp >> 12));
    tmp[len++] = (char)(0x80 | ((cp >> 6) & 0x3f));
    tmp[len++] = (char)(0x80 | (cp & 0x3f));
  } else {
    tmp[len++] = (char)(0xf0 | (cp >> 18));
    tmp[len++] = (char)(0x80 | ((cp >> 12) & 0x3f));
    tmp[len++] = (char)(0x80 | ((cp >> 6) & 0x3f));
    tmp[len++] = (char)(0x80 | (cp & 0x3f));
  }
  out.Append(tmp, len);
}

static int swell_valid_utf8_sequence_len(const unsigned char *s, int avail)
{
  if (avail <= 0) return 0;
  unsigned char c = s[0];
  if (c < 0x80) return 1;
  if (c < 0xc2) return 0;
  if (c < 0xe0) {
    if (avail < 2 || (s[1] & 0xc0) != 0x80) return 0;
    return 2;
  }
  if (c < 0xf0) {
    if (avail < 3 || (s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80) return 0;
    if (c == 0xe0 && s[1] < 0xa0) return 0;
    if (c == 0xed && s[1] >= 0xa0) return 0; // surrogate range
    return 3;
  }
  if (c < 0xf5) {
    if (avail < 4 || (s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 ||
        (s[3] & 0xc0) != 0x80) return 0;
    if (c == 0xf0 && s[1] < 0x90) return 0;
    if (c == 0xf4 && s[1] >= 0x90) return 0;
    return 4;
  }
  return 0;
}

const char *swell_text_for_skia(const char *buf, int len,
                                WDL_FastString &tmp, int *out_len)
{
  // Single-pass: check for 8-bit chars AND validate UTF-8
  bool has_8bit = false;
  bool needs_conversion = false;
  for (int i = 0; i < len;) {
    unsigned char c = (unsigned char)buf[i];
    if (c >= 0x80) has_8bit = true;

    int n = swell_valid_utf8_sequence_len((const unsigned char *)buf + i, len - i);
    if (n <= 0) {
      needs_conversion = true;
      if (has_8bit) break;
      i++;
    } else {
      i += n;
    }
  }

  if (!needs_conversion) {
    *out_len = len;
    return buf;
  }

  tmp.Set("");
  for (int i = 0; i < len;) {
    int n = swell_valid_utf8_sequence_len((const unsigned char *)buf + i, len - i);
    if (n > 0) {
      tmp.Append(buf + i, n);
      i += n;
    } else {
      swell_append_utf8_codepoint(tmp, swell_cp1252_to_unicode((unsigned char)buf[i]));
      i++;
    }
  }
  *out_len = tmp.GetLength();
  return tmp.Get();
}

// Helper: get or build and cache SkFont from HDC curfont
static const SkFont &swell_get_cached_skfont(HDC ctx)
{
  // Validate cached typeface has glyph data — SkFont default-constructs with
  // SkTypeface::MakeEmpty() which is non-null but glyphless, and measureText
  // on it returns 0. Without the countGlyphs check, an HDC with no font ever
  // selected (curfont == cached_font_ptr == NULL) would short-circuit here
  // and never get a real typeface assigned.
  if (ctx->cached_font_ptr == ctx->curfont &&
      ctx->cached_skfont.getTypeface() &&
      ctx->cached_skfont.getTypeface()->countGlyphs() > 0)
    return ctx->cached_skfont;

  ctx->cached_font_ptr = ctx->curfont;
  ctx->cached_fm_valid = false;
  SkFont &f = ctx->cached_skfont;
  // Fallback for HDCs with no font selected: use the DPI-scaled theme default
  // (swell_theme_rescale multiplies default_font_size by g_swell_ui_scale).
  // Hardcoding 12 here would render text at 12 physical px regardless of
  // scale — tooltips / drag readouts on HiDPI come out tiny.
  const float defaultSize = (float)g_swell_theme.default_font_size;
  float fontSize = defaultSize > 0.0f ? defaultSize : 12.0f;

  if (ctx->curfont && HGDIOBJ_VALID(ctx->curfont, TYPE_FONT)) {
    LOGFONT *lf = static_cast<LOGFONT *>(ctx->curfont->typedata);
    if (lf) {
      fontSize = lf->lfHeight < 0 ? (float)(-lf->lfHeight) : (float)lf->lfHeight;
      if (fontSize < 1.0f) fontSize = defaultSize > 0.0f ? defaultSize : 12.0f;
      int fontWeight = lf->lfWeight > 0 ? lf->lfWeight : FW_NORMAL;
      bool fontItalic = lf->lfItalic != 0;
      if (lf->lfFaceName[0]) {
        f.setTypeface(swell_get_typeface(lf->lfFaceName, fontWeight, fontItalic));
      }
      // Only apply synthetic emboldening if the matched typeface is not
      // already bold — avoids double-bold on fonts with true bold faces
      if (fontWeight >= FW_BOLD && f.getTypeface()) {
        SkFontStyle tfs = f.getTypeface()->fontStyle();
        if (tfs.weight() < SkFontStyle::kBold_Weight)
          f.setEmbolden(true);
      }
    }
  }
  // SkFont default-constructs with a non-null but glyphless empty typeface
  // singleton, so `!getTypeface()` alone is not enough — also require real
  // glyphs. Otherwise HDCs with no curfont selected (or curfont with empty
  // lfFaceName) keep the empty singleton and measureText returns 0.
  if (!f.getTypeface() || f.getTypeface()->countGlyphs() <= 0) {
    f.setTypeface(swell_get_typeface(g_swell_deffont_face, FW_NORMAL, false));
  }
  f.setSize(fontSize);
  if (g_swell_subpixel_text) {
    f.setSubpixel(true);
    f.setEdging(SkFont::Edging::kSubpixelAntiAlias);
  }
  return f;
}

// Helper: measure text width in pixels using SkFont.
// Returns advance (sum of glyph advances), not bbox — matches Win32
// GetTextExtentPoint32 and is what callers want for layout (spaces, trailing
// whitespace, zero-ink chars all contribute to advance but not bbox).
static float swell_text_width(const SkFont &font, const char *buf, int len)
{
  if (len <= 0 || !buf) return 0.0f;
  return font.measureText(buf, len, SkTextEncoding::kUTF8);
}

SkFont swell_make_skfont_from_hdc(HDC ctx)
{
  if (ctx) return SkFont(swell_get_cached_skfont(ctx));
  return SkFont();
}

// Helper: word-wrap a logical text segment into display lines that fit within maxWidth.
// Lines are broken at word boundaries (spaces) when possible; falls back to
// character-level breaks for very long words.
static void swell_wordwrap_line(const SkFont &font, const char *txt, int tstart,
                                int tend, float maxWidth,
                                WDL_TypedBuf<int> &lineEnds_out)
{
  int pos = tstart;
  while (pos < tend) {
    float segW = swell_text_width(font, txt + pos, tend - pos);
    if (segW <= maxWidth || maxWidth < 1.0f) {
      lineEnds_out.Add(tend);
      break;
    }
    // Find break point: walk forward measuring progressively longer substrings
    int lastBreak = pos;  // last good break point (word boundary)
    for (int test = pos + 1; test <= tend; test++) {
      float w = swell_text_width(font, txt + pos, test - pos);
      if (w > maxWidth) {
        // Use last word-break point if found, otherwise force-break at test-1
        if (lastBreak > pos) {
          lineEnds_out.Add(lastBreak);
          pos = lastBreak;
          // Skip leading spaces on next line
          while (pos < tend && txt[pos] == ' ') pos++;
        } else {
          int forceBrk = (test - 1 > pos) ? (test - 1) : (pos + 1);
          lineEnds_out.Add(forceBrk);
          pos = forceBrk;
        }
        break;
      }
      // Record word boundaries
      if (txt[test - 1] == ' ') {
        lastBreak = test;
      }
    }
    // If we reached the end without breaking, add the rest
    if (pos == tstart || (pos < tend && lineEnds_out.GetSize() == 0)) {
      // Shouldn't normally get here; fallback
      lineEnds_out.Add(tend);
      break;
    }
    // Recompute pos from last added lineEnd
    if (lineEnds_out.GetSize() > 0)
      pos = lineEnds_out.Get()[lineEnds_out.GetSize() - 1];
  }
}

// Process Win32 '&' accelerator prefixes when DT_NOPREFIX is not set.
// '&x' → strip &, underline char x. '&&' → single '&'. Sets clean text
// in outText and the byte offset of each char that needs an underline
// (relative to outText) in underlineAt.
static void swell_handle_prefix(const char *buf, int len,
                                WDL_FastString &outText,
                                WDL_TypedBuf<int> &underlineAt)
{
  outText.Set("");
  underlineAt.Resize(0);
  for (int i = 0; i < len; i++) {
    if (buf[i] == '&') {
      i++;
      if (i >= len) break;
      if (buf[i] == '&') {
        underlineAt.Add(outText.GetLength());
        outText.Append("&", 1);
      } else {
        underlineAt.Add(outText.GetLength());
        outText.Append(buf + i, 1);
      }
    } else {
      outText.Append(buf + i, 1);
    }
  }
}

int SWELL_DrawText(HDC ctx, const char *buf, int len, RECT *r, int align)
{
  if (!HDC_VALID(ctx) || !r) return 0;

  if (len == -1) len = (int)strlen(buf);
  if (len <= 0 || !buf) return 0;

  WDL_FastString utf8tmp, cleanbuf;
  const char *src = swell_text_for_skia(buf, len, utf8tmp, &len);
  if (len <= 0 || !src) return 0;

  // Strip \r (CR) — no visible glyph, breaks line-splitting.
  // Fast path: scan for \r first; skip allocation if none found.
  bool hasCR = false;
  for (int i = 0; i < len; ++i) {
    if (src[i] == '\r') { hasCR = true; break; }
  }
  if (hasCR) {
    for (int i = 0; i < len; i++)
      if (src[i] != '\r') cleanbuf.Append(src + i, 1);
    buf = cleanbuf.Get();
    len = cleanbuf.GetLength();
    if (len <= 0) return 0;
  } else {
    buf = src;
  }

  // Win32 &-prefix handling: strip accelerators from display text.
  WDL_FastString prefixStripped;
  WDL_TypedBuf<int> prefixUnderlineAt;
  if (!(align & DT_NOPREFIX)) {
    bool hasAmp = false;
    for (int i = 0; i < len; ++i) {
      if (buf[i] == '&') { hasAmp = true; break; }
    }
    if (hasAmp) {
      swell_handle_prefix(buf, len, prefixStripped, prefixUnderlineAt);
      buf = prefixStripped.Get();
      len = prefixStripped.GetLength();
      if (len <= 0) return 0;
    }
  }

  // Build SkFont from selected font or default
  const SkFont &font = swell_get_cached_skfont(ctx);

  float ascent, descent, lineht;
  int rowH;
  if (ctx->cached_fm_valid) {
    ascent  = ctx->cached_fm_ascent;
    descent = ctx->cached_fm_descent;
    lineht  = ctx->cached_fm_rowH;
    rowH    = (int)(lineht + 0.5f);
    if (rowH < 1) rowH = (int)(font.getSize() + 0.5f);
  } else {
    SkFontMetrics fm;
    font.getMetrics(&fm);
    ascent  = -fm.fAscent;
    descent = fm.fDescent;
    lineht  = ascent + descent;
    rowH = (int)(lineht + 0.5f);
    if (rowH < 1) rowH = (int)(font.getSize() + 0.5f);
    ctx->cached_fm_ascent  = ascent;
    ctx->cached_fm_descent = descent;
    ctx->cached_fm_rowH    = lineht;
    ctx->cached_fm_valid   = true;
  }

  SkPaint underlinePaint;
  bool haveUnderlines = prefixUnderlineAt.GetSize() > 0;
  if (haveUnderlines) {
    underlinePaint.setStyle(SkPaint::kFill_Style);
    underlinePaint.setColor(ctx->cur_text_color_int);
    underlinePaint.setAntiAlias(true);
  }

  bool wordbreak = (align & DT_WORDBREAK) && !(align & DT_SINGLELINE);
  if (!wordbreak) {
    // ---- Single-line path (original behaviour) ----

    // Measure text. Use advance (return value), not bbox.width() —
    // bbox is visual ink extent and is 0 for whitespace-only or zero-ink
    // glyphs, which would make DT_CALCRECT widths collapse.
    SkRect bounds;
    float advance = font.measureText(buf, len, SkTextEncoding::kUTF8, &bounds);

    int textW = (int)(advance + 0.5f);
    int textH = rowH;

    if (align & DT_CALCRECT) {
      r->left = 0;
      r->top = 0;
      r->right = textW;
      r->bottom = textH;
      return textH;
    }

    if (!ctx->canvas) return textH;

    // Determine text position (Skia drawString y = baseline)
    float x = (float)r->left;
    float y = (float)r->top + ascent;

    if (align & DT_CENTER)
      x = (float)(r->left + (r->right - r->left - textW) / 2);
    else if (align & DT_RIGHT)
      x = (float)(r->right - textW);

    if (align & DT_VCENTER)
      y = (float)(r->top + (r->bottom - r->top - textH) / 2 + ascent);

    bool opaque = (ctx->curbkmode == OPAQUE);

    // Draw background if opaque
    if (opaque) {
      SkPaint bgPaint;
      bgPaint.setStyle(SkPaint::kFill_Style);
      bgPaint.setColor(ctx->curbkcol);
      ctx->canvas->drawRect(
        SkRect::MakeLTRB((float)r->left, (float)r->top,
                          (float)r->right, (float)r->bottom), bgPaint);
    }

    // Draw text
    SkPaint textPaint;
    textPaint.setStyle(SkPaint::kFill_Style);
    textPaint.setColor(ctx->cur_text_color_int);
    textPaint.setAntiAlias(true);

    ctx->canvas->drawSimpleText(buf, len, SkTextEncoding::kUTF8,
                                x, y, font, textPaint);

    // Draw underlines for &-prefix chars
    if (haveUnderlines) {
      const int ulH = 1;
      const int ulOff = 2;
      float ulY = y + descent + ulOff + 0.5f;
      for (int ui = 0; ui < prefixUnderlineAt.GetSize(); ui++) {
        int uIdx = prefixUnderlineAt.Get()[ui];
        float uLeft = x + swell_text_width(font, buf, uIdx);
        float uRight = uLeft + swell_text_width(font, buf + uIdx, 1);
        ctx->canvas->drawRect(
          SkRect::MakeLTRB(uLeft, ulY, uRight, ulY + ulH), underlinePaint);
      }
    }

    if (opaque) {
      swell_DirtyContext(ctx, r->left, r->top, r->right, r->bottom);
    } else {
      int dirtyTop = (int)(y - ascent);
      int dirtyBot = (int)(y + descent);
      swell_DirtyContext(ctx, (int)x, dirtyTop, (int)(x + textW), dirtyBot);
    }

    return textH;
  }

  // ---- Multiline word-break path ----

  int availW = r->right - r->left;

  // Build display lines: split by \n, then word-wrap each logical line
  WDL_TypedBuf<int> dlineStarts;
  WDL_TypedBuf<int> dlineEnds;
  {
    int pos = 0;
    while (pos < len) {
      // Find next newline; strip trailing \r (CRLF → LF)
      int nl = pos;
      while (nl < len && buf[nl] != '\n') nl++;
      int lineEnd = nl;
      if (lineEnd > pos && buf[lineEnd - 1] == '\r') lineEnd--;

      // Word-wrap this logical segment
      WDL_TypedBuf<int> segEnds;
      swell_wordwrap_line(font, buf, pos, lineEnd, (float)availW, segEnds);
      if (segEnds.GetSize() == 0) {
        // Empty logical segment (blank line from consecutive \n)
        dlineStarts.Add(pos);
        dlineEnds.Add(pos);
      } else {
        int segStart = pos;
        for (int i = 0; i < segEnds.GetSize(); i++) {
          dlineStarts.Add(segStart);
          dlineEnds.Add(segEnds.Get()[i]);
          segStart = segEnds.Get()[i];
          while (segStart < lineEnd && buf[segStart] == ' ') segStart++;
          if (segStart > segEnds.Get()[i]) segStart = segEnds.Get()[i];
        }
      }

      pos = nl + 1;  // skip the \n
      if (nl >= len) break;
    }
  }

  int numLines = dlineStarts.GetSize();
  int totalH = numLines * rowH;

  if (align & DT_CALCRECT) {
    // Compute max line width for the output rect
    int maxLineW = 0;
    for (int i = 0; i < numLines; i++) {
      int lStart = dlineStarts.Get()[i];
      int lEnd   = dlineEnds.Get()[i];
      if (lStart >= lEnd) continue;
      int lw = (int)(swell_text_width(font, buf + lStart, lEnd - lStart) + 0.5f);
      if (lw > maxLineW) maxLineW = lw;
    }
    r->left = 0;
    r->top = 0;
    r->right = maxLineW;
    r->bottom = totalH;
    return totalH;
  }

  if (!ctx->canvas) return totalH;

  // Vertical positioning of the text block
  float yOffset;
  if (align & DT_BOTTOM)
    yOffset = (float)(r->bottom - totalH);
  else if (align & DT_VCENTER)
    yOffset = (float)(r->top + (r->bottom - r->top - totalH) / 2);
  else
    yOffset = (float)r->top;  // DT_TOP (or no flag)

  bool opaque = (ctx->curbkmode == OPAQUE);

  if (opaque) {
    SkPaint bgPaint;
    bgPaint.setStyle(SkPaint::kFill_Style);
    bgPaint.setColor(ctx->curbkcol);
    ctx->canvas->drawRect(
      SkRect::MakeLTRB((float)r->left, (float)r->top,
                        (float)r->right, (float)r->bottom), bgPaint);
  }

  SkPaint textPaint;
  textPaint.setStyle(SkPaint::kFill_Style);
  textPaint.setColor(ctx->cur_text_color_int);
  textPaint.setAntiAlias(true);

  for (int i = 0; i < numLines; i++) {
    int lStart = dlineStarts.Get()[i];
    int lEnd   = dlineEnds.Get()[i];
    if (lStart >= lEnd) continue;

    float lineW = swell_text_width(font, buf + lStart, lEnd - lStart);
    float lx = (float)r->left;
    if (align & DT_CENTER)
      lx = (float)(r->left + (availW - lineW) / 2);
    else if (align & DT_RIGHT)
      lx = (float)(r->right - lineW);

    float ly = yOffset + i * rowH + ascent;
    ctx->canvas->drawSimpleText(buf + lStart, lEnd - lStart,
                                SkTextEncoding::kUTF8, lx, ly, font, textPaint);

    if (opaque) {
      swell_DirtyContext(ctx, r->left, (int)(yOffset + i * rowH),
                         r->right, (int)(yOffset + (i + 1) * rowH));
    } else {
      int dirtyTop = (int)(ly - ascent);
      int dirtyBot = (int)(ly + descent);
      swell_DirtyContext(ctx, (int)lx, dirtyTop, (int)(lx + lineW), dirtyBot);
    }
  }

  // Draw underlines for &-prefix chars in multiline path
  if (haveUnderlines) {
    const int ulH = 1;
    const int ulOff = 2;
    for (int ui = 0; ui < prefixUnderlineAt.GetSize(); ui++) {
      int uIdx = prefixUnderlineAt.Get()[ui];
      int lineIdx = -1;
      for (int i = 0; i < numLines; i++) {
        int lStart = dlineStarts.Get()[i];
        int lEnd2  = dlineEnds.Get()[i];
        if (uIdx >= lStart && uIdx < lEnd2) {
          lineIdx = i;
          break;
        }
      }
      if (lineIdx < 0) continue;
      int lStart2 = dlineStarts.Get()[lineIdx];
      int lEnd2   = dlineEnds.Get()[lineIdx];
      float lineW = swell_text_width(font, buf + lStart2, lEnd2 - lStart2);
      float lx2 = (float)r->left;
      if (align & DT_CENTER)
        lx2 = (float)(r->left + (availW - lineW) / 2);
      else if (align & DT_RIGHT)
        lx2 = (float)(r->right - lineW);

      float uLeft = lx2 + swell_text_width(font, buf + lStart2, uIdx - lStart2);
      float uRight = uLeft + swell_text_width(font, buf + uIdx, 1);
      float ulY = yOffset + lineIdx * rowH + ascent + descent + ulOff + 0.5f;
      ctx->canvas->drawRect(
        SkRect::MakeLTRB(uLeft, ulY, uRight, ulY + ulH), underlinePaint);
    }
  }

  return totalH;
}

BOOL GetTextMetrics(HDC ctx, TEXTMETRIC *tm)
{
  if (!HDC_VALID(ctx) || !tm) return FALSE;

  memset(tm, 0, sizeof(TEXTMETRIC));

  const SkFont &font = swell_get_cached_skfont(ctx);

  if (!ctx->cached_fm_valid) {
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    ctx->cached_fm_ascent  = -metrics.fAscent;
    ctx->cached_fm_descent = metrics.fDescent;
    ctx->cached_fm_rowH    = ctx->cached_fm_ascent + ctx->cached_fm_descent;
    ctx->cached_fm_leading = metrics.fLeading;
    ctx->cached_fm_valid   = true;
  }

  tm->tmAscent = (int)(ctx->cached_fm_ascent + 0.5f);
  tm->tmDescent = (int)(ctx->cached_fm_descent + 0.5f);
  tm->tmHeight = tm->tmAscent + tm->tmDescent;
  tm->tmInternalLeading = (int)(ctx->cached_fm_leading + 0.5f);
  tm->tmAveCharWidth = (int)(font.getSize() * 0.5f + 0.5f);

  return TRUE;
}

int GetTextFace(HDC ctx, int nCount, LPTSTR lpFaceName)
{
  if (!lpFaceName || nCount <= 0) return 0;
  lpFaceName[0] = 0;
  if (!HDC_VALID(ctx)) return 0;

  const SkFont &font = swell_get_cached_skfont(ctx);
  sk_sp<SkTypeface> tf = font.refTypeface();
  if (!tf) return 0;

  SkString name;
  tf->getFamilyName(&name);
  if (name.isEmpty()) return 0;

  int len = (int)name.size();
  if (len >= nCount) len = nCount - 1;
  memcpy(lpFaceName, name.c_str(), len);
  lpFaceName[len] = 0;
  return len;
}

int GetGlyphIndicesW(HDC ctx, wchar_t *buf, int len, unsigned short *indices,
                     int flags)
{
  (void)ctx; (void)buf; (void)flags;
  if (indices && len > 0) memset(indices, 0, (size_t)len * sizeof(unsigned short));
  return 0;
}

// ---------------------------------------------------------------------------
// Clip region
// ---------------------------------------------------------------------------

void SWELL_PushClipRegion(HDC ctx)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  ctx->clip_save_count = ctx->canvas->save();
}

void SWELL_SetClipRegion(HDC ctx, const RECT *r)
{
  if (!HDC_VALID(ctx) || !ctx->canvas || !r) return;
  ctx->canvas->clipRect(
    SkRect::MakeLTRB((float)r->left, (float)r->top,
                      (float)r->right, (float)r->bottom));
}

void SWELL_SetClipRoundRect(HDC ctx, int l, int t, int r, int b, int radius)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (r <= l || b <= t) return;
  float rad = (float)(radius < 0 ? 0 : radius);
  SkRect rect = SkRect::MakeLTRB((float)l, (float)t, (float)r, (float)b);
  SkRRect rr = SkRRect::MakeRectXY(rect, rad, rad);
  ctx->canvas->clipRRect(rr, SkClipOp::kIntersect, true);
}

void SWELL_PopClipRegion(HDC ctx)
{
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  ctx->canvas->restoreToCount(ctx->clip_save_count);
}

// ---------------------------------------------------------------------------
// System colors
// ---------------------------------------------------------------------------

int GetSysColor(int idx)
{
  // Map Win32 system color constants onto the semantic palette so legacy
  // host code keeps working. The traditional bevel highlight/shadow pair
  // collapses to a single subtle border color in flat modern UIs.
  switch (idx) {
    case COLOR_3DFACE:       return g_swell_theme.bg_window;
    case COLOR_3DSHADOW:     return g_swell_theme.border;
    case COLOR_3DHILIGHT:    return g_swell_theme.bg_surface;
    case COLOR_3DDKSHADOW:   return g_swell_theme.border_strong;
    case COLOR_BTNFACE:      return g_swell_theme.bg_button;
    case COLOR_BTNTEXT:      return g_swell_theme.fg_text;
    case COLOR_WINDOW:       return g_swell_theme.bg_input;
    case COLOR_SCROLLBAR:    return g_swell_theme.bg_scrollbar;
    case COLOR_INFOBK:       return g_swell_theme.info_bg;
    case COLOR_INFOTEXT:     return g_swell_theme.info_text;
    default:                 return 0;
  }
}

// ---------------------------------------------------------------------------
// Context info
// ---------------------------------------------------------------------------

void *SWELL_GetCtxGC(HDC ctx)
{
  (void)ctx;
  return nullptr;
}

void *SWELL_GetCtxFrameBuffer(HDC ctx)
{
  if (!ctx || !HDC_VALID(ctx)) return nullptr;
  HDC__ *ct = (HDC__*)ctx;
  if (!ct->surface) return nullptr;
  SkPixmap pm;
  if (!ct->surface->peekPixels(&pm)) return nullptr;

  void *ptr = pm.writable_addr();
  if (!ptr) return nullptr;

  // Account for canvas translation so returned pointer maps to
  // window client area origin (0,0) in drawing coordinates.
  if (ct->canvas) {
    SkM44 m = ct->canvas->getLocalToDevice();
    float tx = m.rc(0,3), ty = m.rc(1,3);
    int ix = (int)tx, iy = (int)ty;
    if (ix >= 0 && ix < pm.width())
      ptr = (uint8_t*)ptr + ix * 4;
    if (iy >= 0 && iy < pm.height())
      ptr = (uint8_t*)ptr + iy * (int)pm.rowBytes();
  }
  return ptr;
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

int AddFontResourceEx(LPCTSTR str, DWORD fl, void *pdv)
{
  (void)str; (void)fl; (void)pdv;
  return 0;
}

HFONT SWELL_GetDefaultFont()
{
  static int s_last_font_size = 0;
  int cur_size = g_swell_theme.default_font_size;
  if (!g_swell_default_font_instance || s_last_font_size != cur_size) {
    if (g_swell_default_font_instance)
      DeleteObject(g_swell_default_font_instance);
    g_swell_default_font_instance = CreateFont(
        -cur_size, 0, 0, 0, FW_NORMAL,
        0, 0, 0, 0, 0, 0, 0, 0, g_swell_deffont_face);
    g_swell_default_font = g_swell_default_font_instance;
    s_last_font_size = cur_size;
  }
  return g_swell_default_font_instance;
}

// ---------------------------------------------------------------------------
// Paint pipeline
// ---------------------------------------------------------------------------

static bool swell_child_needs_parent_underpaint(HWND child)
{
  if (!child) return false;
  return child->m_wndproc == buttonWindowProc ||
         child->m_wndproc == editWindowProc ||
         child->m_wndproc == listViewWindowProc ||
         child->m_wndproc == treeViewWindowProc ||
         child->m_wndproc == comboWindowProc ||
         child->m_wndproc == tabControlWindowProc ||
         child->m_wndproc == trackbarWindowProc ||
         child->m_wndproc == progressWindowProc;
}

static void swell_underpaint_child_background(HWND parent, HWND child,
                                              SkCanvas *canvas,
                                              swell_gdpLocalContext *ctx)
{
  if (!parent || !child || !canvas || !ctx) return;

  RECT cr = child->m_position;
  if (cr.right <= cr.left || cr.bottom <= cr.top) return;

  const int save = canvas->save();
  canvas->clipRect(
      SkRect::MakeLTRB((float)cr.left, (float)cr.top,
                       (float)cr.right, (float)cr.bottom),
      SkClipOp::kIntersect, false);

  if (parent->m_style & WS_CLIPCHILDREN) {
    for (int i = 0; i < parent->m_children.GetSize(); i++) {
      HWND other = parent->m_children.Get(i);
      if (!other || other == child || !other->m_visible) continue;
      RECT orc = other->m_position;
      if (orc.right <= orc.left || orc.bottom <= orc.top) continue;
      canvas->clipRect(
          SkRect::MakeLTRB((float)orc.left, (float)orc.top,
                           (float)orc.right, (float)orc.bottom),
          SkClipOp::kDifference, false);
    }
  }

  RECT oldClip = ctx->clipr;
  ctx->clipr = cr;
  SendMessage(parent, WM_PAINT, (WPARAM)ctx, 0);
  ctx->clipr = oldClip;

  canvas->restoreToCount(save);
}

void SWELL_internalSkiaPaint(HWND hwnd, SkCanvas *canvas,
    int bmout_xpos, int bmout_ypos, bool forceref)
{
  if (!hwnd) return;

  if (hwnd->m_invalidated)
    forceref = true;

  if (forceref || hwnd->m_child_invalidated) {
    swell_perf_note_paint_window(hwnd);

    // Clear old dirty state before sending paint messages. Any invalidation
    // caused by input or app code during paint remains set for the next frame.
    hwnd->m_invalidated = false;
    hwnd->m_child_invalidated = false;

    swell_gdpLocalContext ctx_local{};
    ctx_local.ctx.canvas = canvas;
    if (canvas)
      ctx_local.ctx.surface = sk_ref_sp(canvas->getSurface());
    ctx_local.ctx.dirty_rect_valid = false;
    ctx_local.ctx.surface_offs.x = bmout_xpos;
    ctx_local.ctx.surface_offs.y = bmout_ypos;
    ctx_local.ctx.clip_save_count = 0;
    ctx_local.ctx.curpen = nullptr;
    ctx_local.ctx.curbrush = nullptr;
    ctx_local.ctx.curfont = nullptr;
    ctx_local.ctx.cached_font_ptr = nullptr;
    ctx_local.ctx.cached_fm_valid = false;
    ctx_local.ctx.cur_text_color_int = SK_ColorBLACK;
    ctx_local.ctx.curbkmode = TRANSPARENT;
    ctx_local.ctx.curbkcol = 0;
    ctx_local.ctx.lastpos_x = 0.0f;
    ctx_local.ctx.lastpos_y = 0.0f;

    RECT local = { 0, 0,
      hwnd->m_position.right - hwnd->m_position.left,
      hwnd->m_position.bottom - hwnd->m_position.top };
    ctx_local.clipr = local;
    RECT ncr = local;

    hwnd->m_paintctx = &ctx_local;

    // WM_NCCALCSIZE: compute client inset (e.g. menu bar at top)
    SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncr);
    int nc_left = ncr.left;
    int nc_top  = ncr.top;

    if (forceref) {
      SendMessage(hwnd, WM_NCPAINT, 1, 0);
    }

    // Translate canvas for client area painting: WM_PAINT draws at (0,0)
    // which should map to window pixel (nc_left, nc_top).
    int nc_save = canvas ? canvas->save() : 0;
    if (canvas && (nc_left || nc_top))
      canvas->translate((float)nc_left, (float)nc_top);

    ctx_local.clipr = { 0, 0,
      ncr.right - ncr.left, ncr.bottom - ncr.top };
    ctx_local.ctx.surface_offs.x = bmout_xpos + nc_left;
    ctx_local.ctx.surface_offs.y = bmout_ypos + nc_top;

    ctx_local.ctx.curfont = hwnd->m_font;

    int client_paint_save = canvas ? canvas->save() : 0;
    if (canvas && (hwnd->m_style & WS_CLIPCHILDREN)) {
      for (int i = 0; i < hwnd->m_children.GetSize(); i++) {
        HWND child = hwnd->m_children.Get(i);
        if (!child || !child->m_visible) continue;
        if (swell_child_needs_parent_underpaint(child)) continue;
        RECT cr = child->m_position;
        canvas->clipRect(
          SkRect::MakeLTRB((float)cr.left, (float)cr.top,
                            (float)cr.right, (float)cr.bottom),
          SkClipOp::kDifference, false);
      }
    }

    if (forceref) {
      SendMessage(hwnd, WM_PAINT, (WPARAM)&ctx_local, 0);
    }
    if (canvas) canvas->restoreToCount(client_paint_save);

    if (!forceref && canvas) {
      for (int i = 0; i < hwnd->m_children.GetSize(); i++) {
        HWND child = hwnd->m_children.Get(i);
        if (!child || !child->m_visible) continue;
        if (!child->m_invalidated && !child->m_child_invalidated) continue;
        if (!swell_child_needs_parent_underpaint(child)) continue;
        swell_underpaint_child_background(hwnd, child, canvas, &ctx_local);
      }
    }

    hwnd->m_paintctx = nullptr;

    // Recurse into visible children that need paint.
    // Child m_position is in client coordinates; canvas is already translated
    // to (nc_left, nc_top) so children render at the right window position.
    for (int i = 0; i < hwnd->m_children.GetSize(); i++) {
      HWND child = hwnd->m_children.Get(i);
      if (!child || !child->m_visible) continue;
      if (!forceref && !child->m_invalidated && !child->m_child_invalidated) continue;

      int saveCount = canvas ? canvas->save() : 0;

      if (canvas) {
        RECT cr = child->m_position;
        canvas->clipRect(
          SkRect::MakeLTRB((float)cr.left, (float)cr.top,
                            (float)cr.right, (float)cr.bottom));
        canvas->translate((float)cr.left, (float)cr.top);
      }

      // bmout offsets are in window (not client) pixels, so add nc inset.
      SWELL_internalSkiaPaint(child, canvas,
        bmout_xpos + nc_left + child->m_position.left,
        bmout_ypos + nc_top  + child->m_position.top,
        forceref);

      if (canvas) canvas->restoreToCount(saveCount);
    }

    // Undo the NC translation for child recursion
    if (canvas && (nc_left || nc_top))
      canvas->restoreToCount(nc_save);
  }
}

// ---------------------------------------------------------------------------
// SWELL_FillDialogBackground
// ---------------------------------------------------------------------------

void SWELL_FillDialogBackground(HDC hdc, const RECT *r, int level)
{
  (void)level;
  if (!HDC_VALID(hdc) || !hdc->canvas || !r) return;

  HBRUSH br = CreateSolidBrush(g_swell_theme.bg_window);
  SWELL_FillRect(hdc, r, br);
  DeleteObject(br);
}

// ---------------------------------------------------------------------------
// swell_theme: light + dark presets, Adwaita 2026 / macOS Mojave inspired
// ---------------------------------------------------------------------------
// All metric fields below are in *logical* pixels at 1.0x DPI. Colors are
// COLORREF (0x00BBGGRR). swell_theme_rescale() converts metrics to physical
// pixels using the current g_swell_ui_scale.

static void swell_theme_populate_light(swell_theme &t)
{
  // Accent — Adwaita blue
  const int accent_base = RGB(0x35, 0x84, 0xE4);

  // Surfaces
  t.bg_window    = RGB(0xFA, 0xFA, 0xFA);
  t.bg_surface   = RGB(0xFF, 0xFF, 0xFF);
  t.bg_input     = RGB(0xFF, 0xFF, 0xFF);
  t.bg_input_alt = RGB(0xF6, 0xF6, 0xF6);
  t.bg_header    = RGB(0xF0, 0xF0, 0xF0);

  // Buttons (neutral; default button uses accent at draw time)
  t.bg_button         = RGB(0xFF, 0xFF, 0xFF);
  t.bg_button_hover   = RGB(0xF2, 0xF2, 0xF2);
  t.bg_button_pressed = RGB(0xE5, 0xE5, 0xE5);

  // Accent
  t.accent         = accent_base;
  t.accent_hover   = RGB(0x52, 0x94, 0xE5);
  t.accent_pressed = RGB(0x1B, 0x6F, 0xD0);
  t.fg_on_accent   = RGB(0xFF, 0xFF, 0xFF);

  // Menus
  t.bg_menu          = RGB(0xFF, 0xFF, 0xFF);
  t.bg_menu_hover    = accent_base;
  t.bg_menubar       = RGB(0xFA, 0xFA, 0xFA);
  t.bg_menubar_hover = RGB(0xED, 0xED, 0xED);

  // Tabs
  t.bg_tab        = RGB(0xF0, 0xF0, 0xF0);
  t.bg_tab_active = RGB(0xFF, 0xFF, 0xFF);

  // Scrollbar / trackbar / progress
  t.bg_scrollbar          = RGB(0xF5, 0xF5, 0xF5);
  t.scrollbar_thumb       = RGB(0xC0, 0xC0, 0xC0);
  t.scrollbar_thumb_hover = RGB(0xA0, 0xA0, 0xA0);
  t.trackbar_track        = RGB(0xDC, 0xDC, 0xDC);
  t.trackbar_fill         = accent_base;
  t.trackbar_thumb        = RGB(0xFF, 0xFF, 0xFF);
  t.progress_track        = RGB(0xDC, 0xDC, 0xDC);
  t.progress_fill         = accent_base;

  // Text
  t.fg_text          = RGB(0x1E, 0x1E, 0x1E);
  t.fg_text_dim      = RGB(0x5C, 0x5C, 0x5C);
  t.fg_text_disabled = RGB(0xA0, 0xA0, 0xA0);

  // Borders / focus
  t.border        = RGB(0xD4, 0xD4, 0xD4);
  t.border_strong = RGB(0xC0, 0xC0, 0xC0);
  t.focus_ring    = accent_base;

  // Tooltip
  t.info_bg   = RGB(0xFF, 0xFC, 0xE5);
  t.info_text = RGB(0x1E, 0x1E, 0x1E);

  // Caret
  t.caret = RGB(0x1E, 0x1E, 0x1E);

  // Drop shadow approximation
  t.shadow = RGB(0xBF, 0xBF, 0xBF);
}

static void swell_theme_populate_dark(swell_theme &t)
{
  const int accent_base = RGB(0x35, 0x84, 0xE4);

  t.bg_window    = RGB(0x24, 0x24, 0x24);
  t.bg_surface   = RGB(0x2E, 0x2E, 0x2E);
  t.bg_input     = RGB(0x1E, 0x1E, 0x1E);
  t.bg_input_alt = RGB(0x23, 0x23, 0x23);
  t.bg_header    = RGB(0x2A, 0x2A, 0x2A);

  t.bg_button         = RGB(0x35, 0x35, 0x35);
  t.bg_button_hover   = RGB(0x40, 0x40, 0x40);
  t.bg_button_pressed = RGB(0x4A, 0x4A, 0x4A);

  t.accent         = accent_base;
  t.accent_hover   = RGB(0x52, 0x94, 0xE5);
  t.accent_pressed = RGB(0x1B, 0x6F, 0xD0);
  t.fg_on_accent   = RGB(0xFF, 0xFF, 0xFF);

  t.bg_menu          = RGB(0x2E, 0x2E, 0x2E);
  t.bg_menu_hover    = accent_base;
  t.bg_menubar       = RGB(0x24, 0x24, 0x24);
  t.bg_menubar_hover = RGB(0x35, 0x35, 0x35);

  t.bg_tab        = RGB(0x24, 0x24, 0x24);
  t.bg_tab_active = RGB(0x2E, 0x2E, 0x2E);

  t.bg_scrollbar          = RGB(0x1E, 0x1E, 0x1E);
  t.scrollbar_thumb       = RGB(0x55, 0x55, 0x55);
  t.scrollbar_thumb_hover = RGB(0x6A, 0x6A, 0x6A);
  t.trackbar_track        = RGB(0x40, 0x40, 0x40);
  t.trackbar_fill         = accent_base;
  t.trackbar_thumb        = RGB(0xE0, 0xE0, 0xE0);
  t.progress_track        = RGB(0x40, 0x40, 0x40);
  t.progress_fill         = accent_base;

  t.fg_text          = RGB(0xFF, 0xFF, 0xFF);
  t.fg_text_dim      = RGB(0xB5, 0xB5, 0xB5);
  t.fg_text_disabled = RGB(0x70, 0x70, 0x70);

  t.border        = RGB(0x1A, 0x1A, 0x1A);
  t.border_strong = RGB(0x60, 0x60, 0x60);
  t.focus_ring    = accent_base;

  t.info_bg   = RGB(0x3A, 0x3A, 0x2A);
  t.info_text = RGB(0xFF, 0xFF, 0xFF);

  t.caret = RGB(0xFF, 0xFF, 0xFF);

  t.shadow = RGB(0x10, 0x10, 0x10);
}

static void swell_theme_populate_metrics(swell_theme &t)
{
  t.corner_radius           = 4;
  t.corner_radius_large     = 6;
  t.border_width            = 1;
  t.focus_ring_width        = 2;
  t.focus_ring_offset       = 2;

  t.padding_button_h        = 14;
  t.padding_button_v        = 6;
  t.padding_edit_h          = 8;
  t.padding_edit_v          = 4;
  t.padding_menu_item_h     = 12;
  t.padding_menu_item_v     = 6;
  t.padding_listheader_h    = 8;
  t.padding_listheader_v    = 4;

  t.button_min_h            = 28;
  t.edit_min_h              = 26;
  t.menubar_height          = 28;
  t.menu_item_height        = 26;
  t.menu_separator_height   = 8;
  t.tab_height              = 30;
  t.scrollbar_width         = 14;
  t.scrollbar_min_thumb_height = 4;
  t.trackbar_track_h        = 4;
  t.trackbar_thumb_r        = 7;
  t.checkbox_size           = 18;
  t.radio_size              = 18;

  t.default_font_size       = 12;
  t.small_font_size         = 10;
}

void swell_theme_init(int mode)
{
#ifndef SWELL_TARGET_SDL3
  g_swell_subpixel_text = false;
#endif
  g_swell_theme_mode = mode;
  if (mode == SWELL_THEME_DARK) swell_theme_populate_dark(g_swell_theme);
  else                          swell_theme_populate_light(g_swell_theme);
  swell_theme_populate_metrics(g_swell_theme);
  swell_theme_rescale();
}

void swell_theme_rescale()
{
  // Rescale only metric fields (logical -> physical px). Re-populate metrics
  // first so repeated calls don't compound the scale factor.
  swell_theme_populate_metrics(g_swell_theme);
  if (g_swell_ui_scale == 256) return;
  const double sc = g_swell_ui_scale * (1.0 / 256.0);
  #define SC(x) g_swell_theme.x = (int)(g_swell_theme.x * sc + 0.5)
  SC(corner_radius); SC(corner_radius_large); SC(border_width);
  SC(focus_ring_width); SC(focus_ring_offset);
  SC(padding_button_h);    SC(padding_button_v);
  SC(padding_edit_h);      SC(padding_edit_v);
  SC(padding_menu_item_h); SC(padding_menu_item_v);
  SC(padding_listheader_h); SC(padding_listheader_v);
  SC(button_min_h); SC(edit_min_h);
  SC(menubar_height); SC(menu_item_height); SC(menu_separator_height);
  SC(tab_height); SC(scrollbar_width); SC(scrollbar_min_thumb_height);
  SC(trackbar_track_h); SC(trackbar_thumb_r);
  SC(checkbox_size); SC(radio_size);
  SC(default_font_size); SC(small_font_size);
  #undef SC
}
