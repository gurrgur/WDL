/* Cockos SWELL (Simple/Small Win32 Emulation Layer for Linux/OSX)
   SDL backend additions. This is intentionally small and windowing-focused:
   controls and GDI still use SWELL generic code/LICE, while SDL owns top-level
   native windows and the event pump.
*/

#ifndef SWELL_PROVIDED_BY_APP

#include <SDL.h>

#include "swell.h"

#ifdef SWELL_TARGET_SDL

#include "swell-internal.h"
#include "swell-dlggen.h"
#include "../wdlcstring.h"

struct swell_sdl_window_state
{
  HWND hwnd;
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  int texw;
  int texh;
  bool invalidated;
  bool dirty_valid;
  RECT dirty;
};

static WDL_PtrList<swell_sdl_window_state> s_sdl_windows;
static bool s_sdl_active;
static SDL_Event s_cur_evt;
static DWORD s_last_message_pos;
static int s_sdl_paint_depth;

static int swell_sdl_max_int(int a, int b)
{
  return a > b ? a : b;
}

static swell_sdl_window_state *swell_sdl_state_from_hwnd(HWND hwnd)
{
  for (int x = 0; x < s_sdl_windows.GetSize(); x ++)
  {
    swell_sdl_window_state *st = s_sdl_windows.Get(x);
    if (st && st->hwnd == hwnd) return st;
  }
  return NULL;
}

static swell_sdl_window_state *swell_sdl_state_from_window(SDL_Window *window)
{
  for (int x = 0; x < s_sdl_windows.GetSize(); x ++)
  {
    swell_sdl_window_state *st = s_sdl_windows.Get(x);
    if (st && st->window == window) return st;
  }
  return NULL;
}

static HWND swell_sdl_hwnd_from_id(Uint32 window_id)
{
  SDL_Window *window = SDL_GetWindowFromID(window_id);
  swell_sdl_window_state *st = window ? swell_sdl_state_from_window(window) : NULL;
  return st ? st->hwnd : NULL;
}

LRESULT SWELL_SendMouseMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool swell_sdl_initwindowsys()
{
  if (s_sdl_active) return true;
  if (!SDL_WasInit(SDL_INIT_VIDEO) && !getenv("SDL_VIDEODRIVER"))
  {
    // SDL2/Wayland does not allow SWELL's generic popup-menu toplevels to
    // choose their screen position. Prefer X11 so menus can be placed.
    SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "x11,wayland", SDL_HINT_DEFAULT);
  }
  if (SDL_WasInit(SDL_INIT_VIDEO) || !SDL_InitSubSystem(SDL_INIT_VIDEO))
  {
    s_sdl_active = true;
    SDL_StartTextInput();
  }
  return s_sdl_active;
}

