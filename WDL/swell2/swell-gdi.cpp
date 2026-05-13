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
#include <mutex>

#include <core/SkPath.h>
#include <core/SkRRect.h>
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkBlendMode.h>

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
  fprintf(stderr, "SWELL_CALL: SWELL_GDP_CTX_NEW\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_GDP_CTX_DELETE\n");
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
  fprintf(stderr, "SWELL_CALL: GDP_OBJECT_NEW\n");
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
  fprintf(stderr, "SWELL_CALL: GDP_OBJECT_DELETE\n");
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
  fprintf(stderr, "SWELL_CALL: HGDIOBJ_VALID\n");
  if (!p) return false;
  if (p->_infreelist) return false;
  if (reinterpret_cast<INT_PTR>(p) < 256) return false; // sentinels
  if (reqType && p->type != reqType) return false;
  return true;
}

bool HDC_VALID(HDC__ *ct)
{
  fprintf(stderr, "SWELL_CALL: HDC_VALID\n");
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
  fprintf(stderr, "SWELL_CALL: swell_DirtyContext\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_CreateMemContext\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_DeleteGfxContext\n");
  if (!ctx || !HDC_VALID(ctx)) return;
  ctx->surface.reset();
  ctx->canvas = nullptr;
  SWELL_GDP_CTX_DELETE(ctx);
}

HDC BeginPaint(HWND hwnd, PAINTSTRUCT *ps)
{
  fprintf(stderr, "SWELL_CALL: BeginPaint\n");
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
  fprintf(stderr, "SWELL_CALL: EndPaint\n");
  (void)hwnd;
  (void)ps;
  return TRUE;
}

HDC GetDC(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: GetDC\n");
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
  fprintf(stderr, "SWELL_CALL: GetWindowDC\n");
  return GetDC(hwnd);
}

void ReleaseDC(HWND hwnd, HDC ctx)
{
  fprintf(stderr, "SWELL_CALL: ReleaseDC\n");
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
  fprintf(stderr, "SWELL_CALL: CreatePen\n");
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
  fprintf(stderr, "SWELL_CALL: CreatePenAlpha\n");
  HPEN p = CreatePen(attr, wid, col);
  if (p) p->alpha = alpha;
  return p;
}

HBRUSH CreateSolidBrush(int col)
{
  fprintf(stderr, "SWELL_CALL: CreateSolidBrush\n");
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
  fprintf(stderr, "SWELL_CALL: CreateSolidBrushAlpha\n");
  HBRUSH br = CreateSolidBrush(col);
  if (br) br->alpha = alpha;
  return br;
}

HFONT CreateFont(int lfHeight, int lfWidth, int lfEscapement, int lfOrientation,
                 int lfWeight, char lfItalic, char lfUnderline, char lfStrikeOut,
                 char lfCharSet, char lfOutPrecision, char lfClipPrecision,
                 char lfQuality, char lfPitchAndFamily, const char *lfFaceName)
{
  fprintf(stderr, "SWELL_CALL: CreateFont\n");
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
  fprintf(stderr, "SWELL_CALL: CreateFontIndirect\n");
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
  fprintf(stderr, "SWELL_CALL: CreateBitmap\n");
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
  fprintf(stderr, "SWELL_CALL: CreateIconIndirect\n");
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
  fprintf(stderr, "SWELL_CALL: LoadNamedImage\n");
  (void)name;
  (void)alphaFromMask;
  return nullptr;
}

HGDIOBJ SelectObject(HDC ctx, HGDIOBJ pen)
{
  fprintf(stderr, "SWELL_CALL: SelectObject\n");
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
  fprintf(stderr, "SWELL_CALL: DeleteObject\n");
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
  fprintf(stderr, "SWELL_CALL: GetStockObject\n");
  if (wh == NULL_PEN)  return &g_null_pen_obj;
  if (wh == NULL_BRUSH) return &g_null_brush_obj;
  return nullptr;
}

HGDIOBJ SWELL_CloneGDIObject(HGDIOBJ a)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CloneGDIObject\n");
  if (!a || !HGDIOBJ_VALID(a)) return nullptr;
  a->additional_refcnt++;
  return a;
}

