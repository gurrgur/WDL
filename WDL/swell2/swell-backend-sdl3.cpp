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
  fprintf(stderr, "SWELL_CALL: swell_oswindow_manage\n");
  if (!hwnd || hwnd->m_oswindow) return;

  RECT r = hwnd->m_position;
  int w = r.right - r.left;
  int h = r.bottom - r.top;
  if (w < 1) w = 400;
  if (h < 1) h = 300;

  SDL_WindowFlags flags = 0;
  if (hwnd->m_style & WS_THICKFRAME)
    flags |= SDL_WINDOW_RESIZABLE;
  if (!(hwnd->m_style & WS_CAPTION))
    flags |= SDL_WINDOW_BORDERLESS;
  if (!wantFocus)
    flags |= SDL_WINDOW_NOT_FOCUSABLE;
  flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  flags |= SDL_WINDOW_HIDDEN; // show after position is set

  SDL_Window *sdlwin = SDL_CreateWindow(
      hwnd->m_title.Get(), w, h, flags);

  if (!sdlwin) {
    fprintf(stderr, "SWELL SDL3: SDL_CreateWindow failed: %s\n", SDL_GetError());
    return;
  }

  SDL_SetWindowPosition(sdlwin, r.left, r.top);

  if (wantFocus)
    SDL_ShowWindow(sdlwin);

  SDL_Renderer *rend = SDL_CreateRenderer(sdlwin, NULL);
  if (!rend) {
    fprintf(stderr, "SWELL SDL3: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(sdlwin);
    return;
  }

  add_entry(sdlwin, hwnd, rend);
  hwnd->m_oswindow = sdlwin;

  // create backing Skia surface (pixel dimensions for HiDPI)
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(sdlwin, &pw, &ph);
  if (pw < 1) pw = w;
  if (ph < 1) ph = h;
  hwnd->m_backingstore = SkSurfaces::Raster(
      SkImageInfo::MakeN32Premul(pw, ph));
}

// ---------------------------------------------------------------------------
// swell_oswindow_destroy
// ---------------------------------------------------------------------------