static Uint32 swell_sdl_window_flags(HWND hwnd)
{
  Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
  if (hwnd->m_style & WS_THICKFRAME) flags |= SDL_WINDOW_RESIZABLE;
  if (!(hwnd->m_style & WS_CAPTION)) flags |= SDL_WINDOW_BORDERLESS;
  if (hwnd->m_oswindow_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  return flags;
}

static bool swell_sdl_is_menu_window(HWND hwnd)
{
  return hwnd && hwnd->m_classname && !strcmp(hwnd->m_classname, "__SWELL_MENU");
}

class swell_sdl_x11_menu_hints
{
public:
  swell_sdl_x11_menu_hints(bool want)
  {
    m_active = want && SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "x11");
    if (!m_active) return;

    const char *old_type = SDL_GetHint(SDL_HINT_X11_WINDOW_TYPE);
    const char *old_or = SDL_GetHint(SDL_HINT_X11_FORCE_OVERRIDE_REDIRECT);
    lstrcpyn_safe(m_old_type, old_type ? old_type : "", sizeof(m_old_type));
    lstrcpyn_safe(m_old_or, old_or ? old_or : "", sizeof(m_old_or));

    SDL_SetHintWithPriority(SDL_HINT_X11_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE_POPUP_MENU", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_X11_FORCE_OVERRIDE_REDIRECT, "1", SDL_HINT_OVERRIDE);
  }

  ~swell_sdl_x11_menu_hints()
  {
    if (!m_active) return;
    SDL_SetHintWithPriority(SDL_HINT_X11_WINDOW_TYPE, m_old_type, SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_X11_FORCE_OVERRIDE_REDIRECT, m_old_or[0] ? m_old_or : "0", SDL_HINT_OVERRIDE);
  }

private:
  bool m_active;
  char m_old_type[128];
  char m_old_or[16];
};

static void swell_sdl_update_position_from_window(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  int x = 0, y = 0, w = 0, h = 0;
  SDL_GetWindowPosition(hwnd->m_oswindow, &x, &y);
  SDL_GetWindowSize(hwnd->m_oswindow, &w, &h);
  hwnd->m_position.left = x;
  hwnd->m_position.top = y;
  hwnd->m_position.right = x + w;
  hwnd->m_position.bottom = y + h;
  hwnd->m_has_had_position = true;
}

static HWND swell_sdl_top_owner(HWND hwnd)
{
  HWND own = hwnd ? hwnd->m_owner : NULL;
  while (own && own->m_parent && !own->m_oswindow) own = own->m_parent;
  while (own && own->m_owner && !own->m_oswindow)
  {
    own = own->m_owner;
    while (own && own->m_parent && !own->m_oswindow) own = own->m_parent;
  }
  return own;
}

static void swell_sdl_position_if_unset(HWND hwnd, RECT *r)
{
  if (!hwnd || hwnd->m_has_had_position || !(hwnd->m_style & WS_CAPTION)) return;
  if (swell_sdl_is_menu_window(hwnd)) return;
  if (r->left || r->top) return;

  RECT base;
  HWND own = swell_sdl_top_owner(hwnd);
  if (own && own->m_oswindow)
    base = own->m_position;
  else
    SWELL_GetViewPort(&base, NULL, true);

  const int w = r->right - r->left;
  const int h = r->bottom - r->top;
  r->left = base.left + ((base.right - base.left) - w) / 2;
  r->top = base.top + ((base.bottom - base.top) - h) / 2;
  if (r->left < base.left) r->left = base.left;
  if (r->top < base.top) r->top = base.top;
  r->right = r->left + w;
  r->bottom = r->top + h;
  hwnd->m_position = *r;
  hwnd->m_has_had_position = true;
}

static void swell_sdl_paint(HWND hwnd, const RECT *dirty)
{
#ifdef SWELL_LICE_GDI
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (!st || !hwnd || !hwnd->m_oswindow || !st->renderer) return;
  if (s_sdl_paint_depth)
  {
    st->invalidated = true;
    return;
  }
  s_sdl_paint_depth++;

  RECT cr;
  cr.left = cr.top = 0;
  cr.right = hwnd->m_position.right - hwnd->m_position.left;
  cr.bottom = hwnd->m_position.bottom - hwnd->m_position.top;
  if (cr.right <= 0 || cr.bottom <= 0)
  {
    s_sdl_paint_depth--;
    return;
  }

  if (!hwnd->m_backingstore) hwnd->m_backingstore = new LICE_SysBitmap;
  bool forceref = hwnd->m_backingstore->resize(cr.right, cr.bottom);

  RECT r = dirty ? *dirty : cr;
  if (forceref || r.left >= r.right || r.top >= r.bottom) r = cr;
  if (r.left < 0) r.left = 0;
  if (r.top < 0) r.top = 0;
  if (r.right > cr.right) r.right = cr.right;
  if (r.bottom > cr.bottom) r.bottom = cr.bottom;

  LICE_SubBitmap subbm(hwnd->m_backingstore, r.left, r.top, r.right-r.left, r.bottom-r.top);
  if (subbm.getWidth() > 0 && subbm.getHeight() > 0)
  {
    void SWELL_internalLICEpaint(HWND hwnd, LICE_IBitmap *bmout, int bmout_xpos, int bmout_ypos, bool forceref);
    SWELL_internalLICEpaint(hwnd, &subbm, r.left, r.top, forceref);
  }

  if (!st->texture || st->texw != cr.right || st->texh != cr.bottom)
  {
    if (st->texture) SDL_DestroyTexture(st->texture);
    st->texture = SDL_CreateTexture(st->renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, cr.right, cr.bottom);
    st->texw = cr.right;
    st->texh = cr.bottom;
  }
  if (!st->texture)
  {
    s_sdl_paint_depth--;
    return;
  }

  LICE_IBitmap *bm = hwnd->m_backingstore;
  SDL_UpdateTexture(st->texture, NULL, bm->getBits(), bm->getRowSpan() * (int)sizeof(LICE_pixel));
  SDL_RenderClear(st->renderer);
  SDL_RenderCopy(st->renderer, st->texture, NULL, NULL);
  SDL_RenderPresent(st->renderer);
  st->invalidated = false;
  st->dirty_valid = false;
  s_sdl_paint_depth--;
#endif
}

static void swell_sdl_mark_dirty(HWND hwnd, const RECT *r)
{
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (!st) return;
  st->invalidated = true;
  if (!r)
  {
    st->dirty_valid = false;
    return;
  }
  if (!st->dirty_valid)
  {
    st->dirty = *r;
    st->dirty_valid = true;
  }
  else
  {
    if (r->left < st->dirty.left) st->dirty.left = r->left;
    if (r->top < st->dirty.top) st->dirty.top = r->top;
    if (r->right > st->dirty.right) st->dirty.right = r->right;
    if (r->bottom > st->dirty.bottom) st->dirty.bottom = r->bottom;
  }
}

static void swell_sdl_flush_paints()
{
  for (int x = 0; x < s_sdl_windows.GetSize(); x ++)
  {
    swell_sdl_window_state *st = s_sdl_windows.Get(x);
    if (st && st->invalidated)
      swell_sdl_paint(st->hwnd, st->dirty_valid ? &st->dirty : NULL);
  }
}

void swell_oswindow_destroy(HWND hwnd)
{
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (st)
  {
    if (SWELL_focused_oswindow == st->window) SWELL_focused_oswindow = NULL;
    if (st->texture) SDL_DestroyTexture(st->texture);
    if (st->renderer) SDL_DestroyRenderer(st->renderer);
    if (st->window) SDL_DestroyWindow(st->window);
    s_sdl_windows.DeletePtr(st);
    delete st;
  }
  if (hwnd) hwnd->m_oswindow = NULL;
#ifdef SWELL_LICE_GDI
  if (hwnd)
  {
    delete hwnd->m_backingstore;
    hwnd->m_backingstore = NULL;
  }
#endif
}

void swell_oswindow_update_text(HWND hwnd)
{
  if (hwnd && hwnd->m_oswindow) SDL_SetWindowTitle(hwnd->m_oswindow, hwnd->m_title.Get());
}

void swell_oswindow_focus(HWND hwnd)
{
  if (!hwnd)
  {
    SWELL_focused_oswindow = NULL;
    return;
  }
  while (hwnd && !hwnd->m_oswindow) hwnd = hwnd->m_parent;
  if (hwnd && hwnd->m_oswindow)
  {
    SWELL_focused_oswindow = hwnd->m_oswindow;
    SDL_RaiseWindow(hwnd->m_oswindow);
  }
}

void swell_recalcMinMaxInfo(HWND hwnd)
{
}

void SWELL_initargs(int *argc, char ***argv)
{
  swell_sdl_initwindowsys();
}

void swell_oswindow_updatetoscreen(HWND hwnd, RECT *rect)
{
  swell_sdl_mark_dirty(hwnd, rect);
}

void swell_oswindow_manage(HWND hwnd, bool wantfocus)
{
  if (!hwnd) return;

  const bool isVis = hwnd->m_oswindow != NULL;
  const bool wantVis = !hwnd->m_parent && hwnd->m_visible;

  if (isVis != wantVis)
  {
    if (!wantVis)
    {
      RECT r;
      GetWindowRect(hwnd, &r);
      swell_oswindow_destroy(hwnd);
      hwnd->m_position = r;
    }
    else if (swell_sdl_initwindowsys())
    {
      RECT r = hwnd->m_position;
      swell_sdl_position_if_unset(hwnd, &r);
      const int w = swell_sdl_max_int(1, r.right-r.left);
      const int h = swell_sdl_max_int(1, r.bottom-r.top);
      swell_sdl_x11_menu_hints menu_hints(swell_sdl_is_menu_window(hwnd));
      SDL_Window *window = SDL_CreateWindow(hwnd->m_title.Get(), r.left, r.top, w, h, swell_sdl_window_flags(hwnd));
      if (window)
      {
        swell_sdl_window_state *st = new swell_sdl_window_state;
        memset(st, 0, sizeof(*st));
        st->hwnd = hwnd;
        st->window = window;
        st->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!st->renderer) st->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        s_sdl_windows.Add(st);
        hwnd->m_oswindow = window;
        swell_sdl_update_position_from_window(hwnd);
        SDL_ShowWindow(window);
        if (hwnd->m_israised) SDL_SetWindowAlwaysOnTop(window, SDL_TRUE);
        if (wantfocus) swell_oswindow_focus(hwnd);
        swell_sdl_mark_dirty(hwnd, NULL);
      }
    }
  }
  if (wantVis) swell_oswindow_update_text(hwnd);
}