BOOL GetObject(HICON icon, int bmsz, void *_bm)
{
  fprintf(stderr, "SWELL_CALL: GetObject\n");
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
  fprintf(stderr, "SWELL_CALL: Rectangle\n");
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
    strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
    strokePaint.setAntiAlias(true);
    ctx->canvas->drawRect(rect, strokePaint);
  }
}

void Ellipse(HDC ctx, int l, int t, int r, int b)
{
  fprintf(stderr, "SWELL_CALL: Ellipse\n");
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
    strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
    strokePaint.setAntiAlias(true);
    ctx->canvas->drawOval(rect, strokePaint);
  }
}

void RoundRect(HDC ctx, int x, int y, int x2, int y2, int xrnd, int yrnd)
{
  fprintf(stderr, "SWELL_CALL: RoundRect\n");
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  if (!brush_valid(ctx) && !pen_valid(ctx)) return;
  swell_DirtyContext(ctx, x, y, x2, y2);

  SkRect rect = SkRect::MakeLTRB((float)x, (float)y, (float)x2, (float)y2);
  SkRRect rr = SkRRect::MakeRectXY(rect, (float)xrnd, (float)yrnd);

  if (brush_valid(ctx)) {
    SkPaint fillPaint;
    fillPaint.setStyle(SkPaint::kFill_Style);
    fillPaint.setColor(ctx->curbrush->color);
    fillPaint.setAlphaf(ctx->curbrush->alpha);
    fillPaint.setAntiAlias(true);
    ctx->canvas->drawRRect(rr, fillPaint);
  }

  if (pen_valid(ctx)) {
    SkPaint strokePaint;
    strokePaint.setStyle(SkPaint::kStroke_Style);
    strokePaint.setColor(ctx->curpen->color);
    strokePaint.setAlphaf(ctx->curpen->alpha);
    strokePaint.setStrokeWidth(ctx->curpen->wid > 0 ? (float)ctx->curpen->wid : 1.0f);
    strokePaint.setAntiAlias(true);
    ctx->canvas->drawRRect(rr, strokePaint);
  }
}

void SWELL_FillRect(HDC ctx, const RECT *r, HBRUSH br)
{
  fprintf(stderr, "SWELL_CALL: SWELL_FillRect\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_Polygon\n");
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
  fprintf(stderr, "SWELL_CALL: MoveToEx\n");
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
  fprintf(stderr, "SWELL_CALL: LineTo\n");
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
  swell_DirtyContext(ctx, l, t, r2, b2);

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
  fprintf(stderr, "SWELL_CALL: SetPixel\n");
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  swell_DirtyContext(ctx, x, y, x + 1, y + 1);

  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(SWELL_TO_SKCOLOR(c, 255));
  ctx->canvas->drawPoint((float)x + 0.5f, (float)y + 0.5f, paint);
}

void PolyBezierTo(HDC ctx, POINT *pts, int np)
{
  fprintf(stderr, "SWELL_CALL: PolyBezierTo\n");
  if (!HDC_VALID(ctx) || !ctx->canvas || !pts || np < 1) return;
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
  fprintf(stderr, "SWELL_CALL: PolyPolyline\n");
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
  fprintf(stderr, "SWELL_CALL: BitBlt\n");
  if (!HDC_VALID(hdcOut) || !hdcOut->canvas || !HDC_VALID(hdcIn) || !hdcIn->canvas) return;
  if (w <= 0 || h <= 0) return;

  swell_DirtyContext(hdcOut, x, y, x + w, y + h);

  SkSurface *srcSurf = hdcIn->canvas->getSurface();
  if (!srcSurf) return;

  sk_sp<SkImage> img = srcSurf->makeImageSnapshot();
  if (!img) return;

  SkRect srcRect = SkRect::MakeXYWH((float)xin, (float)yin, (float)w, (float)h);
  SkRect dstRect = SkRect::MakeXYWH((float)x, (float)y, (float)w, (float)h);

  SkPaint paint;
  if (mode == SRCCOPY) {
    paint.setBlendMode(SkBlendMode::kSrc);
  }

  hdcOut->canvas->drawImageRect(img, srcRect, dstRect, SkSamplingOptions(),
                                &paint, SkCanvas::kStrict_SrcRectConstraint);
}

