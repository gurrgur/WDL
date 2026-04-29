/* Cockos SWELL (Simple/Small Win32 Emulation Layer for Linux/OSX)
   SDL backend additions. This is intentionally small and windowing-focused:
   controls and GDI still use SWELL generic code/LICE, while SDL owns top-level
   native windows and the event pump.
*/

#ifndef SWELL_PROVIDED_BY_APP

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL2/SDL_image.h>

#ifdef SDL_VIDEO_DRIVER_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <GL/glx.h>
#endif

#include "swell.h"

#ifdef SWELL_TARGET_SDL

#include "swell-internal.h"
#ifdef SWELL_SKIA_GDI
#include "swell-gdi-skia.h"
#endif
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
  bool dirty_needs_paint;
  bool prepainted_before_show;
  Uint32 prepaint_ticks;
  RECT dirty;
  int dirty_rect_count;
  RECT dirty_rects[8];
};

static WDL_PtrList<swell_sdl_window_state> s_sdl_windows;
static bool s_sdl_active;
static SDL_Event s_cur_evt;
static DWORD s_last_message_pos;
static int s_sdl_paint_depth;
static bool s_sdl_paint_event_pending;
static bool s_sdl_processing_events;
static HICON s_sdl_program_icon;
static SDL_Surface *s_sdl_program_icon_surface;

static void swell_sdl_queue_paint_event()
{
  if (!s_sdl_active || s_sdl_paint_event_pending) return;
  SDL_Event evt;
  memset(&evt, 0, sizeof(evt));
  evt.type = SDL_USEREVENT;
  evt.user.code = 'SWLP';
  if (SDL_PushEvent(&evt) >= 0) s_sdl_paint_event_pending = true;
}

static int swell_sdl_max_int(int a, int b)
{
  return a > b ? a : b;
}

static bool swell_sdl_clip_rect_to_client(RECT *r, const RECT *dirty, const RECT *cr)
{
  if (!r || !cr) return false;
  *r = dirty ? *dirty : *cr;
  if (r->left < cr->left) r->left = cr->left;
  if (r->top < cr->top) r->top = cr->top;
  if (r->right > cr->right) r->right = cr->right;
  if (r->bottom > cr->bottom) r->bottom = cr->bottom;
  return r->left < r->right && r->top < r->bottom;
}

#ifdef SDL_VIDEO_DRIVER_X11
static bool swell_sdl_get_x11_window(SDL_Window *window, Display **display, Window *xid)
{
  if (display) *display = NULL;
  if (xid) *xid = 0;
  const char *driver = SDL_GetCurrentVideoDriver();
  if (!window || !driver || strcmp(driver, "x11")) return false;

  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_X11) return false;
  if (display) *display = info.info.x11.display;
  if (xid) *xid = info.info.x11.window;
  return info.info.x11.display && info.info.x11.window;
}
#endif

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

static bool swell_sdl_ensure_texture(swell_sdl_window_state *st, int w, int h)
{
  if (!st || !st->renderer || w <= 0 || h <= 0) return false;
  if (!st->texture || st->texw != w || st->texh != h)
  {
    if (st->texture) SDL_DestroyTexture(st->texture);
    st->texture = SDL_CreateTexture(st->renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STATIC, w, h);
    st->texw = st->texture ? w : 0;
    st->texh = st->texture ? h : 0;
  }
  return st->texture != NULL;
}

static bool swell_sdl_upload_texture_rect(swell_sdl_window_state *st, LICE_IBitmap *bm, const RECT *r)
{
  if (!st || !st->texture || !bm || !r || r->left >= r->right || r->top >= r->bottom) return false;

  SDL_Rect sr = { r->left, r->top, r->right-r->left, r->bottom-r->top };
  const int src_pitch = bm->getRowSpan() * (int)sizeof(LICE_pixel);
  const unsigned char *src = (const unsigned char *)bm->getBits() + r->top * src_pitch + r->left * (int)sizeof(LICE_pixel);
  return SDL_UpdateTexture(st->texture, &sr, src, src_pitch) == 0;
}

static bool swell_sdl_upload_texture_rects(swell_sdl_window_state *st, LICE_IBitmap *bm, const RECT *fallback, const RECT *rects, int rect_count)
{
  if (rect_count > 0 && rects)
  {
    int split_area = 0;
    for (int x = 0; x < rect_count; x ++)
      split_area += (rects[x].right - rects[x].left) * (rects[x].bottom - rects[x].top);

    const int fallback_area = fallback ? (fallback->right - fallback->left) * (fallback->bottom - fallback->top) : 0;
    if (fallback_area > 0 && split_area * 4 > fallback_area * 3)
      return swell_sdl_upload_texture_rect(st, bm, fallback);

    bool ret = true;
    for (int x = 0; x < rect_count; x ++)
      ret = swell_sdl_upload_texture_rect(st, bm, rects+x) && ret;
    return ret;
  }
  return swell_sdl_upload_texture_rect(st, bm, fallback);
}

static void swell_sdl_present_texture(swell_sdl_window_state *st)
{
  if (!st || !st->renderer || !st->texture) return;
  SDL_RenderCopy(st->renderer, st->texture, NULL, NULL);
  SDL_RenderPresent(st->renderer);
}

static HWND swell_sdl_hwnd_from_id(Uint32 window_id)
{
  SDL_Window *window = SDL_GetWindowFromID(window_id);
  swell_sdl_window_state *st = window ? swell_sdl_state_from_window(window) : NULL;
  return st ? st->hwnd : NULL;
}

LRESULT SWELL_SendMouseMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void swell_sdl_load_program_icon()
{
  if (s_sdl_program_icon_surface) return;

  char buf[1024];
  GetModuleFileName(NULL, buf, sizeof(buf));
  WDL_remove_filepart(buf);
  lstrcatn(buf, "/Resources/main.png", sizeof(buf));
  s_sdl_program_icon = LoadNamedImage(buf, true);
  if (!s_sdl_program_icon)
  {
    strcpy(buf+strlen(buf)-3, "ico");
    s_sdl_program_icon = LoadNamedImage(buf, true);
  }

  BITMAP bm;
  memset(&bm, 0, sizeof(bm));
  if (s_sdl_program_icon && GetObject(s_sdl_program_icon, sizeof(bm), &bm) &&
      bm.bmBits && bm.bmWidth > 0 && bm.bmHeight > 0)
  {
    s_sdl_program_icon_surface = SDL_CreateRGBSurfaceWithFormatFrom(
      bm.bmBits, bm.bmWidth, bm.bmHeight, 32, bm.bmWidthBytes, SDL_PIXELFORMAT_ARGB8888);
  }
}