void swell_oswindow_maximize(HWND hwnd, bool wantmax)
{
  if (hwnd && hwnd->m_oswindow)
  {
    if (wantmax) SDL_MaximizeWindow(hwnd->m_oswindow);
    else SDL_RestoreWindow(hwnd->m_oswindow);
  }
}

void swell_oswindow_update_style(HWND hwnd, LONG oldstyle)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  SDL_SetWindowBordered(hwnd->m_oswindow, (hwnd->m_style & WS_CAPTION) ? SDL_TRUE : SDL_FALSE);
  SDL_SetWindowResizable(hwnd->m_oswindow, (hwnd->m_style & WS_THICKFRAME) ? SDL_TRUE : SDL_FALSE);
}

void swell_oswindow_update_enable(HWND hwnd)
{
}

int SWELL_SetWindowLevel(HWND hwnd, int newlevel)
{
  int rv = 0;
  if (hwnd)
  {
    rv = hwnd->m_israised ? 1 : 0;
    hwnd->m_israised = newlevel > 0;
    if (hwnd->m_oswindow) SDL_SetWindowAlwaysOnTop(hwnd->m_oswindow, newlevel > 0 ? SDL_TRUE : SDL_FALSE);
  }
  return rv;
}

void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
{
  r->left = r->top = 0;
  r->right = 1024;
  r->bottom = 768;
  if (!swell_sdl_initwindowsys()) return;

  int display = 0;
  if (sourcerect)
  {
    SDL_Rect sr = { sourcerect->left, sourcerect->top, sourcerect->right-sourcerect->left, sourcerect->bottom-sourcerect->top };
    display = SDL_GetRectDisplayIndex(&sr);
    if (display < 0) display = 0;
  }
  SDL_Rect dr = { 0, 0, 1024, 768 };
  if ((wantWork ? SDL_GetDisplayUsableBounds(display, &dr) : SDL_GetDisplayBounds(display, &dr)) == 0)
  {
    r->left = dr.x;
    r->top = dr.y;
    r->right = dr.x + dr.w;
    r->bottom = dr.y + dr.h;
  }
}

