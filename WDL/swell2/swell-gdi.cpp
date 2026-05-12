/*
  SWELL2 GDI module — headless build
  Implements HDC lifecycle, GDI object management, drawing stubs,
  text metrics, color conversion, and the GDI object pool.
*/

#include "swell-gdi-internalpool.h"

#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

swell_colortheme g_swell_ctheme;
const char *g_swell_deffont_face = "Arial";

static HFONT g_swell_default_font_instance = nullptr;
HFONT g_swell_default_font = nullptr; // extern, may alias the static

int g_swell_ui_scale = 256;

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
  } else {
    p = new HDC__();
  }
  memset(p, 0, sizeof(HDC__));
  p->_infreelist = false;
  return p;
}

void SWELL_GDP_CTX_DELETE(HDC__ *hdc)
{
  if (!hdc) return;
  std::lock_guard<std::mutex> lock(g_hdc_pool_mutex);
  if (g_hdc_pool_count < SWELL_MAX_HDC_POOL) {
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
  std::lock_guard<std::mutex> lock(g_gdiobj_pool_mutex);
  if (g_gdiobj_pool_count < SWELL_MAX_HGDIOBJ_POOL) {
    obj->_infreelist = true;
    obj->_next = g_gdiobj_free_list;
    g_gdiobj_free_list = obj;
    g_gdiobj_pool_count++;
  } else {
    delete obj;
  }
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

// Convert SkColor back to native RGB
static inline int SkColorToNativeRGB(int sk)
{
  return RGB(SkColorGetR(sk), SkColorGetG(sk), SkColorGetB(sk));
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

  ctx->surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h));
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

  HDC__ *ctx = SWELL_GDP_CTX_NEW();
  if (!ctx) return nullptr;

  sk_sp<SkSurface> bs = hwnd->m_backingstore;
  if (bs) {
    ctx->canvas = bs->getCanvas();
  } else {
    ctx->canvas = nullptr;
  }
  // surface is NOT owned — GetDC borrows from the window
  ctx->surface.reset();
  ctx->surface_offs.x = 0;
  ctx->surface_offs.y = 0;
  ctx->dirty_rect_valid = false;
  ctx->clip_save_count = 0;
  ctx->curpen = nullptr;
  ctx->curbrush = nullptr;
  ctx->curfont = hwnd->m_font;
  ctx->cur_text_color_int = SK_ColorBLACK;
  ctx->curbkmode = TRANSPARENT;
  ctx->curbkcol = 0;
  ctx->lastpos_x = 0.0f;
  ctx->lastpos_y = 0.0f;

  return ctx;
}

HDC GetWindowDC(HWND hwnd)
{
  return GetDC(hwnd);
}

void ReleaseDC(HWND hwnd, HDC ctx)
{
  (void)hwnd;
  if (!ctx || !HDC_VALID(ctx)) return;
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
  obj->wid = wid;
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

  HGDIOBJ__ *obj = GDP_OBJECT_NEW();
  if (!obj) return nullptr;
  obj->type = TYPE_BITMAP;

  SkBitmap *bm = new SkBitmap();
  bm->allocN32Pixels(width, height);
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
  bm->allocN32Pixels(w, h);
  obj->typedata = bm;
  obj->additional_refcnt = 0;
  return obj;
}

HICON LoadNamedImage(const char *name, bool alphaFromMask)
{
  (void)name;
  (void)alphaFromMask;
  return nullptr;
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
      return old ? old : GDI_NULL_SENTINEL(sentinelType);
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
  return old ? old : GDI_NULL_SENTINEL(t);
}