static bool swell_sdl_initwindowsys()
{
  if (s_sdl_active) return true;
  if (!SDL_WasInit(SDL_INIT_VIDEO) && !getenv("SDL_VIDEODRIVER"))
  {
    // SDL2/Wayland does not allow SWELL's generic popup-menu toplevels to
    // choose their screen position. Prefer X11 so menus can be placed.
    SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "x11,wayland", SDL_HINT_DEFAULT);
  }
  if (g_swell_appname && *g_swell_appname)
    SDL_SetHintWithPriority(SDL_HINT_APP_NAME, g_swell_appname, SDL_HINT_OVERRIDE);
  if (SDL_WasInit(SDL_INIT_VIDEO) || !SDL_InitSubSystem(SDL_INIT_VIDEO))
  {
    s_sdl_active = true;
    swell_sdl_load_program_icon();
    SDL_StartTextInput();
  }
  return s_sdl_active;
}

static bool swell_sdl_wants_dialog_treatment(HWND hwnd)
{
  if (!hwnd) return false;
  if (DialogBoxIsActive() == hwnd) return true;
  return hwnd->m_owner && !(hwnd->m_style & WS_THICKFRAME);
}

static bool swell_sdl_is_menu_window(HWND hwnd);
static HWND swell_sdl_top_owner(HWND hwnd);

static Uint32 swell_sdl_window_flags(HWND hwnd)
{
  Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
  if (hwnd->m_style & WS_THICKFRAME) flags |= SDL_WINDOW_RESIZABLE;
  if (!(hwnd->m_style & WS_CAPTION)) flags |= SDL_WINDOW_BORDERLESS;
  if (hwnd->m_style == WS_CHILD || swell_sdl_wants_dialog_treatment(hwnd)) flags |= SDL_WINDOW_SKIP_TASKBAR;
  if (swell_sdl_is_menu_window(hwnd)) flags |= SDL_WINDOW_POPUP_MENU | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_ALWAYS_ON_TOP;
  if (hwnd->m_oswindow_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  return flags;
}

static bool swell_sdl_is_menu_window(HWND hwnd)
{
  return hwnd && hwnd->m_classname && !strcmp(hwnd->m_classname, "__SWELL_MENU");
}

static void swell_sdl_get_menu_position_offset(HWND hwnd, int *xoffs, int *yoffs)
{
  if (xoffs) *xoffs = 0;
  if (yoffs) *yoffs = 0;
  if (!swell_sdl_is_menu_window(hwnd)) return;

  int top = 0, left = 0, bottom = 0, right = 0;
  HWND owner = swell_sdl_top_owner(hwnd);
  if (owner && owner->m_oswindow && swell_sdl_is_menu_window(hwnd->m_owner))
  {
    if (hwnd->m_position.left >= owner->m_position.right-1)
    {
      left = owner->m_position.left;
      top = owner->m_position.top;
    }
  }
  else if (owner && owner->m_oswindow)
  {
    left = owner->m_position.left;
    top = owner->m_position.top;
  }
  else if (hwnd->m_oswindow)
  {
    SDL_GetWindowBordersSize(hwnd->m_oswindow, &top, &left, &bottom, &right);
  }

  if (xoffs) *xoffs = left;
  if (yoffs) *yoffs = top;
}

static void swell_sdl_set_window_position(HWND hwnd, int x, int y)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  int xoffs = 0, yoffs = 0;
  swell_sdl_get_menu_position_offset(hwnd, &xoffs, &yoffs);
  SDL_SetWindowPosition(hwnd->m_oswindow, x - xoffs, y - yoffs);
}

static void swell_sdl_update_menu_position_from_window(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;

  int x = 0, y = 0, w = 0, h = 0;
  SDL_GetWindowPosition(hwnd->m_oswindow, &x, &y);
  SDL_GetWindowSize(hwnd->m_oswindow, &w, &h);

  int xoffs = 0, yoffs = 0;
  swell_sdl_get_menu_position_offset(hwnd, &xoffs, &yoffs);
  hwnd->m_position.left = x + xoffs;
  hwnd->m_position.top = y + yoffs;
  hwnd->m_position.right = hwnd->m_position.left + w;
  hwnd->m_position.bottom = hwnd->m_position.top + h;
  hwnd->m_has_had_position = true;
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

#ifdef SDL_VIDEO_DRIVER_X11
static HWND swell_sdl_x11_owner_hwnd(HWND hwnd)
{
  HWND own = hwnd ? hwnd->m_owner : NULL;
  while (own)
  {
    while (own && own->m_parent && !own->m_oswindow) own = own->m_parent;
    if (own && own->m_oswindow) return own;
    own = own ? own->m_owner : NULL;
  }
  return NULL;
}

static void swell_sdl_x11_set_metadata(HWND hwnd, SDL_Window *window)
{
  Display *display = NULL;
  Window xid = 0;
  if (!swell_sdl_get_x11_window(window, &display, &xid)) return;

  if (g_swell_appname && *g_swell_appname)
  {
    XClassHint class_hint;
    class_hint.res_name = (char *)g_swell_appname;
    class_hint.res_class = (char *)g_swell_appname;
    XSetClassHint(display, xid, &class_hint);
  }

  const bool is_menu = swell_sdl_is_menu_window(hwnd);
  HWND owner = (is_menu || swell_sdl_wants_dialog_treatment(hwnd)) ? swell_sdl_x11_owner_hwnd(hwnd) : NULL;
  Display *owner_display = NULL;
  Window owner_xid = 0;
  if (owner && swell_sdl_get_x11_window(owner->m_oswindow, &owner_display, &owner_xid) &&
      owner_display == display && owner_xid)
  {
    XSetTransientForHint(display, xid, owner_xid);
  }

  Atom wm_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  Atom wm_type_value = 0;
  if (is_menu)
    wm_type_value = XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
  else if (DialogBoxIsActive() == hwnd)
    wm_type_value = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  else if (hwnd && (hwnd->m_style & WS_CAPTION))
    wm_type_value = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
  if (wm_type && wm_type_value)
    XChangeProperty(display, xid, wm_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&wm_type_value, 1);

  XFlush(display);
}
#endif

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

  if (!hwnd->m_backingstore)
  {
#ifdef SWELL_SKIA_GDI
    hwnd->m_backingstore = SWELL_CreateSkiaRasterBitmap(cr.right, cr.bottom);
    if (!hwnd->m_backingstore) hwnd->m_backingstore = new LICE_SysBitmap;
#else
    hwnd->m_backingstore = new LICE_SysBitmap;
#endif
  }
  bool forceref = hwnd->m_backingstore->resize(cr.right, cr.bottom);

  RECT r;
  if (!swell_sdl_clip_rect_to_client(&r, forceref ? NULL : dirty, &cr))
  {
    st->invalidated = false;
    st->dirty_valid = false;
    st->dirty_needs_paint = false;
    st->dirty_rect_count = 0;
    s_sdl_paint_depth--;
    return;
  }

  st->invalidated = false;
  st->dirty_valid = false;
  st->dirty_needs_paint = false;
  st->dirty_rect_count = 0;

  LICE_SubBitmap subbm(hwnd->m_backingstore, r.left, r.top, r.right-r.left, r.bottom-r.top);
  if (subbm.getWidth() > 0 && subbm.getHeight() > 0)
  {
    void SWELL_internalLICEpaint(HWND hwnd, LICE_IBitmap *bmout, int bmout_xpos, int bmout_ypos, bool forceref);
    SWELL_internalLICEpaint(hwnd, &subbm, r.left, r.top, forceref);
  }

  if (!swell_sdl_ensure_texture(st, cr.right, cr.bottom))
  {
    s_sdl_paint_depth--;
    return;
  }

  swell_sdl_upload_texture_rect(st, hwnd->m_backingstore, &r);
  swell_sdl_present_texture(st);
  s_sdl_paint_depth--;
  if (st->invalidated) swell_sdl_queue_paint_event();
#endif
}