bool GetWindowRect(HWND hwnd, RECT *r)
{
  if (!hwnd) return false;
  if (hwnd->m_oswindow)
  {
    *r = hwnd->m_position;
    return true;
  }

  r->left = r->top = 0;
  ClientToScreen(hwnd, (LPPOINT)r);
  r->right = r->left + hwnd->m_position.right - hwnd->m_position.left;
  r->bottom = r->top + hwnd->m_position.bottom - hwnd->m_position.top;
  return true;
}

void swell_oswindow_begin_resize(SWELL_OSWINDOW wnd)
{
}

void swell_oswindow_resize(SWELL_OSWINDOW wnd, int reposflag, RECT f)
{
  if (!wnd) return;
  if ((reposflag&3) == 3) SDL_SetWindowPosition(wnd, f.left, f.top);
  else if (reposflag&1) SDL_SetWindowPosition(wnd, f.left, f.top);
  if (reposflag&2) SDL_SetWindowSize(wnd, swell_sdl_max_int(1, f.right-f.left), swell_sdl_max_int(1, f.bottom-f.top));
}

void swell_oswindow_postresize(HWND hwnd, RECT f)
{
}

void UpdateWindow(HWND hwnd)
{
  if (hwnd)
  {
    swell_sdl_mark_dirty(hwnd, NULL);
    swell_sdl_flush_paints();
  }
}

void swell_oswindow_invalidate(HWND hwnd, const RECT *r)
{
  while (hwnd && !hwnd->m_oswindow) hwnd = hwnd->m_parent;
  if (!hwnd) return;
  swell_sdl_mark_dirty(hwnd, r);
}

static int swell_sdl_mods()
{
  const SDL_Keymod mod = SDL_GetModState();
  int rv = 0;
  if (mod & KMOD_SHIFT) rv |= FSHIFT;
  if (mod & KMOD_CTRL) rv |= FCONTROL;
  if (mod & KMOD_ALT) rv |= FALT;
  return rv;
}

static HWND swell_sdl_event_target(HWND hwnd)
{
  HWND foc = GetFocusIncludeMenus();
  if (foc && hwnd && IsChild(hwnd, foc)) return foc;
  if (foc && foc->m_oswindow && !(foc->m_style&WS_CAPTION)) return foc;
  return hwnd;
}