void swell_oswindow_destroy(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_destroy\n");
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
  fprintf(stderr, "SWELL_CALL: swell_oswindow_resize\n");
  if (!hwnd || !hwnd->m_oswindow) return;

  SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
  if (!e) return;

  if (reposflag & 1)
    SDL_SetWindowPosition(e->window, r->left, r->top);
  if (reposflag & 2) {
    int w = r->right - r->left;
    int h = r->bottom - r->top;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    SDL_SetWindowSize(e->window, w, h);

    // resize backing store to match new pixel dimensions
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(e->window, &pw, &ph);
    if (pw > 0 && ph > 0) {
      hwnd->m_backingstore = SkSurfaces::Raster(
          SkImageInfo::MakeN32Premul(pw, ph));
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
  fprintf(stderr, "SWELL_CALL: swell_oswindow_focus\n");
  if (!hwnd) return;

  HWND top = hwnd;
  while (top->m_parent) top = (HWND)top->m_parent;

  if (top->m_oswindow) {
    SDL_RaiseWindow((SDL_Window*)top->m_oswindow);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_update_style
// ---------------------------------------------------------------------------

void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_update_style\n");
  if (!hwnd || !hwnd->m_oswindow) return;

  DWORD newstyle = hwnd->m_style;
  bool oldResize = (oldstyle & WS_THICKFRAME) != 0;
  bool newResize = (newstyle & WS_THICKFRAME) != 0;
  bool oldCaption = (oldstyle & WS_CAPTION) != 0;
  bool newCaption = (newstyle & WS_CAPTION) != 0;

  if (oldResize != newResize || oldCaption != newCaption) {
    // SDL3 doesn't support changing window flags after creation,
    // so destroy and recreate
    SDL_WindowEntry *e = find_entry_by_hwnd(hwnd);
    if (!e) return;
    SDL_Window *oldwin = e->window;
    bool wasVisible = SDL_GetWindowFlags(oldwin) & SDL_WINDOW_HIDDEN ? false : true;
    SDL_DestroyWindow(oldwin);
    remove_entry(e);
    hwnd->m_oswindow = NULL;
    hwnd->m_backingstore.reset();

    // recreate with new flags
    swell_oswindow_manage(hwnd, wasVisible);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_update_enable
// ---------------------------------------------------------------------------

void swell_oswindow_update_enable(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_update_enable\n");
  (void)hwnd;
  // SDL3 doesn't have a native enable/disable for windows
}

// ---------------------------------------------------------------------------
// swell_oswindow_update_text
// ---------------------------------------------------------------------------

void swell_oswindow_update_text(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_update_text\n");
  if (!hwnd || !hwnd->m_oswindow) return;
  SDL_SetWindowTitle((SDL_Window*)hwnd->m_oswindow, hwnd->m_title.Get());
}

// ---------------------------------------------------------------------------
// swell_oswindow_invalidate: trigger paint via backing store
// ---------------------------------------------------------------------------

void swell_oswindow_invalidate(HWND hwnd, const RECT *r)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_invalidate\n");
  if (!hwnd || !hwnd->m_oswindow) return;

  // guard against recursive paint cycles
  if (hwnd->m_paintctx) return;

  // Paint using backing store canvas, then update screen
  sk_sp<SkSurface> bs = hwnd->m_backingstore;
  if (bs) {
    SkCanvas *canvas = bs->getCanvas();
    if (canvas) {
      if (r) {
        SkRect clip = SkRect::MakeLTRB(
            (float)r->left, (float)r->top,
            (float)r->right, (float)r->bottom);
        canvas->save();
        canvas->clipRect(clip);
      }

      SWELL_internalSkiaPaint(hwnd, canvas, 0, 0, false);

      if (r) {
        canvas->restore();
      }
    }
    swell_oswindow_updatetoscreen(hwnd, r);
  }
}

// ---------------------------------------------------------------------------
// swell_oswindow_updatetoscreen: copy Skia surface to SDL texture, present
// ---------------------------------------------------------------------------

void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_updatetoscreen\n");
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
        SDL_PIXELFORMAT_BGRA8888,
        SDL_TEXTUREACCESS_STREAMING,
        pw, ph);
    if (!e->texture) return;
    e->tex_w = pw;
    e->tex_h = ph;
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

  SDL_FRect fsrc = { (float)sdlr.x, (float)sdlr.y, (float)sdlr.w, (float)sdlr.h };
  SDL_FRect fdst = { (float)sdlr.x, (float)sdlr.y, (float)sdlr.w, (float)sdlr.h };
  SDL_RenderTexture(e->renderer, e->texture, &fsrc, &fdst);
  SDL_RenderPresent(e->renderer);
}

// ---------------------------------------------------------------------------
// swell_oswindow_to_hwnd / swell_oswindow_from_hwnd
// ---------------------------------------------------------------------------

HWND swell_oswindow_to_hwnd(SWELL_OSWINDOW osw)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_to_hwnd\n");
  if (!osw) return NULL;
  SDL_WindowEntry *e = find_entry_by_window((SDL_Window*)osw);
  return e ? e->hwnd : NULL;
}

SWELL_OSWINDOW swell_oswindow_from_hwnd(HWND hwnd)
{
  fprintf(stderr, "SWELL_CALL: swell_oswindow_from_hwnd\n");
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
  if (key >= 'A' && key <= 'Z') return key;
  if (key >= '0' && key <= '9') return key;

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
// Mouse hit-test: find deepest visible child at position
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

    case SDL_EVENT_WINDOW_EXPOSED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2) {
        sk_sp<SkSurface> bs = e->hwnd->m_backingstore;
        SkCanvas *canvas = bs ? bs->getCanvas() : nullptr;
        if (canvas) {
          canvas->clear(SK_ColorTRANSPARENT);
          SWELL_internalSkiaPaint(e->hwnd, canvas, 0, 0, false);
          swell_oswindow_updatetoscreen(e->hwnd, NULL);
        }
      }
      break;
    }

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd && e->hwnd->m_hashaddestroy < 2) {
        SendMessage(e->hwnd, WM_CLOSE, 0, 0);
      }
      break;
    }

    case SDL_EVENT_WINDOW_RESIZED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        int nw = evt->window.data1;
        int nh = evt->window.data2;
        int ow = e->hwnd->m_position.right - e->hwnd->m_position.left;
        int oh = e->hwnd->m_position.bottom - e->hwnd->m_position.top;

        if (nw > 0 && nh > 0 && (nw != ow || nh != oh)) {
          e->hwnd->m_position.right = e->hwnd->m_position.left + nw;
          e->hwnd->m_position.bottom = e->hwnd->m_position.top + nh;

          // resize backing store
          int pw = 0, ph = 0;
          SDL_GetWindowSizeInPixels(e->window, &pw, &ph);
          if (pw > 0 && ph > 0) {
            e->hwnd->m_backingstore = SkSurfaces::Raster(
                SkImageInfo::MakeN32Premul(pw, ph));
          }
          if (e->texture) { SDL_DestroyTexture(e->texture); e->texture = NULL; }

          SendMessage(e->hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(nw, nh));
        }
      }
      break;
    }

    case SDL_EVENT_WINDOW_MOVED: {
      SDL_WindowEntry *e = find_entry_by_windowID(evt->window.windowID);
      if (e && e->hwnd) {
        int nx = evt->window.data1;
        int ny = evt->window.data2;
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
        HWND foc = GetFocus();
        if (foc) SendMessage(foc, WM_KILLFOCUS, 0, 0);
        SendMessage(e->hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
      }
      break;
    }

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

      LRESULT handled = SendMessage(foc, WM_KEYDOWN, vk, lp);

      // synthesize WM_CHAR for printable characters
      if (handled == 0 && vk >= 0x20 && vk < 0x7F) {
        SendMessage(foc, WM_CHAR, vk, lp);
      }
      break;
    }

    case SDL_EVENT_KEY_UP: {
      SDL_Keycode k = evt->key.key;
      SDL_Keymod mod = evt->key.mod;
      LPARAM lp = make_key_lparam(mod);

      int vk = sdl_key_to_vk(k);
      if (!vk && k < 0x80) vk = k;

      if (vk && !(k & SDLK_EXTENDED_MASK)) {
        HWND foc = GetFocus();
        if (foc) SendMessage(foc, WM_KEYUP, vk, lp);
      }
      break;
    }

    // ---- mouse button events ----

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      float mx = evt->button.x;
      float my = evt->button.y;
      Uint8 btn = evt->button.button;
      Uint8 clicks = evt->button.clicks;
      bool down = evt->button.down;

      SDL_WindowEntry *e = find_entry_by_windowID(evt->button.windowID);
      HWND target = NULL;
      if (e && e->hwnd) {
        target = e->hwnd;
        // hit-test for child
        HWND child = hittest_child(e->hwnd, mx, my);
        if (child) {
          // convert to child coords
          mx -= child->m_position.left;
          my -= child->m_position.top;
          target = child;
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
        msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
      } else if (btn == SDL_BUTTON_MIDDLE) {
        msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
      }

      if (msg) {
        SendMessage(target, msg, 0, MAKELPARAM((int)mx, (int)my));
      }
      break;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      float mx = evt->motion.x;
      float my = evt->motion.y;

      SDL_WindowEntry *e = find_entry_by_windowID(evt->motion.windowID);
      HWND target = NULL;
      if (e && e->hwnd) {
        target = e->hwnd;
        HWND child = hittest_child(e->hwnd, mx, my);
        if (child) {
          mx -= child->m_position.left;
          my -= child->m_position.top;
          target = child;
        }
      }
      if (target) {
        SendMessage(target, WM_MOUSEMOVE, 0, MAKELPARAM((int)mx, (int)my));
      }
      break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
      float wx = evt->wheel.x;
      float wy = evt->wheel.y;
      SDL_WindowEntry *e = find_entry_by_windowID(evt->wheel.windowID);
      HWND target = NULL;
      if (e && e->hwnd) {
        target = e->hwnd;
        HWND child = hittest_child(e->hwnd, evt->wheel.mouse_x, evt->wheel.mouse_y);
        if (child) target = child;
      }
      if (!target) break;

      int delta = (int)(wy * 120.0f);  // WHEEL_DELTA = 120
      if (delta != 0) {
        SendMessage(target, WM_MOUSEWHEEL,
                    MAKEWPARAM(0, (WORD)delta),
                    MAKELPARAM((int)evt->wheel.mouse_x, (int)evt->wheel.mouse_y));
      }
      int hdelta = (int)(wx * 120.0f);
      if (hdelta != 0) {
        SendMessage(target, WM_MOUSEHWHEEL,
                    MAKEWPARAM(0, (WORD)hdelta),
                    MAKELPARAM((int)evt->wheel.mouse_x, (int)evt->wheel.mouse_y));
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
  fprintf(stderr, "SWELL_CALL: SWELL_RunEvents\n");
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
  fprintf(stderr, "SWELL_CALL: SWELL_initargs\n");
  (void)argc;
  (void)argv;
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SWELL SDL3: SDL_Init failed: %s\n", SDL_GetError());
  }
}
#endif

// ---- SWELL_RunMessageLoop ----
// Defined in swell-wnd.cpp (overrides backend stub)

// ---------------------------------------------------------------------------
// SWELL_CreateXBridgeWindow, SWELL_GetOSWindow, SWELL_GetOSEvent
// ---------------------------------------------------------------------------

#ifndef SWELL_TARGET_OSX
HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *r)
{
  fprintf(stderr, "SWELL_CALL: SWELL_CreateXBridgeWindow\n");
  (void)viewpar; (void)wref; (void)r;
  return NULL;
}

void *SWELL_GetOSWindow(HWND hwnd, const char *type)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetOSWindow\n");
  if (!hwnd || !type) return NULL;
  if (strcmp(type, "!sdl") == 0)
    return (void*)hwnd->m_oswindow;
  return NULL;
}

void *SWELL_GetOSEvent(const char *type)
{
  fprintf(stderr, "SWELL_CALL: SWELL_GetOSEvent\n");
  (void)type;
  return NULL;
}
#endif

// SWELL_GetScaling256 defined in swell-gdi.cpp

#endif // SWELL_TARGET_SDL3