static bool swell_sdl_present_backingstore(HWND hwnd, const RECT *dirty)
{
#ifdef SWELL_LICE_GDI
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (!st || !hwnd || !hwnd->m_backingstore || !hwnd->m_oswindow || !st->renderer) return false;

  RECT cr;
  cr.left = cr.top = 0;
  cr.right = hwnd->m_position.right - hwnd->m_position.left;
  cr.bottom = hwnd->m_position.bottom - hwnd->m_position.top;
  if (cr.right <= 0 || cr.bottom <= 0) return false;

  if (hwnd->m_backingstore->getWidth() != cr.right || hwnd->m_backingstore->getHeight() != cr.bottom)
    return false;

  const bool need_full_upload = !st->texture || st->texw != cr.right || st->texh != cr.bottom;
  RECT r;
  if (!swell_sdl_clip_rect_to_client(&r, need_full_upload ? NULL : dirty, &cr)) return false;
  if (!swell_sdl_ensure_texture(st, cr.right, cr.bottom)) return false;

  if (!swell_sdl_upload_texture_rects(st, hwnd->m_backingstore, &r,
                                      need_full_upload ? NULL : st->dirty_rects,
                                      need_full_upload ? 0 : st->dirty_rect_count)) return false;
  swell_sdl_present_texture(st);
  return true;
#else
  return false;
#endif
}

static void swell_sdl_add_dirty_rect(swell_sdl_window_state *st, const RECT *r)
{
  if (!st) return;
  if (!r)
  {
    st->dirty_valid = false;
    st->dirty_rect_count = 0;
    return;
  }

  RECT cr;
  cr.left = cr.top = 0;
  cr.right = st->hwnd ? st->hwnd->m_position.right - st->hwnd->m_position.left : 0;
  cr.bottom = st->hwnd ? st->hwnd->m_position.bottom - st->hwnd->m_position.top : 0;
  RECT clipped;
  if (!swell_sdl_clip_rect_to_client(&clipped, r, &cr)) return;

  if (!st->dirty_valid)
  {
    st->dirty = clipped;
    st->dirty_valid = true;
  }
  else
  {
    if (clipped.left < st->dirty.left) st->dirty.left = clipped.left;
    if (clipped.top < st->dirty.top) st->dirty.top = clipped.top;
    if (clipped.right > st->dirty.right) st->dirty.right = clipped.right;
    if (clipped.bottom > st->dirty.bottom) st->dirty.bottom = clipped.bottom;
  }

  if (st->dirty_needs_paint) return;

  const int max_rects = (int)(sizeof(st->dirty_rects) / sizeof(st->dirty_rects[0]));
  for (int x = 0; x < st->dirty_rect_count; x ++)
  {
    RECT *tr = st->dirty_rects+x;
    RECT isect;
    if (WinIntersectRect(&isect, tr, &clipped) ||
        (clipped.left <= tr->right && clipped.right >= tr->left &&
         clipped.top <= tr->bottom && clipped.bottom >= tr->top))
    {
      if (clipped.left < tr->left) tr->left = clipped.left;
      if (clipped.top < tr->top) tr->top = clipped.top;
      if (clipped.right > tr->right) tr->right = clipped.right;
      if (clipped.bottom > tr->bottom) tr->bottom = clipped.bottom;
      return;
    }
  }
  if (st->dirty_rect_count < max_rects)
    st->dirty_rects[st->dirty_rect_count++] = clipped;
  else
    st->dirty_rect_count = 0;
}

static bool swell_sdl_paint_initial(HWND hwnd)
{
#ifdef SWELL_LICE_GDI
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (!st || !st->renderer) return false;
  swell_sdl_paint(hwnd, NULL);
  return st->texture != NULL;
#else
  return false;
#endif
}

static void swell_sdl_mark_dirty(HWND hwnd, const RECT *r)
{
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (!st) return;
  st->invalidated = true;
  st->dirty_needs_paint = true;
  st->dirty_rect_count = 0;
  swell_sdl_add_dirty_rect(st, r);
  if (!s_sdl_processing_events) swell_sdl_queue_paint_event();
}

static void swell_sdl_mark_backingstore_dirty(HWND hwnd, const RECT *r)
{
  swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
  if (!st) return;
  st->invalidated = true;
  swell_sdl_add_dirty_rect(st, r);
  if (!s_sdl_processing_events) swell_sdl_queue_paint_event();
}

