# SWELL GDI Rendering Model

This document specifies the GDI subsystem implementation: HDC state machine,
HGDIOBJ internals, Skia integration, paint pipeline, and font system. It is
required reading before implementing any drawing function.

---

## 1. Object Type System

### 1.1 HGDIOBJ__ fields (Skia backend)

```c
struct HGDIOBJ__ {
  int   type;               // TYPE_PEN=1, TYPE_BRUSH=2, TYPE_FONT=3, TYPE_BITMAP=4
  int   additional_refcnt;  // 0=single owner; >0 = that many additional owners
  int   color;              // SkColor format (0xAARRGGBB)
  int   wid;                // pen: stroke width (pixels); -1=null/no-op
                            // brush: 0=filled, -1=null/no-op
  float alpha;              // pen/brush: opacity 0.0–1.0
  void *typedata;           // font: FT_Face; bitmap: SkBitmap*
  bool  _infreelist;        // object is in the free pool, not in use
  struct HGDIOBJ__ *_next;  // free-list chain pointer
};
```

Type constants:
```c
#define TYPE_PEN    1
#define TYPE_BRUSH  2
#define TYPE_FONT   3
#define TYPE_BITMAP 4
```

### 1.2 Null/stock objects

`GetStockObject(NULL_PEN)` returns a static `HGDIOBJ__` with `type=TYPE_PEN, wid=-1`.  
`GetStockObject(NULL_BRUSH)` returns a static `HGDIOBJ__` with `type=TYPE_BRUSH, wid=-1`.

`wid < 0` means "no-op" for all drawing operations. A `NULL_PEN` suppresses outline
drawing; a `NULL_BRUSH` suppresses fill drawing. This is the primary mechanism for
drawing filled shapes with no outline or outlined shapes with no fill.

GetStockObject returns a pointer to a static struct — never free these objects.

### 1.3 Reference counting

`additional_refcnt == 0` means exactly one owner.  
Each call to `SWELL_CloneGDIObject` increments `additional_refcnt`.  
`DeleteObject` decrements; frees when refcnt goes below 0.  
Objects in the free pool (`_infreelist==true`) must not be used.

---

## 2. HDC__ State Machine

### 2.1 Fields

```c
struct HDC__ {
  SkCanvas    *canvas;          // drawing target; not owned (owned by surface below)
  sk_sp<SkSurface> surface;     // backing surface (non-null when context owns its pixel buffer)
  POINT surface_offs;           // offset: drawing coord (x,y) maps to canvas pixel (x+surface_offs.x, y+surface_offs.y)

  RECT  dirty_rect;             // union of all drawn areas, in surface coordinates
  bool  dirty_rect_valid;       // false = no draws yet; true = dirty_rect is valid

  int   clip_save_count;        // canvas save depth at SWELL_PushClipRgn; restored on Pop

  HGDIOBJ__ *curpen;            // selected pen; NULL = no pen selected
  HGDIOBJ__ *curbrush;          // selected brush; NULL = no brush selected
  HGDIOBJ__ *curfont;           // selected font; NULL = use SWELL default font

  int   cur_text_color_int;     // text color as SkColor (0xAARRGGBB)
  int   curbkcol;               // background color as SkColor
  int   curbkmode;              // TRANSPARENT(0) or OPAQUE(1)
  float lastpos_x, lastpos_y;   // current position (set by MoveToEx, updated by LineTo)

  bool  _infreelist;
  struct HDC__ *_next;
};
```

### 2.2 Default state after SWELL_CreateMemContext

| Field | Default |
|---|---|
| `surface` | `SkSurface::MakeRasterN32Premul(w, h)`, cleared to `SK_ColorTRANSPARENT` |
| `surface_offs` | {0, 0} |
| `dirty_rect_valid` | false |
| `curpen` | NULL (no pen selected) |
| `curbrush` | NULL (no brush selected) |
| `curfont` | NULL (use SWELL_GetDefaultFont()) |
| `cur_text_color_int` | `SK_ColorBLACK` (0xFF000000) — black, full alpha |
| `curbkcol` | uninitialized (SetBkColor not called; do not rely on value) |
| `curbkmode` | TRANSPARENT(0) — default |
| `lastpos_x/y` | 0.0, 0.0 |