static int swell_sdl_vkey(SDL_Keycode key)
{
  if (key >= SDLK_a && key <= SDLK_z) return 'A' + (key - SDLK_a);
  if (key >= SDLK_0 && key <= SDLK_9) return '0' + (key - SDLK_0);
  if (key >= SDLK_F1 && key <= SDLK_F12) return VK_F1 + (key - SDLK_F1);
  switch (key)
  {
    case SDLK_RETURN: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_DELETE: return VK_DELETE;
  }
  return 0;
}

static int swell_sdl_key_modifiers(const SDL_KeyboardEvent *key, int vk)
{
  int modifiers = swell_sdl_mods();
  if (vk) modifiers |= FVIRTKEY;
  if (key->keysym.sym >= SDLK_a && key->keysym.sym <= SDLK_z)
    swell_is_likely_capslock = (modifiers&FSHIFT)!=0;
  return modifiers;
}

static void swell_sdl_send_key(HWND hwnd, UINT msgtype, WPARAM wParam, LPARAM lParam)
{
  if (!hwnd) return;
  MSG msg = { hwnd, msgtype, wParam, lParam, };
  INT_PTR extra_flags = 0;
  if (DialogBoxIsActive()) extra_flags |= 1;
  if (SWELLAppMain(SWELLAPP_PROCESSMESSAGE, (INT_PTR)&msg, extra_flags) <= 0)
    SendMessage(hwnd, msg.message, msg.wParam, msg.lParam);
}

static void swell_sdl_on_window_event(const SDL_WindowEvent *we)
{
  HWND hwnd = swell_sdl_hwnd_from_id(we->windowID);
  if (!hwnd) return;

  switch (we->event)
  {
    case SDL_WINDOWEVENT_CLOSE:
      if (hwnd && IsWindowEnabled(hwnd) &&
          !DestroyPopupMenus() &&
          !SendMessage(hwnd, WM_CLOSE, 0, 0))
        SendMessage(hwnd, WM_COMMAND, IDCANCEL, 0);
    break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:
      SWELL_focused_oswindow = hwnd->m_oswindow;
      swell_on_toplevel_raise(hwnd->m_oswindow);
      SendMessage(hwnd, WM_ACTIVATEAPP, 1, 0);
    break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
      if (SWELL_focused_oswindow == hwnd->m_oswindow) SWELL_focused_oswindow = NULL;
      SendMessage(hwnd, WM_ACTIVATEAPP, 0, 0);
    break;
    case SDL_WINDOWEVENT_MOVED:
    {
      hwnd->m_position.right += we->data1 - hwnd->m_position.left;
      hwnd->m_position.bottom += we->data2 - hwnd->m_position.top;
      hwnd->m_position.left = we->data1;
      hwnd->m_position.top = we->data2;
      hwnd->m_has_had_position = true;
      SendMessage(hwnd, WM_MOVE, 0, 0);
    }
    break;
    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
      hwnd->m_position.right = hwnd->m_position.left + we->data1;
      hwnd->m_position.bottom = hwnd->m_position.top + we->data2;
      SendMessage(hwnd, WM_SIZE, hwnd->m_is_maximized ? SIZE_MAXIMIZED : SIZE_RESTORED, 0);
      swell_sdl_mark_dirty(hwnd, NULL);
    break;
    case SDL_WINDOWEVENT_MAXIMIZED:
      hwnd->m_is_maximized = true;
      SendMessage(hwnd, WM_SIZE, SIZE_MAXIMIZED, 0);
    break;
    case SDL_WINDOWEVENT_RESTORED:
      hwnd->m_is_maximized = false;
      SendMessage(hwnd, WM_SIZE, SIZE_RESTORED, 0);
    break;
    case SDL_WINDOWEVENT_EXPOSED:
    case SDL_WINDOWEVENT_SHOWN:
      swell_sdl_mark_dirty(hwnd, NULL);
    break;
  }
}

static POINT swell_sdl_screen_point(Uint32 window_id, int x, int y)
{
  POINT pt = { x, y };
  int gx = 0, gy = 0;
  SDL_GetGlobalMouseState(&gx, &gy);
  pt.x = gx;
  pt.y = gy;
  return pt;
}

static void swell_sdl_calibrate_window_from_mouse(HWND hwnd, int local_x, int local_y, POINT screen_pt)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  const int w = hwnd->m_position.right - hwnd->m_position.left;
  const int h = hwnd->m_position.bottom - hwnd->m_position.top;
  hwnd->m_position.left = screen_pt.x - local_x;
  hwnd->m_position.top = screen_pt.y - local_y;
  hwnd->m_position.right = hwnd->m_position.left + w;
  hwnd->m_position.bottom = hwnd->m_position.top + h;
  hwnd->m_has_had_position = true;
}