void StretchBlt(HDC hdcOut, int x, int y, int w, int h,
                HDC hdcIn, int xin, int yin, int srcw, int srch, int mode)
{
  fprintf(stderr, "SWELL_CALL: StretchBlt\n");
  if (!HDC_VALID(hdcOut) || !hdcOut->canvas || !HDC_VALID(hdcIn) || !hdcIn->canvas) return;
  if (w <= 0 || h <= 0 || srcw <= 0 || srch <= 0) return;

  swell_DirtyContext(hdcOut, x, y, x + w, y + h);

  SkSurface *srcSurf = hdcIn->canvas->getSurface();
  if (!srcSurf) return;

  sk_sp<SkImage> img = srcSurf->makeImageSnapshot();
  if (!img) return;

  SkRect srcRect = SkRect::MakeXYWH((float)xin, (float)yin, (float)srcw, (float)srch);
  SkRect dstRect = SkRect::MakeXYWH((float)x, (float)y, (float)w, (float)h);

  SkPaint paint;
  if (mode == SRCCOPY) {
    paint.setBlendMode(SkBlendMode::kSrc);
  }

  hdcOut->canvas->drawImageRect(img, srcRect, dstRect, SkSamplingOptions(),
                                &paint, SkCanvas::kStrict_SrcRectConstraint);
}

#ifndef SWELL_TARGET_OSX
void StretchBltFromMem(HDC hdcOut, int x, int y, int w, int h,
                       const void *bits, int srcw, int srch, int srcspan)
{
  fprintf(stderr, "SWELL_CALL: StretchBltFromMem\n");
  if (!HDC_VALID(hdcOut) || !hdcOut->canvas || !bits) return;
  if (w <= 0 || h <= 0 || srcw <= 0 || srch <= 0) return;

  swell_DirtyContext(hdcOut, x, y, x + w, y + h);

  SkBitmap srcBm;
  srcBm.allocN32Pixels(srcw, srch);
  const uint8_t *src = static_cast<const uint8_t *>(bits);
  uint32_t *dst = srcBm.getAddr32(0, 0);
  for (int row = 0; row < srch; row++) {
    memcpy(dst, src, srcw * 4);
    src += srcspan;
    dst += srcw;
  }

  sk_sp<SkImage> img = SkImages::RasterFromBitmap(srcBm);
  if (!img) return;

  SkRect dstRect = SkRect::MakeXYWH((float)x, (float)y, (float)w, (float)h);

  hdcOut->canvas->drawImageRect(img, dstRect, SkSamplingOptions());
}

int SWELL_GetScaling256(void)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetScaling256\n");
  return g_swell_ui_scale;
}
#endif

void DrawImageInRect(HDC ctx, HICON img, const RECT *r)
{
  fprintf(stderr, "SWELL_CALL: DrawImageInRect\n");
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
  fprintf(stderr, "SWELL_CALL: SetTextColor\n");
  if (!HDC_VALID(ctx)) return;
  ctx->cur_text_color_int = SWELL_TO_SKCOLOR(col, 255);
}

int GetTextColor(HDC ctx)
{
  fprintf(stderr, "SWELL_CALL: GetTextColor\n");
  if (!HDC_VALID(ctx)) return 0;
  return SkColorToNativeRGB(ctx->cur_text_color_int);
}

void SetBkColor(HDC ctx, int col)
{
  fprintf(stderr, "SWELL_CALL: SetBkColor\n");
  if (!HDC_VALID(ctx)) return;
  ctx->curbkcol = SWELL_TO_SKCOLOR(col, 255);
}