After `BeginPaint` / `GetDC`: curfont is set to `hwnd->m_font` (the window's stored font).

### 2.3 Skia color format

SWELL internally stores colors as `SkColor` (0xAARRGGBB), not native RGB:

```c
SWELL_TO_SKCOLOR(col, alpha=255)
```

Converts native SWELL RGB (`r<<16|g<<8|b` or Win32 `b<<16|g<<8|r` depending on
`SWELL_USE_WIN32_RGB`) to Skia's `SkColorSetARGB(a, r, g, b)` packed format.

**All color fields in HDC__ are SkColor (0xAARRGGBB), not native RGB.**  
`SetTextColor(ctx, col)` — `col` is native RGB; converted to SkColor on store.  
`SetBkColor(ctx, col)` — same.

---

## 3. SelectObject Semantics

```c
HGDIOBJ SelectObject(HDC ctx, HGDIOBJ pen);
```

Type of object to select determined by `pen->type`. Object is installed into the
matching slot (`curpen`, `curbrush`, or `curfont`).

Return value (the previously selected object):
- If nothing was previously in the slot (`*mod == NULL`): returns the sentinel
  `(HGDIOBJ)(INT_PTR)type` (i.e., `(HGDIOBJ)TYPE_PEN`, `(HGDIOBJ)TYPE_BRUSH`, or
  `(HGDIOBJ)TYPE_FONT`). These sentinel values are low integers (1, 2, 3) and are
  NOT valid HGDIOBJ pointers.
- If something was selected: returns the old HGDIOBJ.

Passing the sentinel back into SelectObject (to restore "nothing") works:
```c
if (p == (HGDIOBJ__)TYPE_PEN) mod = &c->curpen; // deselect pen
...
HGDIOBJ__ *np = *mod;
*mod = 0;
return np ? np : p;  // if was NULL, return sentinel
```

Passing a TYPE_BITMAP object into SelectObject returns 0 (not supported for selection).

**Never call DeleteObject on a sentinel value.**

---

## 4. Drawing Operations

### 4.1 Pen behavior

- `wid == 0`: hairline (1 pixel wide)
- `wid > 0`: wider stroke (platform-dependent rendering)
- `wid < 0`: NULL pen — drawing op skipped entirely

Drawing operations that use the pen (`LineTo`, outline of `Rectangle`, `Ellipse`,
`RoundRect`, `PolyPolyline`, `PolyBezierTo`):
- Check `HGDIOBJ_VALID(c->curpen, TYPE_PEN) && c->curpen->wid >= 0` before drawing.
- Use `c->curpen->color` (SkColor) and `c->curpen->alpha` for color.

### 4.2 Brush behavior

Fill operations (`FillRect`, `SWELL_FillRect`, fill of `Rectangle`, `Ellipse`,
`RoundRect`, `SWELL_Polygon`):
- Check `HGDIOBJ_VALID(c->curbrush, TYPE_BRUSH) && c->curbrush->wid >= 0`.
- `wid < 0` (NULL_BRUSH): fill skipped.

### 4.3 Current position (LineTo, MoveToEx)

```c
MoveToEx(ctx, x, y, &oldpt)  // sets lastpos_x/y; optionally returns old pos
LineTo(ctx, x, y)             // draws from lastpos to (x,y); updates lastpos
PolyBezierTo(ctx, pts, n)     // draws bezier from lastpos; updates lastpos
```

`LineTo` calls `canvas->drawLine()` with an `SkPaint` built from `c->curpen->color/alpha/wid`.

### 4.4 Rectangle

Draws filled rect (with brush) then outline (with pen):
```c
SkPaint fill; fill.setStyle(SkPaint::kFill_Style); fill.setColor(brush->color); fill.setAlphaf(brush->alpha);
canvas->drawRect(SkRect::MakeLTRB(l, t, r, b), fill);

SkPaint stroke; stroke.setStyle(SkPaint::kStroke_Style); stroke.setColor(pen->color); stroke.setAlphaf(pen->alpha); stroke.setStrokeWidth(pen->wid);
canvas->drawRect(SkRect::MakeLTRB(l, t, r-1, b-1), stroke);
```
Note: stroke rect is one pixel smaller on each edge than fill rect (standard Win32
convention: right/bottom are exclusive for fill, inclusive for outline).

### 4.5 BitBlt / StretchBlt modes

```c
mode = SRCCOPY (0)              → SkBlendMode::kSrc   (replace destination)
mode = SRCCOPY_USEALPHACHAN     → SkBlendMode::kSrcOver (alpha compositing)
```

### 4.6 Dirty rect tracking

Each drawing operation that touches the surface calls `swell_DirtyContext`:
- Expands `dirty_rect` to include the drawn area.
- Used by `ReleaseDC` and `GetDC`/`GetWindowDC` to know which screen region to update.

---

## 5. Clip Region

SWELL's Skia backend supports exactly **one level** of clip stack:

```c
SWELL_PushClipRgn(ctx)         // canvas->save(); records save count in clip_save_count
SWELL_SetClipRegion(ctx, &r)   // canvas->clipRect(r)
SWELL_PopClipRegion(ctx)       // canvas->restoreToCount(clip_save_count)
```

Implementation:
- `clip_save_count` stores the canvas save depth before the push.
- `SWELL_PushClipRgn` calls `canvas->save()` and records the depth.
- `SWELL_SetClipRegion` intersects the current clip with `r` via `canvas->clipRect`.
- `SWELL_PopClipRegion` restores to `clip_save_count` via `canvas->restoreToCount`.
- Only one push is supported; nested pushes overwrite the saved count.

**Do not push twice without popping.**

---

## 6. Paint Pipeline

### 6.1 Trigger

`InvalidateRect(hwnd, rect, eraseBk)`:
- Sets `hwnd->m_invalidated = true`.
- Sets `parent->m_child_invalidated = true` for each ancestor.
- Calls `swell_oswindow_invalidate(topLevel, &screenRect)` — tells the OS to
  schedule a redraw of the OS window.

### 6.2 Paint entry (SDL3 backend: window expose event)

On `SDL_EVENT_WINDOW_EXPOSED` callback from SDL3, the backend calls `SWELL_internalSkiaPaint`:

```
SWELL_internalSkiaPaint(hwnd, bmout, bmout_xpos, bmout_ypos, forceref)
```

Steps:
1. If `hwnd->m_invalidated`, set `forceref=true`.
2. If `forceref` or `hwnd->m_child_invalidated`:
   a. Create a `swell_gdpLocalContext ctx` pointing into `bmout` at `(bmout_xpos, bmout_ypos)`.
   b. Set `hwnd->m_paintctx = &ctx`.
   c. Call `hwnd->m_wndproc(hwnd, WM_NCCALCSIZE, FALSE, &p)` to compute client inset.
   d. If `forceref`: call `hwnd->m_wndproc(hwnd, WM_NCPAINT, 1, 0)` for non-client area.
   e. Adjust `ctx.surface_offs` and `ctx.clipr` by the NC inset (dx, dy).
   f. Set `ctx.ctx.curfont = hwnd->m_font`.
   g. If `forceref` and clip rect non-empty: call `hwnd->m_wndproc(hwnd, WM_PAINT, (WPARAM)&ctx, 0)`.
   h. Clear `hwnd->m_paintctx`; set `hwnd->m_invalidated = false`.
3. Recurse into visible children (each in a canvas save/clip/translate layer).

### 6.3 BeginPaint / EndPaint (Skia backend)

`BeginPaint(hwnd, &ps)`:
- Returns `&hwnd->m_paintctx->ctx` as the HDC.
- Sets `ps.rcPaint = ctx->clipr` (the dirty clip rect in client coords).
- Window proc draws into this HDC.

`EndPaint(hwnd, &ps)`:
- No-op in the Skia backend (screen update is driven by `swell_oswindow_updatetoscreen`,
  called after the full paint tree completes).

`GetDC(hwnd)` / `GetWindowDC(hwnd)`:
- Creates a new `swell_gdpLocalContext` with a canvas derived from the window's
  backing surface via `canvas->save()` + clip to the window rect.
- `ReleaseDC` flushes dirty region to screen via `swell_oswindow_updatetoscreen`.

### 6.4 Screen update

`swell_oswindow_updatetoscreen(hwnd, &rect)`:
- Reads Skia surface pixel data and copies to the OS window's pixel buffer.
- On SDL3: uploads via `SDL_UpdateTexture` and presents via `SDL_RenderPresent`.

---

## 7. Font System (FreeType backend)

### 7.1 Initialization

`CreateFont` lazily initializes FreeType on first call:
```c
FT_Init_FreeType(&s_freetype)
// then either:
FcInitLoadConfigAndFonts()     // SWELL_FONTCONFIG defined
ScanFontDirectory("/usr/share/fonts")  // fallback
```

### 7.2 lfHeight interpretation

| Value | Meaning |
|---|---|
| `lfHeight > 0` | Cell height (ascender + descender) in pixels |
| `lfHeight < 0` | Character height (ascender only) in pixels; abs value used |
| `lfHeight == 0` | Use default size |

FreeType uses `FT_Set_Pixel_Sizes(face, 0, abs(lfHeight))`.

### 7.3 Font cache (LRU)

Fonts are cached in a list of up to `SWELL_FREETYPE_CACHE_SIZE` (default 80) entries.
Each entry (`fontConfigCacheEnt`) stores: name, flags, width, height, `FT_Face`, file path.

Cache hit requires: same name, same flags (`weight | italic<<30`), same `lfWidth`, same `lfHeight`.  
On hit: the entry is moved to the end of the list (most recent).  
On miss: creates new `FT_Face`, evicts oldest if cache full.

### 7.4 Font matching (fontconfig path)

```c
FcPatternCreate()
FcPatternAddString(pat, FC_FAMILY, lfFaceName)
FcPatternAddInteger(pat, FC_WEIGHT, ...)   // maps FW_* to FC_WEIGHT_*
FcPatternAddInteger(pat, FC_SLANT, FC_SLANT_ITALIC)  // if lfItalic
FcFontMatch(s_fontconfig, pat, &result)
// → opens the matched font file with FT_New_Face
```

### 7.5 Default font

```c
HFONT SWELL_GetDefaultFont()
// returns: CreateFont(-g_swell_ctheme.default_font_size, 0, 0, 0, FW_NORMAL, 0, 0, 0,
//                     0, 0, 0, 0, 0, g_swell_deffont_face)
```

`g_swell_deffont_face` is "Arial" by default (compile-time default).  
The default font is a module-level singleton, created lazily.

If no font is selected in the HDC (`curfont == NULL`), `SWELL_GetDefaultFont()` is used.

### 7.6 GetTextMetrics

```c
tm->tmAscent  = FreeType ascender (in pixels, scaled)
tm->tmDescent = -FreeType descender (in pixels, scaled)
tm->tmHeight  = tmAscent + tmDescent
tm->tmInternalLeading = tmHeight - face->size->metrics.y_ppem
tm->tmAveCharWidth    = face->size->metrics.x_ppem / 2  (approximation)
```

If no font/FreeType not available, fallback values:
```c
tmAscent=8, tmDescent=0, tmHeight=8, tmInternalLeading=0, tmAveCharWidth=4
```

### 7.7 DrawText / SWELL_DrawText

Text is rendered glyph-by-glyph using FreeType bitmaps, blended into the Skia canvas.

- `DT_CALCRECT`: measures without drawing; fills `r->right`/`r->bottom`.
- `DT_SINGLELINE` + `DT_VCENTER`: vertical centering.
- `DT_WORDBREAK`: wraps at spaces.
- `DT_END_ELLIPSIS`: truncates with "..." if too wide.
- `DT_NOPREFIX`: `&` characters are not treated as accelerator underline markers.

Text color from `cur_text_color_int`; background from `curbkcol`/`curbkmode`.

---

## 8. Bitmap Objects

`CreateBitmap(w, h, planes, bpp, bits)`:
- Creates `SkBitmap(w, h)` stored in `typedata`.
- If `bits != NULL`: copies pixel data in (format depends on `bpp`; 32bpp = BGRA or RGBA).

`BitBlt` / `StretchBlt`:
- Renders via `canvas->drawImage()` or `canvas->drawImageRect()`.
- Source HDC's `SkSurface` is snapshotted to `sk_sp<SkImage>` for drawing.

`DrawImageInRect(ctx, hicon, &r)`:
- `hicon` is `HGDIOBJ__` with `type=TYPE_BITMAP`.
- Scales the bitmap to fit `r` via `canvas->drawImageRect()`.

---

## 9. GDI Object Pooling

HDC and HGDIOBJ structs are allocated from free-lists (not direct `malloc`/`free`):

```c
HDC__     * SWELL_GDP_CTX_NEW()       // pop from HDC free list (max 100)
void        SWELL_GDP_CTX_DELETE(hdc) // push back
HGDIOBJ__* GDP_OBJECT_NEW()           // pop from HGDIOBJ free list (max 200)
void        GDP_OBJECT_DELETE(obj)    // push back
```

Free lists are protected by a mutex.  
Objects returned to the pool are reused — their fields must be re-initialized before use.  
Objects must not be used after being passed to `DeleteObject` / `SWELL_DeleteGfxContext`.

---

## 10. SWELL_CreateMemContext vs BeginPaint HDC

| Feature | SWELL_CreateMemContext | BeginPaint HDC |
|---|---|---|
| Owns surface | Yes (`sk_sp<SkSurface>`) | No (borrows from window) |
| Must free | `SWELL_DeleteGfxContext` | `EndPaint` |
| Initial color | transparent black | inherits window surface |
| curfont | NULL (default font used) | hwnd->m_font |
| Use for | off-screen rendering | in-paint drawing only |

---

*End of SWELL GDI Rendering Model*
