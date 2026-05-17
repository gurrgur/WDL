/*
  SWELL2 SDL3 OS backend — display, window management, event translation.
  Provides actual OS windows via SDL3 and routes SDL events to SWELL messages.
*/

#include "swell-internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

#if __has_include(<X11/Xlib.h>) && __has_include(<X11/Xutil.h>)
#define SWELL_SDL3_HAVE_X11_HEADERS 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

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
static SDL_Surface *s_program_icon_surface = NULL;
int g_swell_sdl_current_event_type = 0;

static void swell_sdl_apply_app_metadata()
{
  const char *appname = (g_swell_appname && *g_swell_appname) ?
      g_swell_appname : NULL;
  if (!appname) return;

  SDL_SetHintWithPriority(SDL_HINT_APP_NAME, appname, SDL_HINT_DEFAULT);
  SDL_SetHintWithPriority(SDL_HINT_APP_ID, appname, SDL_HINT_DEFAULT);
  SDL_SetAppMetadata(appname, NULL, appname);
}

#ifdef SWELL_SDL3_HAVE_X11_HEADERS
static void swell_sdl_set_x11_class(SDL_Window *window)
{
  if (!window || !g_swell_appname || !*g_swell_appname) return;

  SDL_PropertiesID props = SDL_GetWindowProperties(window);
  Display *display = (Display *)SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
  const Window xid = (Window)SDL_GetNumberProperty(
      props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  if (!display || !xid) return;

  typedef int (*XSetClassHintFunc)(Display *, Window, XClassHint *);
  typedef int (*XFlushFunc)(Display *);
  static void *s_x11;
  static XSetClassHintFunc s_set_class_hint;
  static XFlushFunc s_flush;
  static bool s_checked;
  if (!s_checked) {
    s_checked = true;
    s_x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (s_x11) {
      s_set_class_hint = (XSetClassHintFunc)dlsym(s_x11, "XSetClassHint");
      s_flush = (XFlushFunc)dlsym(s_x11, "XFlush");
    }
  }
  if (!s_set_class_hint) return;

  XClassHint class_hint;
  class_hint.res_name = (char *)g_swell_appname;
  class_hint.res_class = (char *)g_swell_appname;
  s_set_class_hint(display, xid, &class_hint);
  if (s_flush) s_flush(display);
}

#else
static void swell_sdl_set_x11_class(SDL_Window *) {}
#endif

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

  swell_sdl_apply_app_metadata();

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

  // Popup detection: match original swell's override_redirect logic.
  // Borderless top-level window (no caption, no resize) = popup
  // (tooltip, popup label, dropdown, context menu placeholder).
  // Also WS_CHILD on a top-level = explicit popup flag.
  const bool is_popup = !hwnd->m_parent &&
      (!(hwnd->m_style & WS_CAPTION) || (hwnd->m_style & WS_CHILD));

  SDL_Window *parent_sdlwin = NULL;
  int rel_lx = lx, rel_ly = ly;
  if (is_popup) {
    HWND__ *osw = hwnd->m_owner;
    while (osw && !osw->m_oswindow)
      osw = osw->m_owner ? osw->m_owner : (HWND__ *)osw->m_parent;
    // Tooltips in Win32 typically have no owner. Fall back to the focused
    // top-level so we still get an xdg-popup parent (required on Wayland and
    // needed for skip-taskbar / no-focus-steal behavior).
    if (!osw || !osw->m_oswindow) {
      extern HWND g_swell_focused_oswindow_hwnd;
      if (g_swell_focused_oswindow_hwnd &&
          g_swell_focused_oswindow_hwnd->m_oswindow)
        osw = g_swell_focused_oswindow_hwnd;
    }
    parent_sdlwin = osw ? (SDL_Window *)osw->m_oswindow : NULL;
    if (parent_sdlwin) {
      int px = 0, py = 0;
      SDL_GetWindowPosition(parent_sdlwin, &px, &py);
      rel_lx = lx - px;
      rel_ly = ly - py;
    }
  }

  SDL_Window *sdlwin;
  if (is_popup && parent_sdlwin) {
    // Passive borderless popup (tooltip / dropdown label). Menus take their
    // own path in swell-menu.cpp, so anything routed here is hover-only.
    // SDL_WINDOW_TOOLTIP maps to xdg-popup without grab, no focus steal —
    // matches original swell's focus_on_map=false + override_redirect.
    sdlwin = SDL_CreatePopupWindow(parent_sdlwin, rel_lx, rel_ly, lw, lh,
        SDL_WINDOW_TOOLTIP | SDL_WINDOW_NOT_FOCUSABLE |
        SDL_WINDOW_HIGH_PIXEL_DENSITY);
  } else if (is_popup) {
    // Popup without any SDL parent → borderless regular window.
    // UTILITY keeps it out of the taskbar; NOT_FOCUSABLE prevents focus steal.
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY |
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_NOT_FOCUSABLE |
        SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN;
    sdlwin = SDL_CreateWindow(hwnd->m_title.Get(), lw, lh, flags);
  } else {
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (hwnd->m_style & WS_THICKFRAME)
      flags |= SDL_WINDOW_RESIZABLE;
    if (!(hwnd->m_style & WS_CAPTION))
      flags |= SDL_WINDOW_BORDERLESS;
    if (!wantFocus)
      flags |= SDL_WINDOW_NOT_FOCUSABLE;
    flags |= SDL_WINDOW_HIDDEN;

    sdlwin = SDL_CreateWindow(hwnd->m_title.Get(), lw, lh, flags);
  }

  if (!sdlwin) {
    fprintf(stderr, "SWELL SDL3: SDL_CreateWindow failed: %s\n", SDL_GetError());
    return;
  }

  // SDL_CreatePopupWindow positions at creation; everything else needs an
  // explicit post-create position (including the popup fallback that took the
  // regular SDL_CreateWindow path).
  if (!(is_popup && parent_sdlwin)) {
    SDL_SetWindowPosition(sdlwin, lx, ly);
  }

  swell_sdl_set_x11_class(sdlwin);

  SDL_ShowWindow(sdlwin);

  SDL_Renderer *rend = SDL_CreateRenderer(sdlwin, NULL);
  if (!rend) {
    fprintf(stderr, "SWELL SDL3: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(sdlwin);
    return;
  }
  SDL_SetRenderVSync(rend, 1);

  add_entry(sdlwin, hwnd, rend);
  hwnd->m_oswindow = sdlwin;

  if (s_program_icon_surface)
    SDL_SetWindowIcon(sdlwin, s_program_icon_surface);

  SDL_StartTextInput(sdlwin);

  // create backing Skia surface (pixel dimensions for HiDPI)
  int surf_w = 0, surf_h = 0;
  SDL_GetWindowSizeInPixels(sdlwin, &surf_w, &surf_h);
  if (surf_w < 1) surf_w = pw;
  if (surf_h < 1) surf_h = ph;
  hwnd->m_backingstore = SkSurfaces::Raster(
      SkImageInfo::Make(surf_w, surf_h, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
      &g_swell_surfprops);
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
    // Snapshot current OS window position before destroying, matching
    // original swell which calls GetWindowRect before oswindow_destroy.
    // This ensures re-creating the window (hide then show) preserves the
    // position the user last saw.
    int lx = 0, ly = 0, lw = 0, lh = 0;
    SDL_GetWindowPosition(w, &lx, &ly);
    SDL_GetWindowSize(w, &lw, &lh);
    hwnd->m_position.left   = swell_log_to_phys(lx);
    hwnd->m_position.top    = swell_log_to_phys(ly);
    hwnd->m_position.right  = hwnd->m_position.left + swell_log_to_phys(lw);
    hwnd->m_position.bottom = hwnd->m_position.top  + swell_log_to_phys(lh);

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
          SkImageInfo::Make(surf_w, surf_h, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
          &g_swell_surfprops);

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
    case SDLK_KP_0:       return VK_NUMPAD0;
    case SDLK_KP_1:       return VK_NUMPAD1;
    case SDLK_KP_2:       return VK_NUMPAD2;
    case SDLK_KP_3:       return VK_NUMPAD3;
    case SDLK_KP_4:       return VK_NUMPAD4;
    case SDLK_KP_5:       return VK_NUMPAD5;
    case SDLK_KP_6:       return VK_NUMPAD6;
    case SDLK_KP_7:       return VK_NUMPAD7;
    case SDLK_KP_8:       return VK_NUMPAD8;
    case SDLK_KP_9:       return VK_NUMPAD9;
    case SDLK_KP_PERIOD:  return VK_DECIMAL;
    case SDLK_KP_DIVIDE:  return VK_DIVIDE;
    case SDLK_KP_MULTIPLY:return VK_MULTIPLY;
    case SDLK_KP_MINUS:   return VK_SUBTRACT;
    case SDLK_KP_PLUS:    return VK_ADD;
    case SDLK_KP_ENTER:   return VK_RETURN;
    case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK;
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

static HWND hittest_child(HWND parent, float *x, float *y)
{
  if (!parent || !x || !y) return NULL;
  int n = parent->m_children.GetSize();
  for (int i = n - 1; i >= 0; i--) {
    HWND ch = parent->m_children.Get(i);
    if (!ch || !ch->m_visible) continue;
    RECT cr = ch->m_position;
    if (*x >= cr.left && *x < cr.right && *y >= cr.top && *y < cr.bottom) {
      float cx = *x - cr.left;
      float cy = *y - cr.top;
      // Apply NCCALCSIZE (matching windowfrompoint_recurse) so the
      // coordinate passed to the next recursion level is client-relative.
      int cw = cr.right - cr.left;
      int chh = cr.bottom - cr.top;
      NCCALCSIZE_PARAMS ncp = {{{0, 0, cw, chh}}};
      SendMessage(ch, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
      cx -= ncp.rgrc[0].left;
      cy -= ncp.rgrc[0].top;
      HWND deeper = hittest_child(ch, &cx, &cy);
      *x = cx;
      *y = cy;
      return deeper ? deeper : ch;
    }
  }
  return NULL;
}

static void sdl_window_point_to_client(HWND hwnd, float *x, float *y)
{
  if (!hwnd || !x || !y) return;

  HWND top = hwnd;
  while (top->m_parent) top = (HWND)top->m_parent;

  int nc_left = 0, nc_top = 0;
  get_nc_offsets(top, &nc_left, &nc_top);
  *x -= nc_left;
  *y -= nc_top;

  for (HWND p = hwnd; p && p->m_parent; p = (HWND)p->m_parent) {
    *x -= p->m_position.left;
    *y -= p->m_position.top;
  }
}

// ---------------------------------------------------------------------------
// swell_sdlEventHandler: SDL event -> SWELL message translation
// ---------------------------------------------------------------------------

static HWND s_last_mousemove_hwnd = NULL;
static UINT s_last_mousemove_msg = 0;
static int s_last_mousemove_x = 0;
static int s_last_mousemove_y = 0;
static Uint32 s_last_mousemove_state = 0;
static bool s_last_mousemove_valid = false;

static void swell_sdlResetLastMouseMove()
{
  s_last_mousemove_valid = false;
}

static bool swell_sdlShouldSendMouseMove(HWND hwnd, UINT msg,
                                         int x, int y, Uint32 state)
{
  if (s_last_mousemove_valid &&
      s_last_mousemove_hwnd == hwnd &&
      s_last_mousemove_msg == msg &&
      s_last_mousemove_x == x &&
      s_last_mousemove_y == y &&
      s_last_mousemove_state == state)
    return false;

  s_last_mousemove_hwnd = hwnd;
  s_last_mousemove_msg = msg;
  s_last_mousemove_x = x;
  s_last_mousemove_y = y;
  s_last_mousemove_state = state;
  s_last_mousemove_valid = true;
  return true;
}

static float swell_sdlAbsf(float v)
{
  return v < 0.0f ? -v : v;
}

static float swell_sdlMinScrollDelta()
{
  float delta = swell_log_to_phys(1.0f);
  return delta > 1.0f ? delta : 1.0f;
}

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

    case SDL_EVENT_WINDOW_MINIMIZED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2)
        SendMessage(e->hwnd, WM_SIZE, SIZE_MINIMIZED,
                    MAKELPARAM(0, 0));
      break;
    }

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2 && IsWindowEnabled(e->hwnd)) {
        HWND hwnd = e->hwnd;
        if (!SendMessage(hwnd, WM_CLOSE, 0, 0)) {
          // WM_CLOSE may have triggered DestroyWindow.  Reload the window
          // entry — the old `e` and `hwnd` may be dangling pointers.
          e = find_entry_by_windowID(evt->window.windowID);
          if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2)
            SendMessage(e->hwnd, WM_COMMAND, IDCANCEL, 0);
        }
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
                SkImageInfo::Make(pw, ph, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
                &g_swell_surfprops);

            if (e->hwnd->m_backingstore) {
              SkCanvas *c = e->hwnd->m_backingstore->getCanvas();
              if (c) {
                c->clear(SK_ColorTRANSPARENT);
                if (oldImage) c->drawImage(oldImage, 0, 0);
              }
            }
          }
          if (e->texture) { SDL_DestroyTexture(e->texture); e->texture = NULL; }

          RECT ncr1 = {0, 0, nw, nh};
          SendMessage(e->hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncr1);
          UINT szFlag1 = (SDL_GetWindowFlags(e->window) & SDL_WINDOW_MAXIMIZED)
                         ? SIZE_MAXIMIZED : SIZE_RESTORED;
          SendMessage(e->hwnd, WM_SIZE, szFlag1,
                      MAKELPARAM(ncr1.right - ncr1.left, ncr1.bottom - ncr1.top));
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

        // Resize backing store to physical pixel dimensions (SDL may not fire
        // SDL_EVENT_WINDOW_RESIZED for maximize/restore on all compositors).
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(e->window, &pw, &ph);
        if (pw > 0 && ph > 0) {
          sk_sp<SkImage> oldImage;
          if (e->hwnd->m_backingstore)
            oldImage = e->hwnd->m_backingstore->makeImageSnapshot();
          e->hwnd->m_backingstore = SkSurfaces::Raster(
              SkImageInfo::Make(pw, ph, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
              &g_swell_surfprops);
          if (e->hwnd->m_backingstore) {
            SkCanvas *c = e->hwnd->m_backingstore->getCanvas();
            if (c) { c->clear(SK_ColorTRANSPARENT); if (oldImage) c->drawImage(oldImage, 0, 0); }
          }
        }
        if (e->texture) { SDL_DestroyTexture(e->texture); e->texture = NULL; }

        UINT szFlag = (evt->type == SDL_EVENT_WINDOW_MAXIMIZED)
                      ? SIZE_MAXIMIZED : SIZE_RESTORED;
        RECT ncr2 = {0, 0, nw, nh};
        SendMessage(e->hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncr2);
        SendMessage(e->hwnd, WM_SIZE, szFlag,
                    MAKELPARAM(ncr2.right - ncr2.left, ncr2.bottom - ncr2.top));

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
        SendMessage(e->hwnd, WM_ACTIVATEAPP, TRUE, 0);
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
        SendMessage(e->hwnd, WM_ACTIVATEAPP, FALSE, 0);
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

      if (vk) {
        HWND foc = GetFocus();
        if (foc) {

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
        }
      }
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

        if (down)
          SDL_CaptureMouse(true);
        else
          SDL_CaptureMouse(false);

        // Translate SDL window coords to client coords (subtract NC inset)
        int nc_left = 0, nc_top = 0;
        get_nc_offsets(e->hwnd, &nc_left, &nc_top);
        float cx = mx - nc_left;
        float cy = my - nc_top;

        // NC area click (menu bar etc.) — use WM_NCHITTEST for proper HT code.
        // Per Win32 convention, WM_NCHITTEST / WM_NC*BUTTON* lParam carries
        // SCREEN coords, so translate window-relative mx/my by the window's
        // screen origin (m_position is screen-absolute for top-level windows).
        int client_w = e->hwnd->m_position.right - e->hwnd->m_position.left - nc_left;
        int client_h = e->hwnd->m_position.bottom - e->hwnd->m_position.top - nc_top;
        if (down && (cy < 0 || cx < 0 || cx >= client_w || cy >= client_h)) {
          const int sx = (int)mx + e->hwnd->m_position.left;
          const int sy = (int)my + e->hwnd->m_position.top;
          LRESULT ht = SendMessage(e->hwnd, WM_NCHITTEST, 0, MAKELPARAM(sx, sy));
          if (ht == HTCLIENT) ht = HTNOWHERE; // clamp if client-area returned
          UINT ncmsg = 0;
          if (btn == SDL_BUTTON_LEFT) {
            ncmsg = (clicks >= 2) ? WM_NCLBUTTONDBLCLK : WM_NCLBUTTONDOWN;
          } else if (btn == SDL_BUTTON_RIGHT) {
            ncmsg = (clicks >= 2) ? WM_NCRBUTTONDBLCLK : WM_NCRBUTTONDOWN;
          } else if (btn == SDL_BUTTON_MIDDLE) {
            ncmsg = (clicks >= 2) ? WM_NCMBUTTONDBLCLK : WM_NCMBUTTONDOWN;
          }
          if (ncmsg) SendMessage(e->hwnd, ncmsg, (WPARAM)ht, MAKELPARAM(sx, sy));
          break;
        }

        // Screen coords for NC hit-test below (m_position is screen-absolute
        // for top-level windows). Captured before mx/my get overwritten.
        const int sx_screen = (int)mx + e->hwnd->m_position.left;
        const int sy_screen = (int)my + e->hwnd->m_position.top;

        target = e->hwnd;
        HWND child = hittest_child(e->hwnd, &cx, &cy);
        if (child) {
          target = child;
        }
        mx = cx;
        my = cy;

        // NC hit-test on the target child: if it claims the click belongs in
        // a non-client area (e.g. a coolscroll-reserved scrollbar strip),
        // route as WM_NC*BUTTON* with screen-coord lParam instead of client.
        if (down && target && target != e->hwnd) {
          LRESULT ht = SendMessage(target, WM_NCHITTEST, 0, MAKELPARAM(sx_screen, sy_screen));
          if (ht != HTCLIENT && ht != HTNOWHERE && ht != 0) {
            UINT ncmsg = 0;
            if (btn == SDL_BUTTON_LEFT)
              ncmsg = (clicks >= 2) ? WM_NCLBUTTONDBLCLK : WM_NCLBUTTONDOWN;
            else if (btn == SDL_BUTTON_RIGHT)
              ncmsg = (clicks >= 2) ? WM_NCRBUTTONDBLCLK : WM_NCRBUTTONDOWN;
            else if (btn == SDL_BUTTON_MIDDLE)
              ncmsg = (clicks >= 2) ? WM_NCMBUTTONDBLCLK : WM_NCMBUTTONDOWN;
            if (ncmsg) {
              if (IsWindowEnabled(target))
                SendMessage(target, WM_MOUSEACTIVATE, 0, 0);
              SendMessage(target, ncmsg, (WPARAM)ht, MAKELPARAM(sx_screen, sy_screen));
              break;
            }
          }
        }

        if (down && IsWindowEnabled(target))
          SendMessage(target, WM_MOUSEACTIVATE, 0, 0);
      } else if (cap) {
        sdl_window_point_to_client(cap, &mx, &my);
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
        swell_sdlResetLastMouseMove();
        SendMessage(target, msg, 0, MAKELPARAM((int)mx, (int)my));
      }
      break;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      // SDL3 reports mouse coords in logical pixels; swell uses physical
      float mx = swell_log_to_phys((int)evt->motion.x);
      float my = swell_log_to_phys((int)evt->motion.y);
      bool in_nc = false;

      HWND cap = GetCapture();
      HWND target = cap;
      if (!target) {
        SDL_WindowEntry *e = find_entry_by_windowID(evt->motion.windowID);
        if (e && e->hwnd) {
          int nc_left = 0, nc_top = 0;
          get_nc_offsets(e->hwnd, &nc_left, &nc_top);
          float cx = mx - nc_left;
          float cy = my - nc_top;
          if (cy < 0 || cx < 0) {
            in_nc = true;
            target = e->hwnd;
          } else {
            target = e->hwnd;
            HWND child = hittest_child(e->hwnd, &cx, &cy);
            if (child) {
              target = child;
            }
            mx = cx;
            my = cy;
          }
        }
      } else {
        sdl_window_point_to_client(cap, &mx, &my);
      }
      if (target) {
        const int ix = (int)mx;
        const int iy = (int)my;
        // NC area mouse move
        if (in_nc) {
          if (swell_sdlShouldSendMouseMove(target, WM_NCMOUSEMOVE, ix, iy, evt->motion.state))
            SendMessage(target, WM_NCMOUSEMOVE, 0, MAKELPARAM(ix, iy));
        } else {
          if (swell_sdlShouldSendMouseMove(target, WM_MOUSEMOVE, ix, iy, evt->motion.state)) {
            SendMessage(target, WM_MOUSEMOVE, 0, MAKELPARAM(ix, iy));
            SendMessage(target, WM_SETCURSOR, (WPARAM)target, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
          }
        }
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
        HWND child = hittest_child(e->hwnd, &cx, &cy);
        if (child) target = child;
      }
      if (!target) break;
      swell_sdlResetLastMouseMove();

      static HWND s_scroll_accum_target = NULL;
      static float s_scroll_accum_y = 0.0f;
      static float s_scroll_accum_x = 0.0f;
      if (s_scroll_accum_target != target) {
        s_scroll_accum_target = target;
        s_scroll_accum_y = 0.0f;
        s_scroll_accum_x = 0.0f;
      }
      s_scroll_accum_y += wy * 120.0f;
      s_scroll_accum_x += wx * 120.0f;
      const float min_scroll_delta = swell_sdlMinScrollDelta();
      int delta = 0;
      int hdelta = 0;
      if (swell_sdlAbsf(s_scroll_accum_y) >= min_scroll_delta) {
        delta = (int)s_scroll_accum_y;
        s_scroll_accum_y -= (float)delta;
      }
      if (swell_sdlAbsf(s_scroll_accum_x) >= min_scroll_delta) {
        hdelta = (int)s_scroll_accum_x;
        s_scroll_accum_x -= (float)hdelta;
      }
      if (delta != 0) {
        SendMessage(target, WM_MOUSEWHEEL,
                    MAKEWPARAM(0, (WORD)delta),
                    MAKELPARAM(wmx, wmy));
      }
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

static void swell_sdlDispatchEvent(SDL_Event *evt)
{
  g_swell_event_dispatch_depth++;
  const int old_event_type = g_swell_sdl_current_event_type;
  g_swell_sdl_current_event_type = evt ? (int)evt->type : 0;
  if (!swell_menu_sdl_handle_event(evt))
    swell_sdlEventHandler(evt);
  g_swell_sdl_current_event_type = old_event_type;
  g_swell_event_dispatch_depth--;
}

// ---------------------------------------------------------------------------
// swell_load_program_icon: load <exe_dir>/Resources/main.png as window icon
// ---------------------------------------------------------------------------

static void swell_load_program_icon()
{
  if (s_program_icon_surface) return;

  char buf[1024];
  GetModuleFileName(NULL, buf, sizeof(buf));
  if (!buf[0]) return;

  char *slash = strrchr(buf, '/');
  if (slash) *slash = '\0';

  char path[1024];
  snprintf(path, sizeof(path), "%s/Resources/main.png", buf);
  HICON img = LoadNamedImage(path, true);
  if (!img) {
    snprintf(path, sizeof(path), "%s/Resources/main.ico", buf);
    img = LoadNamedImage(path, true);
  }

  BITMAP bm;
  memset(&bm, 0, sizeof(bm));
  if (img && GetObject(img, sizeof(bm), &bm) &&
      bm.bmBits && bm.bmWidth > 0 && bm.bmHeight > 0)
  {
    s_program_icon_surface = SDL_CreateSurfaceFrom(
        bm.bmWidth, bm.bmHeight, SDL_PIXELFORMAT_BGRA32,
        bm.bmBits, bm.bmWidthBytes);
  }
}

// ---------------------------------------------------------------------------
// SWELL_RunEvents: poll SDL3 events and dispatch
// ---------------------------------------------------------------------------

static bool swell_sdl_has_dirty_window()
{
  for (HWND w = g_swell_top_level_list; w; w = w->m_next) {
    if ((w->m_invalidated || w->m_child_invalidated) && w->m_backingstore)
      return true;
  }
  return false;
}

void SWELL_RunEvents()
{
  SDL_Event evt;
  SDL_Event pending_motion;
  SDL_Event pending_wheel;
  bool has_pending_motion = false;
  bool has_pending_wheel = false;

  auto flush_pending_motion = [&]() {
    if (!has_pending_motion) return;
    swell_sdlDispatchEvent(&pending_motion);
    has_pending_motion = false;
  };

  auto flush_pending_wheel = [&]() {
    if (!has_pending_wheel) return;
    swell_sdlDispatchEvent(&pending_wheel);
    has_pending_wheel = false;
  };

  while (SDL_PollEvent(&evt)) {
    if (evt.type == SDL_EVENT_MOUSE_MOTION) {
      flush_pending_wheel();
      if (swell_sdl_has_dirty_window()) {
        swell_sdlDispatchEvent(&evt);
        return;
      }
      pending_motion = evt;
      has_pending_motion = true;
      continue;
    }

    if (evt.type == SDL_EVENT_MOUSE_WHEEL) {
      flush_pending_motion();
      if (swell_sdl_has_dirty_window()) {
        swell_sdlDispatchEvent(&evt);
        return;
      }
      if (has_pending_wheel &&
          pending_wheel.wheel.windowID == evt.wheel.windowID) {
        pending_wheel.wheel.x += evt.wheel.x;
        pending_wheel.wheel.y += evt.wheel.y;
        pending_wheel.wheel.mouse_x = evt.wheel.mouse_x;
        pending_wheel.wheel.mouse_y = evt.wheel.mouse_y;
        pending_wheel.wheel.timestamp = evt.wheel.timestamp;
      } else {
        flush_pending_wheel();
        if (swell_sdl_has_dirty_window()) {
          swell_sdlDispatchEvent(&evt);
          return;
        }
        pending_wheel = evt;
        has_pending_wheel = true;
      }
      continue;
    }

    if (has_pending_motion) {
      flush_pending_motion();
      if (swell_sdl_has_dirty_window()) {
        swell_sdlDispatchEvent(&evt);
        return;
      }
    }
    if (has_pending_wheel) {
      flush_pending_wheel();
      if (swell_sdl_has_dirty_window()) {
        swell_sdlDispatchEvent(&evt);
        return;
      }
    }

    swell_sdlDispatchEvent(&evt);
    if (swell_sdl_has_dirty_window()) return;
  }

  flush_pending_motion();
  if (swell_sdl_has_dirty_window()) return;
  flush_pending_wheel();
}

// ---------------------------------------------------------------------------
// SWELL_initargs: initialize SDL3
// ---------------------------------------------------------------------------

#ifndef SWELL_TARGET_OSX
void SWELL_initargs(int *argc, char ***argv)
{
  (void)argc;
  (void)argv;
  swell_sdl_apply_app_metadata();
#if defined(__linux__)
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "wayland,x11", SDL_HINT_DEFAULT);
#endif
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
  if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SWELL SDL3: SDL_Init failed: %s\n", SDL_GetError());
  }
  swell_load_program_icon();

  swell_scaling_init(false);
  // Initialize the theme. SWELL_THEME=dark forces dark mode (preview only;
  // dark mode is not yet finalized).
  int themeMode = SWELL_THEME_LIGHT;
  const char *th = getenv("SWELL_THEME");
  if (th && (!strcmp(th, "dark") || !strcmp(th, "DARK"))) themeMode = SWELL_THEME_DARK;
  swell_theme_init(themeMode);
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
  bool ok = wantWork ? SDL_GetDisplayUsableBounds(display, &dr) :
                       SDL_GetDisplayBounds(display, &dr);

  if (ok) {
    // SDL returns logical; swell uses physical
    r->left = swell_log_to_phys(dr.x);
    r->top = swell_log_to_phys(dr.y);
    r->right = r->left + swell_log_to_phys(dr.w);
    r->bottom = r->top + swell_log_to_phys(dr.h);
  }
}

// SWELL_GetScaling256 defined in swell-gdi.cpp

#endif // SWELL_TARGET_SDL3
