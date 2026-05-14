/*
  SWELL2 SDL3 OS backend — display, window management, event translation.
  Provides actual OS windows via SDL3 and routes SDL events to SWELL messages.
*/

#include "swell-internal.h"

#include <cstdio>
#include <cstring>

#ifdef SWELL_TARGET_SDL3

// ---------------------------------------------------------------------------
// SDL_WindowEntry: maps SDL_Window <-> HWND, holds renderer/texture
// ---------------------------------------------------------------------------

struct SDL_WindowEntry {
  SDL_Window *window;
  SDL_WindowID windowID;
  HWND hwnd;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  int tex_w, tex_h;
  SDL_WindowEntry *next;
};

static SDL_WindowEntry *g_sdl_windows = NULL;

static SDL_WindowEntry *find_entry_by_window(SDL_Window *w)
{
  for (SDL_WindowEntry *e = g_sdl_windows; e; e = e->next)
    if (e->window == w) return e;
  return NULL;
}

static SDL_WindowEntry *find_entry_by_hwnd(HWND hwnd)
{
  for (SDL_WindowEntry *e = g_sdl_windows; e; e = e->next)
    if (e->hwnd == hwnd) return e;
  return NULL;
}

static SDL_WindowEntry *find_entry_by_windowID(SDL_WindowID id)
{
  for (SDL_WindowEntry *e = g_sdl_windows; e; e = e->next)
    if (e->windowID == id) return e;
  return NULL;
}

static void add_entry(SDL_Window *w, HWND hwnd, SDL_Renderer *r)
{
  SDL_WindowEntry *e = new SDL_WindowEntry();
  e->window = w;
  e->windowID = SDL_GetWindowID(w);
  e->hwnd = hwnd;
  e->renderer = r;
  e->texture = NULL;
  e->tex_w = 0;
  e->tex_h = 0;
  e->next = g_sdl_windows;
  g_sdl_windows = e;
}

static void remove_entry(SDL_WindowEntry *e)
{
  if (!e) return;
  if (e->texture) SDL_DestroyTexture(e->texture);
  if (e->renderer) SDL_DestroyRenderer(e->renderer);

  SDL_WindowEntry **pp = &g_sdl_windows;
  while (*pp) {
    if (*pp == e) { *pp = e->next; break; }
    pp = &(*pp)->next;
  }
  delete e;
}

// ---------------------------------------------------------------------------
// swell_oswindow_manage: create SDL_Window for top-level HWND
// ---------------------------------------------------------------------------