static HWND swell_sdl_mouse_target(HWND hwnd, int x, int y)
{
  HWND cap = GetCapture();
  if (cap) return cap;
  if (!hwnd) return NULL;
  if (swell_window_wants_all_input() == hwnd) return hwnd;
  POINT pt = { x, y };
  return ChildWindowFromPoint(hwnd, pt);
}

static void swell_sdl_send_mouse(HWND hwnd, UINT msg, WPARAM wParam, POINT screen_pt)
{
  if (!hwnd) return;
  POINT client_pt = screen_pt;
  ScreenToClient(hwnd, &client_pt);
  s_last_message_pos = MAKELONG(((int)screen_pt.x)&0xffff, ((int)screen_pt.y)&0xffff);
  hwnd->Retain();
  SWELL_SendMouseMessage(hwnd, msg, wParam, MAKELPARAM(client_pt.x, client_pt.y));
  hwnd->Release();
}

static void swell_sdl_on_event(const SDL_Event *evt)
{
  s_cur_evt = *evt;
  switch (evt->type)
  {
    case SDL_WINDOWEVENT:
      swell_sdl_on_window_event(&evt->window);
    break;
    case SDL_MOUSEMOTION:
    {
      HWND hwnd = swell_sdl_hwnd_from_id(evt->motion.windowID);
      POINT screen_pt = swell_sdl_screen_point(evt->motion.windowID, evt->motion.x, evt->motion.y);
      swell_sdl_calibrate_window_from_mouse(hwnd, evt->motion.x, evt->motion.y, screen_pt);
      hwnd = swell_sdl_mouse_target(hwnd, evt->motion.x, evt->motion.y);
      swell_sdl_send_mouse(hwnd, WM_MOUSEMOVE, 0, screen_pt);
    }
    break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    {
      HWND top_hwnd = swell_sdl_hwnd_from_id(evt->button.windowID);
      POINT screen_pt = swell_sdl_screen_point(evt->button.windowID, evt->button.x, evt->button.y);
      swell_sdl_calibrate_window_from_mouse(top_hwnd, evt->button.x, evt->button.y, screen_pt);
      HWND hwnd = swell_sdl_mouse_target(top_hwnd, evt->button.x, evt->button.y);
      if (hwnd)
      {
        UINT msg = 0;
        if (evt->button.button == SDL_BUTTON_LEFT) msg = evt->type == SDL_MOUSEBUTTONDOWN ? WM_LBUTTONDOWN : WM_LBUTTONUP;
        else if (evt->button.button == SDL_BUTTON_RIGHT) msg = evt->type == SDL_MOUSEBUTTONDOWN ? WM_RBUTTONDOWN : WM_RBUTTONUP;
        else if (evt->button.button == SDL_BUTTON_MIDDLE) msg = evt->type == SDL_MOUSEBUTTONDOWN ? WM_MBUTTONDOWN : WM_MBUTTONUP;
        if (msg)
        {
          if (evt->type == SDL_MOUSEBUTTONDOWN)
          {
            if (IsWindowEnabled(hwnd)) SendMessage(hwnd, WM_MOUSEACTIVATE, 0, 0);
            if (top_hwnd && top_hwnd->m_oswindow && SWELL_focused_oswindow != top_hwnd->m_oswindow)
            {
              SWELL_focused_oswindow = top_hwnd->m_oswindow;
              swell_on_toplevel_raise(top_hwnd->m_oswindow);
            }
            if (evt->button.clicks > 1)
            {
              if (msg == WM_LBUTTONDOWN) msg = WM_LBUTTONDBLCLK;
              else if (msg == WM_RBUTTONDOWN) msg = WM_RBUTTONDBLCLK;
              else if (msg == WM_MBUTTONDOWN) msg = WM_MBUTTONDBLCLK;
            }
            SDL_CaptureMouse(SDL_TRUE);
          }

          swell_sdl_send_mouse(hwnd, msg, 0, screen_pt);

          if (evt->type == SDL_MOUSEBUTTONUP && !GetCapture())
            SDL_CaptureMouse(SDL_FALSE);
        }
      }
    }
    break;
    case SDL_MOUSEWHEEL:
    {
      HWND hwnd = swell_sdl_hwnd_from_id(evt->wheel.windowID);
      POINT local_pt;
      SDL_GetMouseState(&local_pt.x, &local_pt.y);
      POINT screen_pt = swell_sdl_screen_point(evt->wheel.windowID, local_pt.x, local_pt.y);
      swell_sdl_calibrate_window_from_mouse(hwnd, local_pt.x, local_pt.y, screen_pt);
      hwnd = swell_sdl_mouse_target(hwnd, local_pt.x, local_pt.y);
      if (hwnd)
      {
        s_last_message_pos = MAKELONG(((int)screen_pt.x)&0xffff, ((int)screen_pt.y)&0xffff);
        hwnd->Retain();
        SWELL_SendMouseMessage(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(swell_sdl_mods(), evt->wheel.y * 120), MAKELPARAM(screen_pt.x, screen_pt.y));
        hwnd->Release();
      }
    }
    break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
      HWND hwnd = swell_sdl_hwnd_from_id(evt->key.windowID);
      hwnd = swell_sdl_event_target(hwnd);
      const int vk = swell_sdl_vkey(evt->key.keysym.sym);
      if (hwnd && vk)
      {
        UINT msg = evt->type == SDL_KEYDOWN ? WM_KEYDOWN : WM_KEYUP;
        int modifiers = swell_sdl_key_modifiers(&evt->key, vk);
        if (evt->key.keysym.sym == SDLK_LALT || evt->key.keysym.sym == SDLK_RALT ||
            evt->key.keysym.sym == SDLK_LCTRL || evt->key.keysym.sym == SDLK_RCTRL ||
            evt->key.keysym.sym == SDLK_LSHIFT || evt->key.keysym.sym == SDLK_RSHIFT ||
            evt->key.keysym.sym == SDLK_LGUI || evt->key.keysym.sym == SDLK_RGUI)
        {
          msg = evt->type == SDL_KEYDOWN ? WM_SYSKEYDOWN : WM_SYSKEYUP;
        }
        swell_sdl_send_key(hwnd, msg, vk, modifiers);
      }
    }
    break;
    case SDL_TEXTINPUT:
    {
      HWND hwnd = swell_sdl_hwnd_from_id(evt->text.windowID);
      hwnd = swell_sdl_event_target(hwnd);
      if (hwnd && evt->text.text[0])
      {
        const unsigned char *p = (const unsigned char *)evt->text.text;
        while (*p)
        {
          unsigned int c = 0;
          if (*p < 0x80)
            c = *p++;
          else if ((*p & 0xe0) == 0xc0 && p[1])
          {
            c = ((*p & 0x1f) << 6) | (p[1] & 0x3f);
            p += 2;
          }
          else if ((*p & 0xf0) == 0xe0 && p[1] && p[2])
          {
            c = ((*p & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
            p += 3;
          }
          else if ((*p & 0xf8) == 0xf0 && p[1] && p[2] && p[3])
          {
            c = ((*p & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
            p += 4;
          }
          else p++;

          if (c) swell_sdl_send_key(hwnd, WM_CHAR, c, 0);
        }
      }
    }
    break;
  }
}

static void swell_sdl_coalesce_motion(SDL_Event *evt)
{
  if (evt->type != SDL_MOUSEMOTION) return;

  for (;;)
  {
    SDL_Event next;
    if (SDL_PeepEvents(&next, 1, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) != 1)
      return;
    if (next.type != SDL_MOUSEMOTION || next.motion.windowID != evt->motion.windowID)
      return;

    SDL_PollEvent(evt);
  }
}

void SWELL_RunEvents()
{
  if (!swell_sdl_initwindowsys()) return;
  SDL_Event evt;
  while (SDL_PollEvent(&evt))
  {
    swell_sdl_coalesce_motion(&evt);
    swell_sdl_on_event(&evt);
  }

  swell_sdl_flush_paints();
}

static WDL_IntKeyedArray<HANDLE> s_clip_recs(GlobalFree);
static WDL_PtrList<char> s_clip_fmts;

bool OpenClipboard(HWND hwndDlg)
{
  RegisterClipboardFormat(NULL);
  return true;
}

void CloseClipboard()
{
}

UINT EnumClipboardFormats(UINT lastfmt)
{
  int x = 0;
  for (;;)
  {
    int fmt = 0;
    if (!s_clip_recs.Enumerate(x++, &fmt)) return 0;
    if (!lastfmt) return fmt;
    if ((UINT)fmt == lastfmt) return s_clip_recs.Enumerate(x++, &fmt) ? fmt : 0;
  }
}

HANDLE GetClipboardData(UINT type)
{
  if (type == CF_TEXT && SDL_HasClipboardText())
  {
    char *txt = SDL_GetClipboardText();
    if (txt)
    {
      HANDLE h = GlobalAlloc(0, strlen(txt)+1);
      if (h) strcpy((char *)h, txt);
      SDL_free(txt);
      return h;
    }
  }
  return s_clip_recs.Get(type);
}

void EmptyClipboard()
{
  s_clip_recs.DeleteAll();
}

void SetClipboardData(UINT type, HANDLE h)
{
  if (type == CF_TEXT && h) SDL_SetClipboardText((const char *)h);
  if (h) s_clip_recs.Insert(type, h);
  else s_clip_recs.Delete(type);
}

UINT RegisterClipboardFormat(const char *desc)
{
  if (!s_clip_fmts.GetSize())
  {
    s_clip_fmts.Add(strdup("SWELL__CF_TEXT"));
    s_clip_fmts.Add(strdup("SWELL__CF_HDROP"));
  }
  if (!desc || !*desc) return 0;
  const int n = s_clip_fmts.GetSize();
  for (int x = 0; x < n; x ++)
    if (!strcmp(s_clip_fmts.Get(x), desc)) return x + 1;
  s_clip_fmts.Add(strdup(desc));
  return n + 1;
}

void GetCursorPos(POINT *pt)
{
  int x = 0, y = 0;
  SDL_GetGlobalMouseState(&x, &y);
  pt->x = x;
  pt->y = y;
}

WORD GetAsyncKeyState(int key)
{
  return 0;
}

DWORD GetMessagePos()
{
  return s_last_message_pos;
}

HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *r)
{
  if (wref) *wref = NULL;
  return NULL;
}

void SWELL_InitiateDragDrop(HWND hwnd, RECT* srcrect, const char* srcfn, void (*callback)(const char* dropfn))
{
}

void SWELL_InitiateDragDropOfFileList(HWND hwnd, RECT *srcrect, const char **srclist, int srccount, HICON icon)
{
}

void SWELL_FinishDragDrop()
{
}

static HCURSOR s_last_cursor;
static int s_cursor_vis_cnt;

void SWELL_SetCursor(HCURSOR curs)
{
  s_last_cursor = curs;
}

HCURSOR SWELL_GetCursor()
{
  return s_last_cursor;
}

HCURSOR SWELL_GetLastSetCursor()
{
  return s_last_cursor;
}

bool SWELL_IsCursorVisible()
{
  return s_cursor_vis_cnt >= 0;
}

int SWELL_ShowCursor(BOOL bShow)
{
  s_cursor_vis_cnt += bShow ? 1 : -1;
  SDL_ShowCursor(s_cursor_vis_cnt >= 0 ? SDL_ENABLE : SDL_DISABLE);
  return s_cursor_vis_cnt;
}

BOOL SWELL_SetCursorPos(int X, int Y)
{
  SDL_WarpMouseGlobal(X, Y);
  return TRUE;
}

HCURSOR SWELL_LoadCursorFromFile(const char *fn)
{
  return NULL;
}

static SWELL_CursorResourceIndex *SWELL_curmodule_cursorresource_head;

HCURSOR SWELL_LoadCursor(const char *_idx)
{
  return NULL;
}

void SWELL_Register_Cursor_Resource(const char *idx, const char *name, int hotspot_x, int hotspot_y)
{
  SWELL_CursorResourceIndex *ri = (SWELL_CursorResourceIndex*)malloc(sizeof(SWELL_CursorResourceIndex));
  ri->hotspot.x = hotspot_x;
  ri->hotspot.y = hotspot_y;
  ri->resname = name;
  ri->cachedCursor = 0;
  ri->resid = idx;
  ri->_next = SWELL_curmodule_cursorresource_head;
  SWELL_curmodule_cursorresource_head = ri;
}

int SWELL_KeyToASCII(int wParam, int lParam, int *newflags)
{
  return 0;
}

int swell_is_app_inactive()
{
  return 0;
}

void *SWELL_GetOSWindow(HWND hwnd, const char *type)
{
  if (hwnd && !strcmp(type, "SDL_Window")) return hwnd->m_oswindow;
  return NULL;
}

void *SWELL_GetOSEvent(const char *type)
{
  return !strcmp(type, "SDL_Event") ? &s_cur_evt : NULL;
}

void SWELL_SetViewGL(HWND h, char wantGL)
{
}

bool SWELL_GetViewGL(HWND h)
{
  return false;
}

bool SWELL_SetGLContextToView(HWND h)
{
  return false;
}

#endif
#endif