void DeleteObject(HGDIOBJ obj)
{
  if (!obj) return;

  // Never delete stock objects (null pen/brush)
  if (obj == &g_null_pen_obj || obj == &g_null_brush_obj) return;

  // Never delete sentinel values
  if (reinterpret_cast<INT_PTR>(obj) < 256) return;

  if (!HGDIOBJ_VALID(obj)) return;

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

HGDIOBJ GetStockObject(int wh)
{
  if (wh == NULL_PEN)  return &g_null_pen_obj;
  if (wh == NULL_BRUSH) return &g_null_brush_obj;
  return nullptr;
}

HGDIOBJ SWELL_CloneGDIObject(HGDIOBJ a)
{
  if (!a || !HGDIOBJ_VALID(a)) return nullptr;
  a->additional_refcnt++;
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
// Drawing (stubs — no-op, but check state and update dirty rect)
// ---------------------------------------------------------------------------

void Rectangle(HDC ctx, int l, int t, int r, int b)
{
  if (!HDC_VALID(ctx)) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, l, t, r, b);
}

void Ellipse(HDC ctx, int l, int t, int r, int b)
{
  if (!HDC_VALID(ctx)) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, l, t, r, b);
}

void RoundRect(HDC ctx, int x, int y, int x2, int y2, int xrnd, int yrnd)
{
  (void)xrnd; (void)yrnd;
  if (!HDC_VALID(ctx)) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, x, y, x2, y2);
}

void SWELL_FillRect(HDC ctx, const RECT *r, HBRUSH br)
{
  if (!HDC_VALID(ctx) || !r) return;
  // Use explicit brush if provided, otherwise use selected brush
  if (br && HGDIOBJ_VALID(br, TYPE_BRUSH) && br->wid >= 0) {
    swell_DirtyContext(ctx, r->left, r->top, r->right, r->bottom);
  } else if (brush_valid(ctx)) {
    swell_DirtyContext(ctx, r->left, r->top, r->right, r->bottom);
  }
}

void SWELL_Polygon(HDC ctx, POINT *pts, int npts)
{
  if (!HDC_VALID(ctx) || !pts || npts < 2) return;
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
  if (!HDC_VALID(ctx)) return;
  if (!pen_valid(ctx)) {
    ctx->lastpos_x = (float)x;
    ctx->lastpos_y = (float)y;
    return;
  }

  int l = (int)ctx->lastpos_x, t = (int)ctx->lastpos_y;
  int r2 = x, b2 = y;
  if (l > r2) { int tmp = l; l = r2; r2 = tmp; }
  if (t > b2) { int tmp = t; t = b2; b2 = tmp; }
  swell_DirtyContext(ctx, l, t, r2, b2);

  ctx->lastpos_x = (float)x;
  ctx->lastpos_y = (float)y;
}

void SetPixel(HDC ctx, int x, int y, int c)
{
  (void)c;
  if (!HDC_VALID(ctx)) return;
  swell_DirtyContext(ctx, x, y, x + 1, y + 1);
}

void PolyBezierTo(HDC ctx, POINT *pts, int np)
{
  if (!HDC_VALID(ctx) || !pts || np < 1) return;
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
  swell_DirtyContext(ctx, l, t, r2, b2);

  ctx->lastpos_x = (float)pts[np - 1].x;
  ctx->lastpos_y = (float)pts[np - 1].y;
}