static void swell_sdl_flush_paints()
{
  s_sdl_paint_event_pending = false;
  for (int x = 0; x < s_sdl_windows.GetSize(); x ++)
  {
    swell_sdl_window_state *st = s_sdl_windows.Get(x);
    if (st && st->invalidated)
    {
      if (!st->dirty_needs_paint && swell_sdl_present_backingstore(st->hwnd, st->dirty_valid ? &st->dirty : NULL))
      {
        st->invalidated = false;
        st->dirty_valid = false;
        st->dirty_needs_paint = false;
        st->dirty_rect_count = 0;
      }
      else
      {
        swell_sdl_paint(st->hwnd, st->dirty_valid ? &st->dirty : NULL);
      }
    }
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

void swell_scaling_init(bool no_auto_hidpi)
{
  if (no_auto_hidpi || g_swell_ui_scale != 256) return;
  if (!swell_sdl_initwindowsys()) return;

  const char *env_scale = getenv("SWELL_UI_SCALE");
  if (!env_scale || !*env_scale) env_scale = getenv("QT_SCALE_FACTOR");
  if (!env_scale || !*env_scale) env_scale = getenv("ELM_SCALE");
  if (env_scale && *env_scale)
  {
    const double sc = atof(env_scale);
    if (sc > 1.0 && sc < 8.0)
    {
      g_swell_ui_scale = (int)(sc * 256.0 + 0.5);
      return;
    }
  }

  int display = 0;
  HWND hwnd = SWELL_topwindows;
  while (hwnd && !hwnd->m_oswindow) hwnd = hwnd->m_next;
  if (hwnd && hwnd->m_oswindow)
  {
    const int idx = SDL_GetWindowDisplayIndex(hwnd->m_oswindow);
    if (idx >= 0) display = idx;
  }

  float ddpi = 0.0f;
  if (SDL_GetDisplayDPI(display, &ddpi, NULL, NULL) == 0 && ddpi > 100.0f && ddpi < 768.0f)
  {
    int sc = (int)(ddpi * 256.0f / 96.0f + 0.5f);
    if (sc > 256 && sc < 2048)
      g_swell_ui_scale = sc;
  }
}

void swell_oswindow_updatetoscreen(HWND hwnd, RECT *rect)
{
  swell_sdl_mark_backingstore_dirty(hwnd, rect);
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
      int create_x = r.left, create_y = r.top;
      if (swell_sdl_is_menu_window(hwnd))
      {
        int xoffs = 0, yoffs = 0;
        swell_sdl_get_menu_position_offset(hwnd, &xoffs, &yoffs);
        create_x -= xoffs;
        create_y -= yoffs;
      }
      swell_sdl_x11_menu_hints menu_hints(swell_sdl_is_menu_window(hwnd));
      SDL_Window *window = SDL_CreateWindow(hwnd->m_title.Get(), create_x, create_y, w, h, swell_sdl_window_flags(hwnd));
      if (window)
      {
        swell_sdl_window_state *st = new swell_sdl_window_state;
        memset(st, 0, sizeof(*st));
        st->hwnd = hwnd;
        st->window = window;
        st->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!st->renderer) st->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        s_sdl_windows.Add(st);
        hwnd->m_oswindow = window;
        const bool is_menu = swell_sdl_is_menu_window(hwnd);
        if (is_menu)
        {
          hwnd->m_position = r;
          hwnd->m_has_had_position = true;
        }
        else
          swell_sdl_update_position_from_window(hwnd);
        if (s_sdl_program_icon_surface) SDL_SetWindowIcon(window, s_sdl_program_icon_surface);
#ifdef SDL_VIDEO_DRIVER_X11
        swell_sdl_x11_set_metadata(hwnd, window);
#endif
        if (!is_menu)
        {
          st->prepainted_before_show = swell_sdl_paint_initial(hwnd);
          if (st->prepainted_before_show) st->prepaint_ticks = SDL_GetTicks();
        }
        SDL_ShowWindow(window);
        if (is_menu)
        {
          swell_sdl_mark_dirty(hwnd, NULL);
          swell_sdl_flush_paints();
        }
        if (hwnd->m_israised) SDL_SetWindowAlwaysOnTop(window, SDL_TRUE);
        if (wantfocus) swell_oswindow_focus(hwnd);
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
  swell_sdl_window_state *st = swell_sdl_state_from_window(wnd);
  if ((reposflag&3) == 3)
  {
    if (st && swell_sdl_is_menu_window(st->hwnd)) swell_sdl_set_window_position(st->hwnd, f.left, f.top);
    else SDL_SetWindowPosition(wnd, f.left, f.top);
  }
  else if (reposflag&1)
  {
    if (st && swell_sdl_is_menu_window(st->hwnd)) swell_sdl_set_window_position(st->hwnd, f.left, f.top);
    else SDL_SetWindowPosition(wnd, f.left, f.top);
  }
  if (reposflag&2) SDL_SetWindowSize(wnd, swell_sdl_max_int(1, f.right-f.left), swell_sdl_max_int(1, f.bottom-f.top));
}

void swell_oswindow_postresize(HWND hwnd, RECT f)
{
}

void UpdateWindow(HWND hwnd)
{
  if (hwnd)
  {
    while (hwnd && !hwnd->m_oswindow) hwnd = hwnd->m_parent;
    if (!hwnd) return;
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
    case SDLK_LCTRL:
    case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_LALT:
    case SDLK_RALT: return VK_MENU;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LGUI:
    case SDLK_RGUI: return VK_LWIN;
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
      if (swell_sdl_is_menu_window(hwnd))
        swell_sdl_update_menu_position_from_window(hwnd);
      else
      {
        hwnd->m_position.right += we->data1 - hwnd->m_position.left;
        hwnd->m_position.bottom += we->data2 - hwnd->m_position.top;
        hwnd->m_position.left = we->data1;
        hwnd->m_position.top = we->data2;
        hwnd->m_has_had_position = true;
      }
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
      swell_sdl_update_position_from_window(hwnd);
      SendMessage(hwnd, WM_SIZE, SIZE_MAXIMIZED, 0);
      swell_sdl_mark_dirty(hwnd, NULL);
    break;
    case SDL_WINDOWEVENT_RESTORED:
      hwnd->m_is_maximized = false;
      swell_sdl_update_position_from_window(hwnd);
      SendMessage(hwnd, WM_SIZE, SIZE_RESTORED, 0);
      swell_sdl_mark_dirty(hwnd, NULL);
    break;
    case SDL_WINDOWEVENT_EXPOSED:
    case SDL_WINDOWEVENT_SHOWN:
    {
      swell_sdl_window_state *st = swell_sdl_state_from_hwnd(hwnd);
      if (st && st->prepainted_before_show)
      {
        if (SDL_GetTicks() - st->prepaint_ticks < 100) break;
        st->prepainted_before_show = false;
      }
      swell_sdl_mark_dirty(hwnd, NULL);
    }
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

static int swell_sdl_wheel_delta(double amt, double scale, int axis, bool smooth)
{
  static double s_accum[2][2];
  const double smooth_min_amt = 0.125;
  if (scale <= 0.0) return 0;
  if (axis < 0 || axis > 1) axis = 0;
  double *accum = &s_accum[axis][smooth ? 1 : 0];
  if ((*accum < 0.0 && amt > 0.0) || (*accum > 0.0 && amt < 0.0))
    *accum = 0.0;
  *accum += amt;
  if (smooth && *accum > -smooth_min_amt && *accum < smooth_min_amt)
    return 0;
  const int delta = (int)(*accum * scale);
  if (delta) *accum -= delta / scale;
  return delta;
}

static void swell_sdl_send_wheel(HWND hwnd, UINT msg, int delta, POINT screen_pt)
{
  if (!hwnd || !delta) return;
  s_last_message_pos = MAKELONG(((int)screen_pt.x)&0xffff, ((int)screen_pt.y)&0xffff);
  hwnd->Retain();
  SWELL_SendMouseMessage(hwnd, msg, MAKEWPARAM(swell_sdl_mods(), delta), MAKELPARAM(screen_pt.x, screen_pt.y));
  hwnd->Release();
}

struct swell_sdl_pending_wheel
{
  HWND hwnd;
  POINT screen_pt;
  double amt;
};

static swell_sdl_pending_wheel s_sdl_pending_smooth_wheel[2];

static void swell_sdl_queue_smooth_wheel(HWND hwnd, int axis, double amt, POINT screen_pt)
{
  if (!hwnd || amt == 0.0) return;
  if (axis < 0 || axis > 1) axis = 0;

  swell_sdl_pending_wheel *p = &s_sdl_pending_smooth_wheel[axis];
  if (p->hwnd && p->hwnd != hwnd)
  {
    p->hwnd->Release();
    memset(p, 0, sizeof(*p));
  }
  if (!p->hwnd)
  {
    hwnd->Retain();
    p->hwnd = hwnd;
  }
  p->screen_pt = screen_pt;
  p->amt += amt;
}

static void swell_sdl_flush_smooth_wheel()
{
  const UINT msgs[2] = { WM_MOUSEHWHEEL, WM_MOUSEWHEEL };
  for (int axis = 0; axis < 2; axis ++)
  {
    swell_sdl_pending_wheel *p = &s_sdl_pending_smooth_wheel[axis];
    if (p->hwnd)
    {
      swell_sdl_send_wheel(p->hwnd, msgs[axis], swell_sdl_wheel_delta(p->amt, 32.0, axis, true), p->screen_pt);
      p->hwnd->Release();
      memset(p, 0, sizeof(*p));
    }
  }
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
        double x = evt->wheel.x;
        double y = evt->wheel.y;
        double scale = 120.0;
        bool smooth = false;
#if SDL_VERSION_ATLEAST(2,0,18)
        if (evt->wheel.preciseX != (float)evt->wheel.x || evt->wheel.preciseY != (float)evt->wheel.y)
        {
          x = evt->wheel.preciseX;
          y = evt->wheel.preciseY;
          scale = 16.0;
          smooth = true;
        }
#endif
        if (evt->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        {
          x = -x;
          y = -y;
        }
        if (smooth)
        {
          swell_sdl_queue_smooth_wheel(hwnd, 0, -x, screen_pt);
          swell_sdl_queue_smooth_wheel(hwnd, 1, y, screen_pt);
        }
        else
        {
          swell_sdl_send_wheel(hwnd, WM_MOUSEHWHEEL, swell_sdl_wheel_delta(-x, scale, 0, false), screen_pt);
          swell_sdl_send_wheel(hwnd, WM_MOUSEWHEEL, swell_sdl_wheel_delta(y, scale, 1, false), screen_pt);
        }
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
  s_sdl_processing_events = true;
  while (SDL_PollEvent(&evt))
  {
    swell_sdl_coalesce_motion(&evt);
    swell_sdl_on_event(&evt);
  }
  s_sdl_processing_events = false;

  swell_sdl_flush_smooth_wheel();
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
  const SDL_Keymod mod = SDL_GetModState();
  const Uint32 mouse = SDL_GetMouseState(NULL, NULL);

  if (key == VK_LBUTTON) return (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 0x8000 : 0;
  if (key == VK_MBUTTON) return (mouse & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 0x8000 : 0;
  if (key == VK_RBUTTON) return (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 0x8000 : 0;

  if (key == VK_CONTROL) return (mod & KMOD_CTRL) ? 0x8000 : 0;
  if (key == VK_MENU) return (mod & KMOD_ALT) ? 0x8000 : 0;
  if (key == VK_SHIFT) return (mod & KMOD_SHIFT) ? 0x8000 : 0;
  if (key == VK_LWIN) return (mod & KMOD_GUI) ? 0x8000 : 0;

  return 0;
}

DWORD GetMessagePos()
{
  return s_last_message_pos;
}

#ifdef SDL_VIDEO_DRIVER_X11

static const char * const bridge_class_name = "__swell_xbridgewndclass";

struct swell_sdl_bridge_state
{
  swell_sdl_bridge_state(bool needrep, Display *disp, Window native, Window curpar, HWND child);
  ~swell_sdl_bridge_state();

  Display *display;
  Window native_w;
  Window cur_parent_xid;
  HWND hwnd_child;
  bool lastvis;
  bool need_reparent;
  RECT lastrect;
  GLXContext gl_ctx;
};

static WDL_PtrList<swell_sdl_bridge_state> s_sdl_bridge_windows;
static swell_sdl_bridge_state *s_last_gl_ctx;

swell_sdl_bridge_state::swell_sdl_bridge_state(bool needrep, Display *disp, Window native, Window curpar, HWND child)
{
  display = disp;
  native_w = native;
  cur_parent_xid = curpar;
  hwnd_child = child;
  lastvis = false;
  need_reparent = needrep;
  gl_ctx = NULL;
  memset(&lastrect, 0, sizeof(lastrect));
  s_sdl_bridge_windows.Add(this);
}

swell_sdl_bridge_state::~swell_sdl_bridge_state()
{
  s_sdl_bridge_windows.DeletePtr(this);
  if (gl_ctx)
  {
    if (s_last_gl_ctx == this)
    {
      glXMakeCurrent(display, None, NULL);
      s_last_gl_ctx = NULL;
    }
    glXDestroyContext(display, gl_ctx);
    gl_ctx = NULL;
  }
  if (display && native_w)
  {
    if (!need_reparent) XReparentWindow(display, native_w, DefaultRootWindow(display), 0, 0);
    XDestroyWindow(display, native_w);
    XFlush(display);
  }
}

static Display *swell_sdl_get_any_x11_display()
{
  for (int x = 0; x < s_sdl_windows.GetSize(); x ++)
  {
    swell_sdl_window_state *st = s_sdl_windows.Get(x);
    Display *display = NULL;
    if (st && swell_sdl_get_x11_window(st->window, &display, NULL) && display) return display;
  }
  return NULL;
}

static void swell_sdl_xbridge_fit_child(swell_sdl_bridge_state *bs, HWND hwnd, bool apply_hints);

static void swell_sdl_xbridge_resize_child(swell_sdl_bridge_state *bs, HWND hwnd)
{
  swell_sdl_xbridge_fit_child(bs, hwnd, true);
}

static void swell_sdl_xbridge_map_children(swell_sdl_bridge_state *bs)
{
  if (!bs || !bs->display || !bs->native_w) return;

  Window root, par, *list = NULL;
  unsigned int nlist = 0;
  if (!XQueryTree(bs->display, bs->native_w, &root, &par, &list, &nlist)) return;
  if (list)
  {
    for (unsigned int x = 0; x < nlist; x ++)
      XMapWindow(bs->display, list[x]);
    if (nlist) XFlush(bs->display);
    XFree(list);
  }
}

static void swell_sdl_xbridge_fit_child(swell_sdl_bridge_state *bs, HWND hwnd, bool apply_hints)
{
  if (!bs || !bs->display || !bs->native_w) return;

  RECT r;
  GetClientRect(hwnd, &r);
  if (r.right <= 0 || r.bottom <= 0) return;

  Window root, par, *list = NULL;
  unsigned int nlist = 0;
  if (!XQueryTree(bs->display, bs->native_w, &root, &par, &list, &nlist)) return;
  if (!list || !nlist)
  {
    if (list) XFree(list);
    return;
  }

  if (apply_hints)
  {
    XSizeHints *hints = XAllocSizeHints();
    if (hints)
    {
      memset(hints, 0, sizeof(*hints));
      long hints_ret = 0;
      XGetWMNormalHints(bs->display, list[0], hints, &hints_ret);
      if (hints->flags & PMinSize)
      {
        if (r.right < hints->min_width) r.right = hints->min_width;
        if (r.bottom < hints->min_height) r.bottom = hints->min_height;
      }
      if (hints->flags & PMaxSize)
      {
        if (hints->max_width > 0 && r.right > hints->max_width) r.right = hints->max_width;
        if (hints->max_height > 0 && r.bottom > hints->max_height) r.bottom = hints->max_height;
      }
      XFree(hints);
    }
  }

  XMapWindow(bs->display, list[0]);
  XMoveResizeWindow(bs->display, list[0], 0, 0, r.right, r.bottom);
  XFlush(bs->display);
  XFree(list);
}

static bool swell_sdl_xbridge_parent(HWND viewpar, Display **display, Window *parent_xid, HWND *parent_hwnd)
{
  if (display) *display = NULL;
  if (parent_xid) *parent_xid = 0;
  if (parent_hwnd) *parent_hwnd = NULL;

  HWND hpar = viewpar;
  while (hpar)
  {
    if (hpar->m_oswindow)
    {
      Display *disp = NULL;
      Window xid = 0;
      if (!swell_sdl_get_x11_window(hpar->m_oswindow, &disp, &xid)) return false;
      if (display) *display = disp;
      if (parent_xid) *parent_xid = xid;
      if (parent_hwnd) *parent_hwnd = hpar;
      return disp && xid;
    }
    hpar = hpar->m_parent;
  }

  Display *disp = swell_sdl_get_any_x11_display();
  if (!disp) return false;
  if (display) *display = disp;
  if (parent_xid) *parent_xid = DefaultRootWindow(disp);
  return true;
}

static LRESULT swell_sdl_xbridge_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_DESTROY:
      if (hwnd && hwnd->m_private_data)
      {
        swell_sdl_bridge_state *bs = (swell_sdl_bridge_state *)hwnd->m_private_data;
        hwnd->m_private_data = 0;
        delete bs;
      }
    break;

    case WM_TIMER:
      if (wParam == 1010)
      {
        swell_sdl_xbridge_resize_child((swell_sdl_bridge_state *)hwnd->m_private_data, hwnd);
        KillTimer(hwnd, wParam);
      }
      if (wParam != 1) break;
    case WM_MOVE:
    case WM_SIZE:
      if (hwnd && hwnd->m_private_data)
      {
        swell_sdl_bridge_state *bs = (swell_sdl_bridge_state *)hwnd->m_private_data;
        HWND h = hwnd->m_parent;
        RECT tr = hwnd->m_position;

        while (h)
        {
          RECT cr = h->m_position;
          if (h->m_oswindow)
          {
            cr.right -= cr.left;
            cr.bottom -= cr.top;
            cr.left = cr.top = 0;
          }

          if (h->m_wndproc)
          {
            NCCALCSIZE_PARAMS p = {{ cr }};
            h->m_wndproc(h, WM_NCCALCSIZE, 0, (LPARAM)&p);
            cr = p.rgrc[0];
          }

          tr.left += cr.left;
          tr.top += cr.top;
          tr.right += cr.left;
          tr.bottom += cr.top;

          if (tr.left < cr.left) tr.left = cr.left;
          if (tr.top < cr.top) tr.top = cr.top;
          if (tr.right > cr.right) tr.right = cr.right;
          if (tr.bottom > cr.bottom) tr.bottom = cr.bottom;

          if (h->m_oswindow) break;
          h = h->m_parent;
        }

        Display *display = NULL;
        Window parent_xid = 0;
        HWND parent_hwnd = NULL;
        if (!swell_sdl_xbridge_parent(h ? h : hwnd->m_parent, &display, &parent_xid, &parent_hwnd))
          break;

        if (display != bs->display)
          break;

        if (parent_xid != bs->cur_parent_xid)
          bs->need_reparent = true;

        const bool vis = IsWindowVisible(hwnd) && tr.right > tr.left && tr.bottom > tr.top;
        if (uMsg == WM_TIMER && wParam == 1 && vis)
          swell_sdl_xbridge_fit_child(bs, hwnd, false);

        if (bs->need_reparent || vis != bs->lastvis || (vis && memcmp(&tr, &bs->lastrect, sizeof(RECT))))
        {
          if (bs->lastvis && !vis)
          {
            XUnmapWindow(bs->display, bs->native_w);
            bs->lastvis = false;
          }

          if (bs->need_reparent)
          {
            XReparentWindow(bs->display, bs->native_w, parent_xid, tr.left, tr.top);
            XResizeWindow(bs->display, bs->native_w, tr.right-tr.left, tr.bottom-tr.top);
            bs->lastrect = tr;
            bs->cur_parent_xid = parent_xid;
            bs->need_reparent = false;
          }
          else if (memcmp(&tr, &bs->lastrect, sizeof(RECT)))
          {
            bs->lastrect = tr;
            XMoveResizeWindow(bs->display, bs->native_w, tr.left, tr.top, tr.right-tr.left, tr.bottom-tr.top);
          }

          if (vis && !bs->lastvis)
          {
            XMapRaised(bs->display, bs->native_w);
            swell_sdl_xbridge_fit_child(bs, hwnd, false);
            bs->lastvis = true;
          }
          XFlush(bs->display);
        }
      }
    break;

    case WM_USER+1000:
      if (hwnd && hwnd->m_private_data && wParam && lParam)
      {
        swell_sdl_bridge_state *bs = (swell_sdl_bridge_state *)hwnd->m_private_data;
        if (bs->display && bs->native_w)
        {
          Window root, par, *list = NULL;
          unsigned int nlist = 0;
          if (XQueryTree(bs->display, bs->native_w, &root, &par, &list, &nlist))
          {
            if (!list || !nlist)
            {
              if (list) XFree(list);
              return 0;
            }

            XWindowAttributes attr;
            memset(&attr, 0, sizeof(attr));
            if (XGetWindowAttributes(bs->display, list[0], &attr) && attr.width && attr.height)
            {
              *((int *)(INT_PTR)wParam) = attr.width;
              *((int *)(INT_PTR)lParam) = attr.height;
            }
            XFree(list);
          }
        }
      }
    break;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#endif

HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *r)
{
  if (wref) *wref = NULL;
#ifdef SDL_VIDEO_DRIVER_X11
  if (!wref || !r || !swell_sdl_initwindowsys()) return NULL;

  Display *display = NULL;
  Window parent_xid = 0;
  HWND parent_hwnd = NULL;
  if (!swell_sdl_xbridge_parent(viewpar, &display, &parent_xid, &parent_hwnd)) return NULL;

  const bool need_reparent = parent_hwnd == NULL;
  const int w = swell_sdl_max_int(1, r->right-r->left);
  const int h = swell_sdl_max_int(1, r->bottom-r->top);
  Window native_w = XCreateWindow(display, parent_xid, 0, 0, w, h, 0,
                                  CopyFromParent, InputOutput, CopyFromParent, 0, NULL);
  if (!native_w) return NULL;

  HWND hwnd = new HWND__(viewpar, 0, r, NULL, true, swell_sdl_xbridge_proc);
  swell_sdl_bridge_state *bs = new swell_sdl_bridge_state(need_reparent, display, native_w, parent_xid, hwnd);
  hwnd->m_classname = bridge_class_name;
  hwnd->m_private_data = (INT_PTR)bs;

  *wref = (void *)native_w;
  XSelectInput(display, native_w, StructureNotifyMask | SubstructureNotifyMask);
  XSync(display, False);

  SetTimer(hwnd, 1, 100, NULL);
  if (!need_reparent) SendMessage(hwnd, WM_SIZE, SIZE_RESTORED, 0);
  return hwnd;
#endif
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

static HCURSOR swell_sdl_system_cursor(SDL_SystemCursor id)
{
  static SDL_Cursor *cursors[SDL_NUM_SYSTEM_CURSORS];
  const int idx = (int)id;
  if (idx < 0 || idx >= (int)SDL_NUM_SYSTEM_CURSORS) return NULL;
  if (!cursors[idx]) cursors[idx] = SDL_CreateSystemCursor(id);
  return (HCURSOR)cursors[idx];
}

void SWELL_SetCursor(HCURSOR curs)
{
  s_last_cursor = curs;
  SDL_SetCursor((SDL_Cursor *)curs);
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

static void swell_sdl_get_hotspot_from_cur_file(const char *fn, POINT *pt)
{
  pt->x = 0;
  pt->y = 0;
  
  FILE *fp = WDL_fopenA(fn, "rb");
  if (!fp) return;
  
  unsigned char buf[32];
  // Check CUR file header: reserved(2)=0, type(2)=2 (CUR), count(2)=1
  if (fread(buf, 1, 6, fp) == 6 && 
      !buf[0] && !buf[1] &&        // reserved (must be 0)
      buf[2] == 2 && buf[3] == 0 && // type = 2 (CUR)
      buf[4] == 1 && buf[5] == 0)   // count = 1
  {
    // Read the first entry header
    // Offset 0: width (1 byte)
    // Offset 1: height (1 byte) 
    // Offset 2: color count (1 byte)
    // Offset 3: reserved (1 byte)
    // Offset 4-5: hotspot x (2 bytes, little endian)
    // Offset 6-7: hotspot y (2 bytes, little endian)
    // Offset 8-11: size of image data (4 bytes)
    // Offset 12-15: offset to image data (4 bytes)
    if (fread(buf, 1, 16, fp) == 16)
    {
      pt->x = buf[4] | (buf[5] << 8);
      pt->y = buf[6] | (buf[7] << 8);
    }
  }
  fclose(fp);
}

static SDL_Surface *swell_sdl_surface_from_image(HICON img)
{
    if (!img) return NULL;

    BITMAP bm;
    if (!GetObject(img, sizeof(bm), &bm) || !bm.bmBits || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return NULL;

    // LICE pixel format is ARGB8888; this matches the program-icon code.
    return SDL_CreateRGBSurfaceWithFormatFrom(
        bm.bmBits, bm.bmWidth, bm.bmHeight,
        32, bm.bmWidthBytes, SDL_PIXELFORMAT_ARGB8888);
}

static SDL_Surface* swell_sdl_scale_surface(SDL_Surface* src, float scale) {
    if (scale == 1.0f) return src;

    int new_w = (int)(src->w * scale);
    int new_h = (int)(src->h * scale);
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;

    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return NULL;

    if (SDL_BlitScaled(src, NULL, dst, NULL) != 0) {
        SDL_FreeSurface(dst);
        return NULL;
    }
    return dst;
}

HCURSOR SWELL_LoadCursorFromFile(const char *fn) {
    if (!swell_sdl_initwindowsys()) return NULL;

    // 1. Load with SDL_image
    SDL_Surface* surf = IMG_Load(fn);
    if (!surf) return NULL;

    // 2. Scale according to global UI scale
    float scale = g_swell_ui_scale / 256.0f;
    SDL_Surface* scaled_surf = swell_sdl_scale_surface(surf, scale);
    if (scaled_surf != surf) SDL_FreeSurface(surf);
    if (!scaled_surf) return NULL;

    // 3. Convert to optimal format for cursor (ARGB8888)
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(scaled_surf,
                                                  SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(scaled_surf);
    if (!conv) return NULL;

    // 4. Hotspot scaling
    POINT hotspot = {0,0};
    if (strstr(fn, ".cur") || strstr(fn, ".CUR"))
        swell_sdl_get_hotspot_from_cur_file(fn, &hotspot);
    hotspot.x = (int)(hotspot.x * scale);
    hotspot.y = (int)(hotspot.y * scale);

    // 5. Create cursor
    SDL_Cursor* cursor = SDL_CreateColorCursor(conv, hotspot.x, hotspot.y);
    SDL_FreeSurface(conv);
    return (HCURSOR)cursor;
}

static SWELL_CursorResourceIndex *SWELL_curmodule_cursorresource_head;

HCURSOR SWELL_LoadCursor(const char *_idx)
{
  SDL_SystemCursor id = SDL_SYSTEM_CURSOR_ARROW;
  if (_idx == IDC_NO) id = SDL_SYSTEM_CURSOR_NO;
  else if (_idx == IDC_SIZENWSE) id = SDL_SYSTEM_CURSOR_SIZENWSE;
  else if (_idx == IDC_SIZENESW) id = SDL_SYSTEM_CURSOR_SIZENESW;
  else if (_idx == IDC_SIZEALL) id = SDL_SYSTEM_CURSOR_SIZEALL;
  else if (_idx == IDC_SIZEWE) id = SDL_SYSTEM_CURSOR_SIZEWE;
  else if (_idx == IDC_SIZENS) id = SDL_SYSTEM_CURSOR_SIZENS;
  else if (_idx == IDC_HAND) id = SDL_SYSTEM_CURSOR_HAND;
  else if (_idx == IDC_IBEAM) id = SDL_SYSTEM_CURSOR_IBEAM;
  else if (_idx == IDC_UPARROW) id = SDL_SYSTEM_CURSOR_ARROW;
  else if (_idx == IDC_ARROW) id = SDL_SYSTEM_CURSOR_ARROW;
  else
  {
      SWELL_CursorResourceIndex *p = SWELL_curmodule_cursorresource_head;
      while (p)
      {
          if (p->resid == _idx)
          {
              if (p->cachedCursor) return p->cachedCursor;

              // Build the path to the cursor file
              char buf[1024];
              GetModuleFileName(NULL, buf, sizeof(buf));
              WDL_remove_filepart(buf);
              snprintf_append(buf, sizeof(buf), "/Resources/%s.cur", p->resname);

              HCURSOR curs = SWELL_LoadCursorFromFile(buf);
              if (!curs) {   // fallback to PNG
                  strcpy(buf + strlen(buf) - 3, "png");
                  curs = SWELL_LoadCursorFromFile(buf);
              }
              if (curs) {
                  p->cachedCursor = curs;
                  return curs;
              }
              break;
          }
          p = p->_next;
      }
  }
  return swell_sdl_system_cursor(id);
}

void SWELL_DestroyCursor(HCURSOR curs)
{
  if (curs)
  {
    // Only free non-system cursors
    // We need to check if it's a system cursor - one way is to track custom ones
    SDL_FreeCursor((SDL_Cursor *)curs);
  }
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
#ifdef SDL_VIDEO_DRIVER_X11
  if (h && h->m_classname == bridge_class_name && h->m_private_data)
  {
    swell_sdl_bridge_state *bs = (swell_sdl_bridge_state *)h->m_private_data;
    if (wantGL && !bs->gl_ctx)
    {
      static GLint att[] = { GLX_RGBA, None };
      XVisualInfo *vi = glXChooseVisual(bs->display, 0, att);
      if (vi)
      {
        bs->gl_ctx = glXCreateContext(bs->display, vi, NULL, 1);
        XFree(vi);
      }
    }
    else if (!wantGL && bs->gl_ctx)
    {
      if (s_last_gl_ctx == bs)
      {
        glXMakeCurrent(bs->display, None, NULL);
        s_last_gl_ctx = NULL;
      }
      glXDestroyContext(bs->display, bs->gl_ctx);
      bs->gl_ctx = NULL;
    }
  }
#endif
}

bool SWELL_GetViewGL(HWND h)
{
#ifdef SDL_VIDEO_DRIVER_X11
  return h && h->m_classname == bridge_class_name && h->m_private_data &&
    ((swell_sdl_bridge_state *)h->m_private_data)->gl_ctx != NULL;
#endif
  return false;
}

bool SWELL_SetGLContextToView(HWND h)
{
#ifdef SDL_VIDEO_DRIVER_X11
  if (h)
  {
    if (h->m_classname == bridge_class_name && h->m_private_data)
    {
      swell_sdl_bridge_state *bs = (swell_sdl_bridge_state *)h->m_private_data;
      if (bs->gl_ctx)
      {
        glXMakeCurrent(bs->display, bs->native_w, bs->gl_ctx);
        s_last_gl_ctx = bs;
        return true;
      }
    }
    return false;
  }

  if (s_last_gl_ctx)
  {
    glXMakeCurrent(s_last_gl_ctx->display, None, NULL);
    s_last_gl_ctx = NULL;
  }
  return true;
#else
  return false;
#endif
}

#endif
#endif