void SetBkMode(HDC ctx, int col)
{
  fprintf(stderr, "SWELL_CALL: SetBkMode\n");
  if (!HDC_VALID(ctx)) return;
  ctx->curbkmode = col;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

int SWELL_DrawText(HDC ctx, const char *buf, int len, RECT *r, int align)
{
  fprintf(stderr, "SWELL_CALL: SWELL_DrawText\n");
  if (!HDC_VALID(ctx) || !r) return 0;

  if (len == -1) len = (int)strlen(buf);
  if (len <= 0 || !buf) return 0;

  // Build SkFont from selected font or default
  SkFont font;
  float fontSize = 12.0f;

  if (HGDIOBJ_VALID(ctx->curfont, TYPE_FONT)) {
    LOGFONT *lf = static_cast<LOGFONT *>(ctx->curfont->typedata);
    if (lf) {
      fontSize = lf->lfHeight < 0 ? (float)(-lf->lfHeight) : (float)lf->lfHeight;
    }
  }
  font.setSize(fontSize);

  // Measure text
  SkRect bounds;
  font.measureText(buf, len, SkTextEncoding::kUTF8, &bounds);

  int textW = (int)(bounds.width() + 0.5f);
  int textH = (int)(bounds.height() + 0.5f);

  if (align & DT_CALCRECT) {
    r->right = r->left + textW;
    r->bottom = r->top + textH;
    return textH;
  }

  if (!ctx->canvas) return textH;

  // Determine text position
  float x = (float)r->left;
  float y = (float)r->top;

  if (align & DT_CENTER)
    x = (float)(r->left + (r->right - r->left - textW) / 2);
  else if (!(align & DT_LEFT))
    x = (float)r->left; // DT_LEFT is 0; treat absent as left

  if (align & DT_VCENTER)
    y = (float)(r->top + (r->bottom - r->top - textH) / 2);

  // Draw background if opaque
  if (ctx->curbkmode == OPAQUE) {
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

  ctx->canvas->drawString(buf, x, y, font, textPaint);

  swell_DirtyContext(ctx, (int)x, (int)y, (int)(x + textW), (int)(y + textH));

  return textH;
}

BOOL GetTextMetrics(HDC ctx, TEXTMETRIC *tm)
{
  fprintf(stderr, "SWELL_CALL: GetTextMetrics\n");
  if (!HDC_VALID(ctx) || !tm) return FALSE;

  memset(tm, 0, sizeof(TEXTMETRIC));

  float fontSize = 12.0f;
  if (HGDIOBJ_VALID(ctx->curfont, TYPE_FONT)) {
    LOGFONT *lf = static_cast<LOGFONT *>(ctx->curfont->typedata);
    if (lf) {
      fontSize = lf->lfHeight < 0 ? (float)(-lf->lfHeight) : (float)lf->lfHeight;
    }
  }

  SkFont font;
  font.setSize(fontSize);

  SkFontMetrics metrics;
  font.getMetrics(&metrics);

  tm->tmAscent = (int)(-metrics.fAscent + 0.5f);
  tm->tmDescent = (int)(metrics.fDescent + 0.5f);
  tm->tmHeight = tm->tmAscent + tm->tmDescent;
  tm->tmInternalLeading = (int)(metrics.fLeading + 0.5f);
  tm->tmAveCharWidth = (int)(fontSize * 0.5f + 0.5f);

  return TRUE;
}

int GetTextFace(HDC ctx, int nCount, LPTSTR lpFaceName)
{
  fprintf(stderr, "SWELL_CALL: GetTextFace\n");
  (void)ctx;
  if (lpFaceName && nCount > 0) lpFaceName[0] = 0;
  return 0;
}

int GetGlyphIndicesW(HDC ctx, wchar_t *buf, int len, unsigned short *indices,
                     int flags)
{
  fprintf(stderr, "SWELL_CALL: GetGlyphIndicesW\n");
  (void)ctx; (void)buf; (void)len; (void)indices; (void)flags;
  return 0;
}

// ---------------------------------------------------------------------------
// Clip region
// ---------------------------------------------------------------------------

void SWELL_PushClipRegion(HDC ctx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_PushClipRegion\n");
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  ctx->clip_save_count = ctx->canvas->save();
}

void SWELL_SetClipRegion(HDC ctx, const RECT *r)
{
  fprintf(stderr, "SWELL_CALL: SWELL_SetClipRegion\n");
  if (!HDC_VALID(ctx) || !ctx->canvas || !r) return;
  ctx->canvas->clipRect(
    SkRect::MakeLTRB((float)r->left, (float)r->top,
                      (float)r->right, (float)r->bottom));
}

void SWELL_PopClipRegion(HDC ctx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_PopClipRegion\n");
  if (!HDC_VALID(ctx) || !ctx->canvas) return;
  ctx->canvas->restoreToCount(ctx->clip_save_count);
}

// ---------------------------------------------------------------------------
// System colors
// ---------------------------------------------------------------------------

int GetSysColor(int idx)
{
  fprintf(stderr, "SWELL_CALL: GetSysColor\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_GetCtxGC\n");
  (void)ctx;
  return nullptr;
}

void *SWELL_GetCtxFrameBuffer(HDC ctx)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetCtxFrameBuffer\n");
  if (!ctx || !HDC_VALID(ctx)) return nullptr;
  HDC__ *ct = (HDC__*)ctx;
  if (!ct->surface) return nullptr;
  SkPixmap pm;
  if (ct->surface->peekPixels(&pm))
    return const_cast<void*>(pm.addr());
  return nullptr;
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

int AddFontResourceEx(LPCTSTR str, DWORD fl, void *pdv)
{
  fprintf(stderr, "SWELL_CALL: AddFontResourceEx\n");
  (void)str; (void)fl; (void)pdv;
  return 0;
}

HFONT SWELL_GetDefaultFont()
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetDefaultFont\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_internalSkiaPaint\n");
  if (!hwnd) return;

  if (hwnd->m_invalidated)
    forceref = true;

  if (forceref || hwnd->m_child_invalidated) {
    swell_gdpLocalContext ctx_local;
    memset(&ctx_local, 0, sizeof(ctx_local));
    ctx_local.ctx.canvas = canvas;
    ctx_local.ctx.dirty_rect_valid = false;
    ctx_local.ctx.surface_offs.x = bmout_xpos;
    ctx_local.ctx.surface_offs.y = bmout_ypos;
    ctx_local.ctx.clip_save_count = 0;
    ctx_local.ctx.curpen = nullptr;
    ctx_local.ctx.curbrush = nullptr;
    ctx_local.ctx.curfont = nullptr;
    ctx_local.ctx.cur_text_color_int = SK_ColorBLACK;
    ctx_local.ctx.curbkmode = TRANSPARENT;
    ctx_local.ctx.curbkcol = 0;
    ctx_local.ctx.lastpos_x = 0.0f;
    ctx_local.ctx.lastpos_y = 0.0f;

    ctx_local.clipr = hwnd->m_position;
    RECT ncr = hwnd->m_position;

    hwnd->m_paintctx = &ctx_local;

    // WM_NCCALCSIZE for client inset
    SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncr);

    if (forceref) {
      SendMessage(hwnd, WM_NCPAINT, 0, 0);
    }

    // Adjust surface_offs and clipr by NC inset
    ctx_local.ctx.surface_offs.x += (ncr.left - hwnd->m_position.left);
    ctx_local.ctx.surface_offs.y += (ncr.top - hwnd->m_position.top);
    ctx_local.clipr = ncr;

    ctx_local.ctx.curfont = hwnd->m_font;

    if (forceref) {
      SendMessage(hwnd, WM_PAINT, (WPARAM)&ctx_local, 0);
    }

    hwnd->m_paintctx = nullptr;
    hwnd->m_invalidated = false;
  }

  // Recurse into visible children
  for (int i = 0; i < hwnd->m_children.GetSize(); i++) {
    HWND child = hwnd->m_children.Get(i);
    if (!child || !child->m_visible) continue;

    int saveCount = canvas ? canvas->save() : 0;

    if (canvas) {
      RECT cr = child->m_position;
      canvas->clipRect(
        SkRect::MakeLTRB((float)cr.left, (float)cr.top,
                          (float)cr.right, (float)cr.bottom));
      canvas->translate((float)cr.left, (float)cr.top);
    }

    SWELL_internalSkiaPaint(child, canvas,
      bmout_xpos + child->m_position.left,
      bmout_ypos + child->m_position.top,
      forceref);

    if (canvas) canvas->restoreToCount(saveCount);
  }
}

// ---------------------------------------------------------------------------
// SWELL_FillDialogBackground
// ---------------------------------------------------------------------------

void SWELL_FillDialogBackground(HDC hdc, const RECT *r, int level)
{
  fprintf(stderr, "SWELL_CALL: SWELL_FillDialogBackground\n");
  (void)level;
  if (!HDC_VALID(hdc) || !hdc->canvas || !r) return;

  SkPaint paint;
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(g_swell_ctheme._3dface);

  hdc->canvas->drawRect(
    SkRect::MakeLTRB((float)r->left, (float)r->top,
                      (float)r->right, (float)r->bottom), paint);
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