void swell_oswindow_manage(HWND hwnd, bool wantFocus)
{
  if (!hwnd) return;

  // Destroy or create OS window as visibility requires.
  // Child windows don't get their own OS window.
  const bool haveOS = hwnd->m_oswindow != NULL;
  const bool wantOS = !hwnd->m_parent && hwnd->m_visible;

  if (!wantOS && haveOS)
  {
    swell_oswindow_destroy(hwnd);
    return;
  }

  if (!wantOS || haveOS) return;

  RECT pr = hwnd->m_position;
  int pw = pr.right - pr.left;
  int ph = pr.bottom - pr.top;
  if (pw < 1) pw = 400;
  if (ph < 1) ph = 300;

  // Convert physical → logical for SDL3
  int lw = swell_phys_to_log(pw);
  int lh = swell_phys_to_log(ph);
  int lx = swell_phys_to_log(pr.left);
  int ly = swell_phys_to_log(pr.top);
  if (lw < 1) lw = 1;
  if (lh < 1) lh = 1;

  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (hwnd->m_style & WS_THICKFRAME)
    flags |= SDL_WINDOW_RESIZABLE;
  if (!(hwnd->m_style & WS_CAPTION))
    flags |= SDL_WINDOW_BORDERLESS;
  if (!wantFocus)
    flags |= SDL_WINDOW_NOT_FOCUSABLE;
  flags |= SDL_WINDOW_HIDDEN; // show after position is set

  SDL_Window *sdlwin = SDL_CreateWindow(
      hwnd->m_title.Get(), lw, lh, flags);

  if (!sdlwin) {
    fprintf(stderr, "SWELL SDL3: SDL_CreateWindow failed: %s\n", SDL_GetError());
    return;
  }

  SDL_SetWindowPosition(sdlwin, lx, ly);

  SDL_ShowWindow(sdlwin);

  SDL_Renderer *rend = SDL_CreateRenderer(sdlwin, NULL);
  if (!rend) {
    fprintf(stderr, "SWELL SDL3: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(sdlwin);
    return;
  }

  add_entry(sdlwin, hwnd, rend);
  hwnd->m_oswindow = sdlwin;

  SDL_StartTextInput(sdlwin);

  // create backing Skia surface (pixel dimensions for HiDPI)
  int surf_w = 0, surf_h = 0;
  SDL_GetWindowSizeInPixels(sdlwin, &surf_w, &surf_h);
  if (surf_w < 1) surf_w = pw;
  if (surf_h < 1) surf_h = ph;
  hwnd->m_backingstore = SkSurfaces::Raster(
      SkImageInfo::Make(surf_w, surf_h, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
  if (hwnd->m_backingstore) {
    SkCanvas *c = hwnd->m_backingstore->getCanvas();
    if (c) c->clear(SK_ColorTRANSPARENT);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_destroy
// ---------------------------------------------------------------------------

void swell_oswindow_destroy(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;

  SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
  if (e) {
    SDL_Window *w = e->window;
    hwnd->m_oswindow = NULL;
    hwnd->m_backingstore.reset();
    remove_entry(e);
    SDL_DestroyWindow(w);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_resize
// ---------------------------------------------------------------------------

void swell_oswindow_resize(HWND hwnd, int reposflag, RECT *r)
{
  if (!hwnd || !hwnd->m_oswindow) return;

  SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
  if (!e) return;

  if (reposflag & 1) {
    int lx = swell_phys_to_log(r->left);
    int ly = swell_phys_to_log(r->top);
    SDL_SetWindowPosition(e->window, lx, ly);
  }
  if (reposflag & 2) {
    int pw = r->right - r->left;
    int ph = r->bottom - r->top;
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    int lw = swell_phys_to_log(pw);
    int lh = swell_phys_to_log(ph);
    if (lw < 1) lw = 1;
    if (lh < 1) lh = 1;
    SDL_SetWindowSize(e->window, lw, lh);

    // resize backing store to match new pixel dimensions, preserve old content
    int surf_w = 0, surf_h = 0;
    SDL_GetWindowSizeInPixels(e->window, &surf_w, &surf_h);
    if (surf_w > 0 && surf_h > 0) {
      sk_sp<SkImage> oldImage;
      if (hwnd->m_backingstore)
        oldImage = hwnd->m_backingstore->makeImageSnapshot();

      hwnd->m_backingstore = SkSurfaces::Raster(
          SkImageInfo::Make(surf_w, surf_h, kBGRA_8888_SkColorType, kPremul_SkAlphaType));

      if (hwnd->m_backingstore) {
        SkCanvas *c = hwnd->m_backingstore->getCanvas();
        if (c) {
          c->clear(SK_ColorTRANSPARENT);
          if (oldImage) c->drawImage(oldImage, 0, 0);
        }
      }
    }

    // destroy stale texture
    if (e->texture) {
      SDL_DestroyTexture(e->texture);
      e->texture = NULL;
      e->tex_w = 0;
      e->tex_h = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_focus
// ---------------------------------------------------------------------------

void swell_oswindow_focus(HWND hwnd)
{
  if (!hwnd) {
    g_swell_focused_oswindow_hwnd = NULL;
    return;
  }

  HWND top = hwnd;
  while (top->m_parent) top = (HWND)top->m_parent;

  if (top->m_oswindow) {
    g_swell_focused_oswindow_hwnd = top;
    SDL_RaiseWindow((SDL_Window*)top->m_oswindow);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_update_style
// ---------------------------------------------------------------------------

void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle)
{
  if (!hwnd || !hwnd->m_oswindow) return;

  DWORD newstyle = hwnd->m_style;
  bool oldResize = (oldstyle & WS_THICKFRAME) != 0;
  bool newResize = (newstyle & WS_THICKFRAME) != 0;
  bool oldCaption = (oldstyle & WS_CAPTION) != 0;
  bool newCaption = (newstyle & WS_CAPTION) != 0;

  if (oldResize != newResize || oldCaption != newCaption) {
    // SDL3 doesn't support changing window flags after creation,
    // so destroy and recreate. Destroy renderer first (tied to window).
    SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
    if (!e) return;
    SDL_Window *oldwin = e->window;
    bool wasVisible = SDL_GetWindowFlags(oldwin) & SDL_WINDOW_HIDDEN ? false : true;
    hwnd->m_oswindow = NULL;
    hwnd->m_backingstore.reset();
    remove_entry(e);
    SDL_DestroyWindow(oldwin);

    // recreate with new flags
    swell_oswindow_manage(hwnd, wasVisible);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_update_enable
// ---------------------------------------------------------------------------

void swell_oswindow_update_enable(HWND hwnd)
{
  (void)hwnd;
  // SDL3 doesn't have a native enable/disable for windows
}

// ---------------------------------------------------------------------------
// swell_oswindow_update_text
// ---------------------------------------------------------------------------

void swell_oswindow_update_text(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  SDL_SetWindowTitle((SDL_Window*)hwnd->m_oswindow, hwnd->m_title.Get());
}

// ---------------------------------------------------------------------------
// swell_oswindow_invalidate: trigger paint via backing store
// ---------------------------------------------------------------------------

void swell_oswindow_invalidate(HWND hwnd, const RECT *r)
{
  // Invalidation is deferred — m_invalidated/m_child_invalidated flags
  // are set by InvalidateRect before calling this. Actual paint happens
  // in SWELL_RunMessageLoop (batched per iteration), via UpdateWindow
  // (synchronous), or from SDL_EVENT_WINDOW_EXPOSED (OS-triggered).
  // Guard against recursive paint cycles during WM_PAINT.
  if (!hwnd || !hwnd->m_oswindow) return;
  if (hwnd->m_paintctx) return;
  (void)r;
}

// ---------------------------------------------------------------------------
// swell_oswindow_updatetoscreen: copy Skia surface to SDL texture, present
// ---------------------------------------------------------------------------

void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r)
{
  if (!hwnd || !hwnd->m_oswindow) return;

  SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
  if (!e) return;

  sk_sp<SkSurface> bs = hwnd->m_backingstore;
  if (!bs) return;

  SkImageInfo info = bs->imageInfo();
  int pw = info.width();
  int ph = info.height();

  SkPixmap pixmap;
  if (!bs->peekPixels(&pixmap)) return;

  // create/recreate texture if size changed
  if (!e->texture || e->tex_w != pw || e->tex_h != ph) {
    if (e->texture) SDL_DestroyTexture(e->texture);
    e->texture = SDL_CreateTexture(e->renderer,
        SDL_PIXELFORMAT_BGRA32,  // B,G,R,A in memory = Skia kBGRA_8888_SkColorType
        SDL_TEXTUREACCESS_STREAMING,
        pw, ph);
    if (!e->texture) return;
    e->tex_w = pw;
    e->tex_h = ph;
    SDL_SetTextureBlendMode(e->texture, SDL_BLENDMODE_NONE);
  }

  SDL_Rect sdlr;
  if (r) {
    sdlr.x = r->left;
    sdlr.y = r->top;
    sdlr.w = r->right - r->left;
    sdlr.h = r->bottom - r->top;
  } else {
    sdlr.x = 0;
    sdlr.y = 0;
    sdlr.w = pw;
    sdlr.h = ph;
  }

  // clamp to surface bounds
  if (sdlr.x < 0) { sdlr.w += sdlr.x; sdlr.x = 0; }
  if (sdlr.y < 0) { sdlr.h += sdlr.y; sdlr.y = 0; }
  if (sdlr.x + sdlr.w > pw) sdlr.w = pw - sdlr.x;
  if (sdlr.y + sdlr.h > ph) sdlr.h = ph - sdlr.y;
  if (sdlr.w <= 0 || sdlr.h <= 0) return;

  const void *pixels = (const uint8_t*)pixmap.addr() +
      sdlr.y * pixmap.rowBytes() + sdlr.x * 4;

  SDL_UpdateTexture(e->texture, &sdlr, pixels, pixmap.rowBytes());

  // Render full texture — partial updates only touch sub-region,
  // the texture retains old content elsewhere. No clear needed:
  // full-texture overwrite covers entire render target, and
  // clearing with opaque black destroys window transparency.
  SDL_FRect full = { 0, 0, (float)pw, (float)ph };
  SDL_RenderTexture(e->renderer, e->texture, &full, &full);
  SDL_RenderPresent(e->renderer);
}

// ---------------------------------------------------------------------------
// swell_oswindow_to_hwnd / swell_oswindow_from_hwnd
// ---------------------------------------------------------------------------

HWND swell_oswindow_to_hwnd(SWELL_OSWINDOW osw)
{
  if (!osw) return NULL;
  SDL_WindowEntry *e = find_entry_by_window((SDL_Window*)osw);
  return e ? e->hwnd : NULL;
}

SWELL_OSWINDOW swell_oswindow_from_hwnd(HWND hwnd)
{
  if (!hwnd) return NULL;
  SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
  return e ? (SWELL_OSWINDOW)e->window : NULL;
}

// ---------------------------------------------------------------------------
// swell_oswindow_maximize
// ---------------------------------------------------------------------------

void swell_oswindow_maximize(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  SDL_MaximizeWindow((SDL_Window*)hwnd->m_oswindow);
}

// ---------------------------------------------------------------------------
// Key code translation: SDL_Keycode -> VK_*
// ---------------------------------------------------------------------------

static int sdl_key_to_vk(SDL_Keycode key)
{
  if (key >= SDLK_A && key <= SDLK_Z) return 'A' + (key - SDLK_A);
  if (key >= SDLK_0 && key <= SDLK_9) return '0' + (key - SDLK_0);

  switch (key) {
    case SDLK_RETURN:     return VK_RETURN;
    case SDLK_ESCAPE:     return VK_ESCAPE;
    case SDLK_BACKSPACE:  return VK_BACK;
    case SDLK_TAB:        return VK_TAB;
    case SDLK_SPACE:      return VK_SPACE;
    case SDLK_DELETE:     return VK_DELETE;
    case SDLK_INSERT:     return VK_INSERT;
    case SDLK_HOME:       return VK_HOME;
    case SDLK_END:        return VK_END;
    case SDLK_PAGEUP:     return VK_PRIOR;
    case SDLK_PAGEDOWN:   return VK_NEXT;
    case SDLK_LEFT:       return VK_LEFT;
    case SDLK_RIGHT:      return VK_RIGHT;
    case SDLK_UP:         return VK_UP;
    case SDLK_DOWN:       return VK_DOWN;
    case SDLK_PAUSE:      return VK_PAUSE;
    case SDLK_CAPSLOCK:   return VK_CAPITAL;
    case SDLK_PRINTSCREEN:return VK_SNAPSHOT;
    case SDLK_LSHIFT:     return VK_SHIFT;
    case SDLK_RSHIFT:     return VK_SHIFT;
    case SDLK_LCTRL:      return VK_CONTROL;
    case SDLK_RCTRL:      return VK_CONTROL;
    case SDLK_LALT:       return VK_MENU;
    case SDLK_RALT:       return VK_MENU;
    case SDLK_LGUI:       return VK_LWIN;
    case SDLK_RGUI:       return VK_RWIN;
    default: break;
  }

  if (key >= SDLK_F1 && key <= SDLK_F12)
    return VK_F1 + (key - SDLK_F1);

  if (key >= SDLK_F13 && key <= SDLK_F24)
    return VK_F13 + (key - SDLK_F13);

  return 0;
}

// ---------------------------------------------------------------------------
// Build lParam from SDL keymod flags
// ---------------------------------------------------------------------------

static LPARAM make_key_lparam(SDL_Keymod mod)
{
  LPARAM lp = FVIRTKEY;
  if (mod & SDL_KMOD_SHIFT) lp |= FSHIFT;
  if (mod & SDL_KMOD_CTRL)  lp |= FCONTROL;
  if (mod & SDL_KMOD_ALT)   lp |= FALT;
  if (mod & SDL_KMOD_GUI)   lp |= FLWIN;
  return lp;
}

// ---------------------------------------------------------------------------
// NC offset: top-level windows may have a NC area (e.g. menu bar) above client
// ---------------------------------------------------------------------------

static void get_nc_offsets(HWND hwnd, int *nc_left_out, int *nc_top_out)
{
  *nc_left_out = 0;
  *nc_top_out  = 0;
  if (!hwnd || hwnd->m_parent) return;
  // Use NCCALCSIZE to get the client inset from a zero-origin rect
  RECT nr = { 0, 0,
    hwnd->m_position.right - hwnd->m_position.left,
    hwnd->m_position.bottom - hwnd->m_position.top };
  SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&nr);
  *nc_left_out = nr.left;
  *nc_top_out  = nr.top;
}

// ---------------------------------------------------------------------------
// Mouse hit-test: find deepest visible child at (x,y) in CLIENT coords
// ---------------------------------------------------------------------------

static HWND hittest_child(HWND parent, float x, float y)
{
  if (!parent) return NULL;
  int n = parent->m_children.GetSize();
  for (int i = n - 1; i >= 0; i--) {
    HWND ch = parent->m_children.Get(i);
    if (!ch || !ch->m_visible) continue;
    RECT cr = ch->m_position;
    if (x >= cr.left && x < cr.right && y >= cr.top && y < cr.bottom) {
      HWND deeper = hittest_child(ch, x - cr.left, y - cr.top);
      return deeper ? deeper : ch;
    }
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// swell_sdlEventHandler: SDL event -> SWELL message translation
// ---------------------------------------------------------------------------

static void swell_sdlEventHandler(SDL_Event *evt)
{
  switch (evt->type) {

    // ---- window events ----

    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_SHOWN: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2) {
        sk_sp<SkSurface> bs = e->hwnd->m_backingstore;
        SkCanvas *canvas = bs ? bs->getCanvas() : nullptr;
        if (canvas) {
          SWELL_internalSkiaPaint(e->hwnd, canvas, 0, 0, true);
          swell_oswindow_updatetoscreen(e->hwnd, NULL);
        }
      }
      break;
    }

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2 && IsWindowEnabled(e->hwnd)) {
        if (!SendMessage(e->hwnd, WM_CLOSE, 0, 0) &&
            e->hwnd->m_hashaddestroy < 2)
          SendMessage(e->hwnd, WM_COMMAND, IDCANCEL, 0);
      }
      break;
    }

    case SDL_EVENT_WINDOW_RESIZED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        // SDL3 reports logical pixels; swell works in physical
        int nw = swell_log_to_phys(evt->window.data1);
        int nh = swell_log_to_phys(evt->window.data2);
        int ow = e->hwnd->m_position.right - e->hwnd->m_position.left;
        int oh = e->hwnd->m_position.bottom - e->hwnd->m_position.top;

        if (nw > 0 && nh > 0 && (nw != ow || nh != oh)) {
          e->hwnd->m_position.right = e->hwnd->m_position.left + nw;
          e->hwnd->m_position.bottom = e->hwnd->m_position.top + nh;

          // resize backing store, preserve old content
          int pw = 0, ph = 0;
          SDL_GetWindowSizeInPixels(e->window, &pw, &ph);
          if (pw > 0 && ph > 0) {
            sk_sp<SkImage> oldImage;
            if (e->hwnd->m_backingstore)
              oldImage = e->hwnd->m_backingstore->makeImageSnapshot();

            e->hwnd->m_backingstore = SkSurfaces::Raster(
                SkImageInfo::Make(pw, ph, kBGRA_8888_SkColorType, kPremul_SkAlphaType));

            if (e->hwnd->m_backingstore) {
              SkCanvas *c = e->hwnd->m_backingstore->getCanvas();
              if (c) {
                c->clear(SK_ColorTRANSPARENT);
                if (oldImage) c->drawImage(oldImage, 0, 0);
              }
            }
          }
          if (e->texture) { SDL_DestroyTexture(e->texture); e->texture = NULL; }

          SendMessage(e->hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(nw, nh));
        }
      }
      break;
    }

    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        int lnx = 0, lny = 0;
        SDL_GetWindowPosition(e->window, &lnx, &lny);
        int nx = swell_log_to_phys(lnx);
        int ny = swell_log_to_phys(lny);
        int lnw = 0, lnh = 0;
        SDL_GetWindowSize(e->window, &lnw, &lnh);
        int nw = swell_log_to_phys(lnw);
        int nh = swell_log_to_phys(lnh);
        e->hwnd->m_position.left = nx;
        e->hwnd->m_position.top = ny;
        e->hwnd->m_position.right = nx + nw;
        e->hwnd->m_position.bottom = ny + nh;
        UINT szFlag = (evt->type == SDL_EVENT_WINDOW_MAXIMIZED)
                      ? SIZE_MAXIMIZED : SIZE_RESTORED;
        SendMessage(e->hwnd, WM_SIZE, szFlag, MAKELPARAM(nw, nh));
        // trigger repaint
        sk_sp<SkSurface> bs = e->hwnd->m_backingstore;
        SkCanvas *canvas = bs ? bs->getCanvas() : nullptr;
        if (canvas) {
          SWELL_internalSkiaPaint(e->hwnd, canvas, 0, 0, true);
          swell_oswindow_updatetoscreen(e->hwnd, NULL);
        }
      }
      break;
    }

    case SDL_EVENT_WINDOW_MOVED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        int nx = swell_log_to_phys(evt->window.data1);
        int ny = swell_log_to_phys(evt->window.data2);
        int ox = e->hwnd->m_position.left;
        int oy = e->hwnd->m_position.top;
        int w = e->hwnd->m_position.right - e->hwnd->m_position.left;
        int h = e->hwnd->m_position.bottom - e->hwnd->m_position.top;
        if (nx != ox || ny != oy) {
          e->hwnd->m_position.left = nx;
          e->hwnd->m_position.top = ny;
          e->hwnd->m_position.right = nx + w;
          e->hwnd->m_position.bottom = ny + h;
          SendMessage(e->hwnd, WM_MOVE, 0, MAKELPARAM(nx, ny));
        }
      }
      break;
    }

    case SDL_EVENT_WINDOW_FOCUS_GAINED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        g_swell_focused_oswindow_hwnd = e->hwnd;
        SendMessage(e->hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
        HWND foc = GetFocus();
        if (foc) SendMessage(foc, WM_SETFOCUS, 0, 0);
      }
      break;
    }

    case SDL_EVENT_WINDOW_FOCUS_LOST: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        if (g_swell_focused_oswindow_hwnd == e->hwnd)
          g_swell_focused_oswindow_hwnd = NULL;
        HWND foc = GetFocus();
        if (foc) SendMessage(foc, WM_KILLFOCUS, 0, 0);
        SendMessage(e->hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
      }
      break;
    }

    // ---- application events ----

    case SDL_EVENT_QUIT:
      break; // handled by WM_CLOSE cascade on window close

    // ---- keyboard events ----

    case SDL_EVENT_KEY_DOWN: {
      SDL_Keycode k = evt->key.key;
      SDL_Keymod mod = evt->key.mod;
      LPARAM lp = make_key_lparam(mod);

      int vk = sdl_key_to_vk(k);
      if (!vk && k < 0x80) vk = k; // ASCII printable

      HWND foc = GetFocus();
      if (!foc) break;

      // some extended keys get the extended bit
      if (k & SDLK_EXTENDED_MASK) lp |= 0x1000000;

      // Alt/Ctrl/Shift/GUI keys send WM_SYSKEYDOWN (matching swell-experimental)
      UINT kmsg = WM_KEYDOWN;
      if (k == SDLK_LALT || k == SDLK_RALT ||
          k == SDLK_LCTRL || k == SDLK_RCTRL ||
          k == SDLK_LSHIFT || k == SDLK_RSHIFT ||
          k == SDLK_LGUI || k == SDLK_RGUI)
        kmsg = WM_SYSKEYDOWN;

      SendMessage(foc, kmsg, vk, lp);
      break;
    }

    case SDL_EVENT_KEY_UP: {
      SDL_Keycode k = evt->key.key;
      SDL_Keymod mod = evt->key.mod;
      LPARAM lp = make_key_lparam(mod);

      int vk = sdl_key_to_vk(k);
      if (!vk && k < 0x80) vk = k;

      if (vk) {
        HWND foc = GetFocus();
        if (foc) {
          UINT kmsg = WM_KEYUP;
          if (k == SDLK_LALT || k == SDLK_RALT ||
              k == SDLK_LCTRL || k == SDLK_RCTRL ||
              k == SDLK_LSHIFT || k == SDLK_RSHIFT ||
              k == SDLK_LGUI || k == SDLK_RGUI)
            kmsg = WM_SYSKEYUP;
          SendMessage(foc, kmsg, vk, lp);
        }
      }
      break;
    }

    // ---- text input (compose/IME/dead keys) ----

    case SDL_EVENT_TEXT_INPUT: {
      HWND foc = GetFocus();
      if (!foc || !evt->text.text[0]) break;
      const unsigned char *p = (const unsigned char *)evt->text.text;
      while (*p) {
        unsigned int c = 0;
        if (*p < 0x80) c = *p++;
        else if ((*p & 0xE0) == 0xC0 && p[1]) { c = ((*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { c = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { c = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4; }
        else p++;
        if (c) SendMessage(foc, WM_CHAR, c, 0);
      }
      break;
    }

    // ---- mouse button events ----

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      // SDL3 reports mouse coords in logical pixels; swell uses physical
      float mx = swell_log_to_phys((int)evt->button.x);
      float my = swell_log_to_phys((int)evt->button.y);
      Uint8 btn = evt->button.button;
      Uint8 clicks = evt->button.clicks;
      bool down = evt->button.down;

      SDL_WindowEntry *e = find_entry_by_windowID(evt->button.windowID);

      // If a control has mouse capture, route to that control
      HWND cap = GetCapture();
      HWND target = cap;
      if (!target && e && e->hwnd) {
        if (down)
          swell_oswindow_focus(e->hwnd);

        // Translate SDL window coords to client coords (subtract NC inset)
        int nc_left = 0, nc_top = 0;
        get_nc_offsets(e->hwnd, &nc_left, &nc_top);
        float cx = mx - nc_left;
        float cy = my - nc_top;

        // NC area click (menu bar etc.)
        if (down && btn == SDL_BUTTON_LEFT && (cy < 0 || cx < 0)) {
          UINT ncmsg = (clicks >= 2) ? WM_NCLBUTTONDBLCLK : WM_NCLBUTTONDOWN;
          SendMessage(e->hwnd, ncmsg, HTMENU, MAKELPARAM((int)mx, (int)my));
          break;
        }

        target = e->hwnd;
        HWND child = hittest_child(e->hwnd, cx, cy);
        if (child) {
          // coords relative to child's client origin
          cx -= child->m_position.left;
          cy -= child->m_position.top;
          target = child;
        }
        mx = cx;
        my = cy;

        if (down && IsWindowEnabled(target))
          SendMessage(target, WM_MOUSEACTIVATE, 0, 0);
      } else if (cap) {
        // Captured control — translate to top-level client coords first
        HWND toplevel = cap;
        while (toplevel->m_parent) toplevel = (HWND)toplevel->m_parent;
        int nc_left = 0, nc_top = 0;
        get_nc_offsets(toplevel, &nc_left, &nc_top);
        mx -= nc_left;
        my -= nc_top;
        // Convert to cap-local coords
        HWND p = cap;
        while (p && p->m_parent) {
          mx -= p->m_position.left;
          my -= p->m_position.top;
          p = (HWND)p->m_parent;
        }
      }
      if (!target) break;

      UINT msg = 0;
      if (btn == SDL_BUTTON_LEFT) {
        if (down) {
          msg = (clicks >= 2) ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN;
        } else {
          msg = WM_LBUTTONUP;
        }
      } else if (btn == SDL_BUTTON_RIGHT) {
        if (down) {
          msg = (clicks >= 2) ? WM_RBUTTONDBLCLK : WM_RBUTTONDOWN;
        } else {
          msg = WM_RBUTTONUP;
        }
      } else if (btn == SDL_BUTTON_MIDDLE) {
        if (down) {
          msg = (clicks >= 2) ? WM_MBUTTONDBLCLK : WM_MBUTTONDOWN;
        } else {
          msg = WM_MBUTTONUP;
        }
      }

      if (msg) {
        SendMessage(target, msg, 0, MAKELPARAM((int)mx, (int)my));
      }
      break;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      // SDL3 reports mouse coords in logical pixels; swell uses physical
      float mx = swell_log_to_phys((int)evt->motion.x);
      float my = swell_log_to_phys((int)evt->motion.y);

      HWND cap = GetCapture();
      HWND target = cap;
      if (!target) {
        SDL_WindowEntry *e = find_entry_by_windowID(evt->motion.windowID);
        if (e && e->hwnd) {
          int nc_left = 0, nc_top = 0;
          get_nc_offsets(e->hwnd, &nc_left, &nc_top);
          float cx = mx - nc_left;
          float cy = my - nc_top;
          target = e->hwnd;
          HWND child = hittest_child(e->hwnd, cx, cy);
          if (child) {
            cx -= child->m_position.left;
            cy -= child->m_position.top;
            target = child;
          }
          mx = cx;
          my = cy;
        }
      }
      if (target) {
        SendMessage(target, WM_MOUSEMOVE, 0, MAKELPARAM((int)mx, (int)my));
        SendMessage(target, WM_SETCURSOR, (WPARAM)target, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
      }
      break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
      float wx = evt->wheel.x;
      float wy = evt->wheel.y;
      // SDL3 reports mouse coords in logical pixels; swell uses physical
      int wmx = swell_log_to_phys((int)evt->wheel.mouse_x);
      int wmy = swell_log_to_phys((int)evt->wheel.mouse_y);
      SDL_WindowEntry *e = find_entry_by_windowID(evt->wheel.windowID);
      HWND target = NULL;
      if (e && e->hwnd) {
        int nc_left = 0, nc_top = 0;
        get_nc_offsets(e->hwnd, &nc_left, &nc_top);
        float cx = wmx - nc_left;
        float cy = wmy - nc_top;
        target = e->hwnd;
        HWND child = hittest_child(e->hwnd, cx, cy);
        if (child) target = child;
      }
      if (!target) break;

      int delta = (int)(wy * 120.0f);  // WHEEL_DELTA = 120
      if (delta != 0) {
        SendMessage(target, WM_MOUSEWHEEL,
                    MAKEWPARAM(0, (WORD)delta),
                    MAKELPARAM(wmx, wmy));
      }
      int hdelta = (int)(wx * 120.0f);
      if (hdelta != 0) {
        SendMessage(target, WM_MOUSEHWHEEL,
                    MAKEWPARAM(0, (WORD)hdelta),
                    MAKELPARAM(wmx, wmy));
      }
      break;
    }

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// SWELL_RunEvents: poll SDL3 events and dispatch
// ---------------------------------------------------------------------------

void SWELL_RunEvents()
{
  SDL_Event evt;
  while (SDL_PollEvent(&evt)) {
    swell_sdlEventHandler(&evt);
  }
}

// ---------------------------------------------------------------------------
// SWELL_initargs: initialize SDL3
// ---------------------------------------------------------------------------

#ifndef SWELL_TARGET_OSX
void SWELL_initargs(int *argc, char ***argv)
{
  (void)argc;
  (void)argv;
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
  if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SWELL SDL3: SDL_Init failed: %s\n", SDL_GetError());
  }

  swell_scaling_init(false);
  swell_scale_theme();
}
#endif

// ---------------------------------------------------------------------------
// swell_scaling_init: auto-detect DPI via hidden SDL3 test window
// ---------------------------------------------------------------------------

void swell_scaling_init(bool no_auto_hidpi)
{
  if (no_auto_hidpi || g_swell_ui_scale != 256) return;

  SDL_Window *test = SDL_CreateWindow("swell_dpi_test", 1, 1,
      SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  if (!test) return;

  float cs = SDL_GetWindowDisplayScale(test);
  SDL_DestroyWindow(test);

  if (cs > 1.0f && cs < 8.0f)
    g_swell_ui_scale = (int)(cs * 256.0f + 0.5f);
}

// ---- SWELL_RunMessageLoop ----
// Defined in swell-wnd.cpp (overrides backend stub)

// ---------------------------------------------------------------------------
// SWELL_CreateXBridgeWindow, SWELL_GetOSWindow, SWELL_GetOSEvent
// ---------------------------------------------------------------------------

#ifndef SWELL_TARGET_OSX
HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *r)
{
  (void)viewpar; (void)wref; (void)r;
  return NULL;
}

void *SWELL_GetOSWindow(HWND hwnd, const char *type)
{
  if (!hwnd || !type) return NULL;
  if (strcmp(type, "!sdl") == 0)
    return (void*)hwnd->m_oswindow;
  return NULL;
}

void *SWELL_GetOSEvent(const char *type)
{
  (void)type;
  return NULL;
}
#endif

// ---------------------------------------------------------------------------
// SWELL_GetViewPort: query actual display dimensions via SDL3
// ---------------------------------------------------------------------------

void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
{
  r->left = r->top = 0;
  r->right = 1024;
  r->bottom = 768;

  SDL_DisplayID display = SDL_GetPrimaryDisplay();
  if (sourcerect) {
    // sourcerect is physical; SDL_GetDisplayForRect expects logical
    RECT lr;
    swell_phys_rect_to_log(sourcerect, &lr);
    SDL_Rect sr = { lr.left, lr.top, lr.right - lr.left, lr.bottom - lr.top };
    SDL_DisplayID d = SDL_GetDisplayForRect(&sr);
    if (d) display = d;
  }
  SDL_Rect dr = { 0, 0, 1024, 768 };
  if ((wantWork ? SDL_GetDisplayUsableBounds(display, &dr) :
                  SDL_GetDisplayBounds(display, &dr))) {
    // SDL returns logical; swell uses physical
    r->left = swell_log_to_phys(dr.x);
    r->top = swell_log_to_phys(dr.y);
    r->right = r->left + swell_log_to_phys(dr.w);
    r->bottom = r->top + swell_log_to_phys(dr.h);
  }
}

// SWELL_GetScaling256 defined in swell-gdi.cpp

#endif // SWELL_TARGET_SDL3