void PolyPolyline(HDC ctx, const POINT *pts, const DWORD *cnts, int nseg)
{
  if (!HDC_VALID(ctx) || !pts || !cnts || nseg < 1) return;
  if (!pen_valid(ctx)) return;

  const POINT *p = pts;
  int l = 0, t = 0, r2 = 0, b2 = 0;
  bool first = true;
  for (int i = 0; i < nseg; i++) {
    DWORD n = cnts[i];
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
  if (!first) swell_DirtyContext(ctx, l, t, r2, b2);
}

// ---------------------------------------------------------------------------
// Blit (stubs)
// ---------------------------------------------------------------------------

void BitBlt(HDC hdcOut, int x, int y, int w, int h,
            HDC hdcIn, int xin, int yin, int mode)
{
  (void)xin; (void)yin; (void)mode;
  if (!HDC_VALID(hdcOut)) return;
  (void)hdcIn;
  if (w > 0 && h > 0)
    swell_DirtyContext(hdcOut, x, y, x + w, y + h);
}

void StretchBlt(HDC hdcOut, int x, int y, int w, int h,
                HDC hdcIn, int xin, int yin, int srcw, int srch, int mode)
{
  (void)hdcIn; (void)xin; (void)yin; (void)srcw; (void)srch; (void)mode;
  if (!HDC_VALID(hdcOut)) return;
  if (w > 0 && h > 0)
    swell_DirtyContext(hdcOut, x, y, x + w, y + h);
}

#ifndef SWELL_TARGET_OSX
void StretchBltFromMem(HDC hdcOut, int x, int y, int w, int h,
                       const void *bits, int srcw, int srch, int srcspan)
{
  (void)bits; (void)srcw; (void)srch; (void)srcspan;
  if (!HDC_VALID(hdcOut)) return;
  if (w > 0 && h > 0)
    swell_DirtyContext(hdcOut, x, y, x + w, y + h);
}

int SWELL_GetScaling256(void)
{
  return g_swell_ui_scale;
}
#endif

void DrawImageInRect(HDC ctx, HICON img, const RECT *r)
{
  (void)img;
  if (!HDC_VALID(ctx) || !r) return;
  swell_DirtyContext(ctx, r->left, r->top, r->right, r->bottom);
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
  return SkColorToNativeRGB(ctx->cur_text_color_int);
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
// Text (stubs / fallbacks)
// ---------------------------------------------------------------------------

int SWELL_DrawText(HDC ctx, const char *buf, int len, RECT *r, int align)
{
  (void)buf; (void)len; (void)align;
  if (!HDC_VALID(ctx) || !r) return 0;
  // DT_CALCRECT: don't draw, just measure — but we return 0 anyway
  return 0;
}

BOOL GetTextMetrics(HDC ctx, TEXTMETRIC *tm)
{
  if (!HDC_VALID(ctx) || !tm) return FALSE;

  memset(tm, 0, sizeof(TEXTMETRIC));
  tm->tmAscent = 8;
  tm->tmDescent = 0;
  tm->tmHeight = 8;
  tm->tmInternalLeading = 0;
  tm->tmAveCharWidth = 4;

  return TRUE;
}

int GetTextFace(HDC ctx, int nCount, LPTSTR lpFaceName)
{
  (void)ctx;
  if (lpFaceName && nCount > 0) lpFaceName[0] = 0;
  return 0;
}

int GetGlyphIndicesW(HDC ctx, wchar_t *buf, int len, unsigned short *indices,
                     int flags)
{
  (void)ctx; (void)buf; (void)len; (void)indices; (void)flags;
  return 0;
}

// ---------------------------------------------------------------------------
// Clip region (stubs)
// ---------------------------------------------------------------------------

void SWELL_PushClipRegion(HDC ctx)
{
  (void)ctx;
}

void SWELL_SetClipRegion(HDC ctx, const RECT *r)
{
  (void)ctx;
  (void)r;
}

void SWELL_PopClipRegion(HDC ctx)
{
  (void)ctx;
}

// ---------------------------------------------------------------------------
// System colors
// ---------------------------------------------------------------------------

int GetSysColor(int idx)
{
  switch (idx) {
    case COLOR_3DFACE:       return SkColorToNativeRGB(g_swell_ctheme._3dface);
    case COLOR_3DSHADOW:     return SkColorToNativeRGB(g_swell_ctheme._3dshadow);
    case COLOR_3DHILIGHT:    return SkColorToNativeRGB(g_swell_ctheme._3dhilight);
    case COLOR_3DDKSHADOW:   return SkColorToNativeRGB(g_swell_ctheme._3ddkshadow);
    case COLOR_BTNFACE:      return SkColorToNativeRGB(g_swell_ctheme._3dface);
    case COLOR_BTNTEXT:      return SkColorToNativeRGB(g_swell_ctheme.button_text);
    case COLOR_WINDOW:       return SkColorToNativeRGB(g_swell_ctheme.edit_bg);
    case COLOR_SCROLLBAR:    return SkColorToNativeRGB(g_swell_ctheme.scrollbar);
    case COLOR_INFOBK:       return SkColorToNativeRGB(g_swell_ctheme.info_bg);
    case COLOR_INFOTEXT:     return SkColorToNativeRGB(g_swell_ctheme.info_text);
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
  (void)ctx;
  return nullptr;
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
  if (g_swell_default_font_instance)
    return g_swell_default_font_instance;

  g_swell_default_font_instance = CreateFont(
      -g_swell_ctheme.default_font_size, 0, 0, 0, FW_NORMAL,
      0, 0, 0, 0, 0, 0, 0, 0, g_swell_deffont_face);

  g_swell_default_font = g_swell_default_font_instance;
  return g_swell_default_font_instance;
}

// ---------------------------------------------------------------------------
// Paint pipeline
// ---------------------------------------------------------------------------

void SWELL_internalSkiaPaint(HWND hwnd, SkCanvas *canvas,
    int bmout_xpos, int bmout_ypos, bool forceref)
{
  (void)hwnd; (void)canvas; (void)bmout_xpos; (void)bmout_ypos; (void)forceref;
}

// ---------------------------------------------------------------------------
// swell_colortheme constructor — default light theme values
// ---------------------------------------------------------------------------

swell_colortheme::swell_colortheme()
{
  // 3D / chrome colors
  _3dface       = 0xFFD4D0C8;
  _3dshadow     = 0xFF808080;
  _3dhilight    = 0xFFFFFFFF;
  _3ddkshadow   = 0xFF404040;

  // Button
  button_bg              = 0xFFD4D0C8;
  button_text            = 0xFF000000;
  button_text_disabled   = 0xFF808080;
  button_shadow          = 0xFF808080;
  button_hilight         = 0xFFFFFFFF;

  // Checkbox
  checkbox_bg            = 0xFFD4D0C8;
  checkbox_text          = 0xFF000000;
  checkbox_text_disabled = 0xFF808080;

  // Scrollbar
  scrollbar    = 0xFFD4D0C8;
  scrollbar_fg = 0xFF808080;
  scrollbar_bg = 0xFFD4D0C8;

  // Edit
  edit_bg      = 0xFFFFFFFF;
  edit_text    = 0xFF000000;
  edit_text_sel = 0xFFFFFFFF;
  edit_bg_sel  = 0xFF000080;
  edit_cursor  = 0xFF000000;

  // Info tip
  info_bg   = 0xFFFFFFE1;
  info_text = 0xFF000000;

  // Menu
  menu_bg          = 0xFFD4D0C8;
  menu_text        = 0xFF000000;
  menu_hilight_bg  = 0xFF000080;
  menu_hilight_text = 0xFFFFFFFF;

  // Menubar
  menubar_bg          = 0xFFD4D0C8;
  menubar_text        = 0xFF000000;
  menubar_hilight_bg  = 0xFF000080;
  menubar_hilight_text = 0xFFFFFFFF;
  menubar_height      = 20;

  // Trackbar
  trackbar_bg    = 0xFFD4D0C8;
  trackbar_fg    = 0xFF808080;
  trackbar_thumb = 0xFFD4D0C8;

  // Progress
  progress = 0xFF000080;

  // Label
  label_text = 0xFF000000;

  // Combo
  combo_bg   = 0xFFFFFFFF;
  combo_text = 0xFF000000;

  // ListView
  listview_bg          = 0xFFFFFFFF;
  listview_text        = 0xFF000000;
  listview_header_bg   = 0xFFD4D0C8;
  listview_header_text = 0xFF000000;

  // TreeView
  treeview_bg   = 0xFFFFFFFF;
  treeview_text = 0xFF000000;

  // Tab
  tab_bg       = 0xFFD4D0C8;
  tab_text     = 0xFF000000;
  tab_sel_bg   = 0xFFFFFFFF;
  tab_sel_text = 0xFF000000;

  // Focus rect
  focusrect     = 0xFF000000;
  focus_hilight = 0xFFC0C0C0;

  // Group box
  group_bg   = 0xFFD4D0C8;
  group_text = 0xFF000000;

  // Font
  default_font_size = 12;
}
