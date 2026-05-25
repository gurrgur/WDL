/*
  SWELL2 wlroots OS backend — native Wayland-only backend.

  This backend runs a tiny nested wlroots compositor with one Wayland-backend
  output per SWELL top-level HWND. SWELL paints into its usual Skia raster
  backing store; frames are uploaded as wlroots textures and rendered to the
  host Wayland toplevel.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif

#include "swell-internal.h"

extern "C" {
// wlroots headers use C99 array parameters like float m[static 9], which are
// not accepted by C++. Strip the C99-only qualifier while including headers.
#define static
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer.h>
#undef static
}

#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct swell_wlr_backend {
  wl_display *display;
  wl_event_loop *event_loop;
  wlr_backend *backend;
  wlr_renderer *renderer;
  wlr_allocator *allocator;
  wl_listener new_input;
  bool started;
  bool listening;
  double pointer_x;
  double pointer_y;
  HWND pointer_host;
};

struct swell_wlr_window {
  HWND hwnd;
  HWND host_hwnd;
  wlr_output *output;
  wlr_texture *texture;
  int tex_w;
  int tex_h;
  swell_wlr_window *next;
};

static swell_wlr_backend g_wlr;
static swell_wlr_window *g_wlr_windows = NULL;

static swell_wlr_window *swell_wlr_find_output(HWND hwnd);

struct swell_wlr_pointer {
  wlr_pointer *pointer;
  wl_listener motion;
  wl_listener motion_absolute;
  wl_listener button;
  wl_listener axis;
  wl_listener destroy;
};

static swell_wlr_window *swell_wlr_first_output()
{
  for (swell_wlr_window *w = g_wlr_windows; w; w = w->next)
    if (w->output) return w;
  return NULL;
}

static void swell_wlr_pointer_host_size(HWND host, int *w, int *h)
{
  int ww = 1, hh = 1;
  if (host) {
    ww = host->m_position.right - host->m_position.left;
    hh = host->m_position.bottom - host->m_position.top;
  }
  if (ww < 1) ww = 1;
  if (hh < 1) hh = 1;
  if (w) *w = ww;
  if (h) *h = hh;
}

static HWND swell_wlr_pointer_host()
{
  if (g_wlr.pointer_host && swell_wlr_find_output(g_wlr.pointer_host))
    return g_wlr.pointer_host;
  if (g_swell_focused_oswindow_hwnd &&
      swell_wlr_find_output(g_swell_focused_oswindow_hwnd))
    return g_swell_focused_oswindow_hwnd;
  swell_wlr_window *first = swell_wlr_first_output();
  return first ? first->hwnd : NULL;
}

static HWND swell_wlr_target_from_pointer(int *local_x, int *local_y,
                                          POINT *screen_pt)
{
  HWND host = swell_wlr_pointer_host();
  if (!host) return NULL;

  int sx = host->m_position.left + (int)g_wlr.pointer_x;
  int sy = host->m_position.top + (int)g_wlr.pointer_y;
  if (screen_pt) {
    screen_pt->x = sx;
    screen_pt->y = sy;
  }

  HWND target = GetCapture();
  if (!target) target = WindowFromPoint({ sx, sy });
  if (!target) target = host;

  RECT wr;
  if (GetWindowRect(target, &wr)) {
    sx -= wr.left;
    sy -= wr.top;
  }
  if (local_x) *local_x = sx;
  if (local_y) *local_y = sy;
  return target;
}

static void swell_wlr_send_motion()
{
  int x = 0, y = 0;
  HWND target = swell_wlr_target_from_pointer(&x, &y, NULL);
  if (!target) return;
  SendMessage(target, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
  SendMessage(target, WM_SETCURSOR, (WPARAM)target,
              MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

static void swell_wlr_pointer_motion(struct wl_listener *listener, void *data)
{
  (void)listener;
  wlr_pointer_motion_event *ev = (wlr_pointer_motion_event *)data;
  HWND host = swell_wlr_pointer_host();
  int w = 1, h = 1;
  swell_wlr_pointer_host_size(host, &w, &h);
  g_wlr.pointer_x += ev->delta_x;
  g_wlr.pointer_y += ev->delta_y;
  if (g_wlr.pointer_x < 0) g_wlr.pointer_x = 0;
  if (g_wlr.pointer_y < 0) g_wlr.pointer_y = 0;
  if (g_wlr.pointer_x > w - 1) g_wlr.pointer_x = w - 1;
  if (g_wlr.pointer_y > h - 1) g_wlr.pointer_y = h - 1;
  swell_wlr_send_motion();
}

static void swell_wlr_pointer_motion_absolute(struct wl_listener *listener,
                                              void *data)
{
  (void)listener;
  wlr_pointer_motion_absolute_event *ev =
      (wlr_pointer_motion_absolute_event *)data;
  HWND host = swell_wlr_pointer_host();
  int w = 1, h = 1;
  swell_wlr_pointer_host_size(host, &w, &h);
  g_wlr.pointer_x = ev->x * (double)w;
  g_wlr.pointer_y = ev->y * (double)h;
  swell_wlr_send_motion();
}

static void swell_wlr_pointer_button(struct wl_listener *listener, void *data)
{
  (void)listener;
  wlr_pointer_button_event *ev = (wlr_pointer_button_event *)data;
  const bool down = ev->state == WL_POINTER_BUTTON_STATE_PRESSED;

  int x = 0, y = 0;
  POINT sp = { 0, 0 };
  HWND target = swell_wlr_target_from_pointer(&x, &y, &sp);
  if (!target) return;

  if (down) {
    HWND host = swell_wlr_pointer_host();
    if (host) swell_oswindow_focus(host);
    if (IsWindowEnabled(target))
      SendMessage(target, WM_MOUSEACTIVATE, 0, 0);
  }

  UINT msg = 0;
  if (ev->button == BTN_LEFT)
    msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
  else if (ev->button == BTN_RIGHT)
    msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
  else if (ev->button == BTN_MIDDLE)
    msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
  if (msg) SendMessage(target, msg, 0, MAKELPARAM(x, y));
}

static void swell_wlr_pointer_axis(struct wl_listener *listener, void *data)
{
  (void)listener;
  wlr_pointer_axis_event *ev = (wlr_pointer_axis_event *)data;
  int x = 0, y = 0;
  POINT sp = { 0, 0 };
  HWND target = swell_wlr_target_from_pointer(&x, &y, &sp);
  if (!target) return;

  int delta = (int)(-ev->delta * 12.0);
  if (ev->delta_discrete) delta = -ev->delta_discrete;
  if (!delta) return;

  if (ev->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    SendMessage(target, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)delta),
                MAKELPARAM(sp.x, sp.y));
  } else if (ev->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
    SendMessage(target, WM_MOUSEHWHEEL, MAKEWPARAM(0, (WORD)delta),
                MAKELPARAM(sp.x, sp.y));
  }
}

static void swell_wlr_pointer_destroy(struct wl_listener *listener, void *data)
{
  (void)data;
  swell_wlr_pointer *ptr = NULL;
  ptr = wl_container_of(listener, ptr, destroy);
  wl_list_remove(&ptr->motion.link);
  wl_list_remove(&ptr->motion_absolute.link);
  wl_list_remove(&ptr->button.link);
  wl_list_remove(&ptr->axis.link);
  wl_list_remove(&ptr->destroy.link);
  delete ptr;
}

static void swell_wlr_new_input(struct wl_listener *listener, void *data)
{
  (void)listener;
  wlr_input_device *dev = (wlr_input_device *)data;
  if (!dev || dev->type != WLR_INPUT_DEVICE_POINTER) return;

  swell_wlr_pointer *ptr = new swell_wlr_pointer();
  memset(ptr, 0, sizeof(*ptr));
  ptr->pointer = wlr_pointer_from_input_device(dev);

  ptr->motion.notify = swell_wlr_pointer_motion;
  wl_signal_add(&ptr->pointer->events.motion, &ptr->motion);
  ptr->motion_absolute.notify = swell_wlr_pointer_motion_absolute;
  wl_signal_add(&ptr->pointer->events.motion_absolute, &ptr->motion_absolute);
  ptr->button.notify = swell_wlr_pointer_button;
  wl_signal_add(&ptr->pointer->events.button, &ptr->button);
  ptr->axis.notify = swell_wlr_pointer_axis;
  wl_signal_add(&ptr->pointer->events.axis, &ptr->axis);
  ptr->destroy.notify = swell_wlr_pointer_destroy;
  wl_signal_add(&dev->events.destroy, &ptr->destroy);
}

static bool swell_wlr_init()
{
  if (g_wlr.started) return true;

  g_wlr.display = wl_display_create();
  if (!g_wlr.display) {
    fprintf(stderr, "SWELL WLR: wl_display_create failed\n");
    return false;
  }
  g_wlr.event_loop = wl_display_get_event_loop(g_wlr.display);
  g_wlr.backend = wlr_wl_backend_create(g_wlr.event_loop, NULL);
  if (!g_wlr.backend) {
    fprintf(stderr, "SWELL WLR: wlr_wl_backend_create failed\n");
    return false;
  }
  g_wlr.renderer = wlr_renderer_autocreate(g_wlr.backend);
  if (!g_wlr.renderer) {
    fprintf(stderr, "SWELL WLR: wlr_renderer_autocreate failed\n");
    return false;
  }
  if (!wlr_renderer_init_wl_display(g_wlr.renderer, g_wlr.display)) {
    fprintf(stderr, "SWELL WLR: wlr_renderer_init_wl_display failed\n");
    return false;
  }
  g_wlr.allocator = wlr_allocator_autocreate(g_wlr.backend, g_wlr.renderer);
  if (!g_wlr.allocator) {
    fprintf(stderr, "SWELL WLR: wlr_allocator_autocreate failed\n");
    return false;
  }
  if (!g_wlr.listening) {
    g_wlr.new_input.notify = swell_wlr_new_input;
    wl_signal_add(&g_wlr.backend->events.new_input, &g_wlr.new_input);
    g_wlr.listening = true;
  }
  if (!wlr_backend_start(g_wlr.backend)) {
    fprintf(stderr, "SWELL WLR: wlr_backend_start failed\n");
    return false;
  }

  g_wlr.started = true;
  return true;
}

static swell_wlr_window *swell_wlr_find(HWND hwnd)
{
  for (swell_wlr_window *w = g_wlr_windows; w; w = w->next)
    if (w->hwnd == hwnd) return w;
  return NULL;
}

static swell_wlr_window *swell_wlr_find_output(HWND hwnd)
{
  swell_wlr_window *win = swell_wlr_find(hwnd);
  return win && win->output ? win : NULL;
}

static HWND swell_wlr_toplevel(HWND hwnd)
{
  while (hwnd && hwnd->m_parent) hwnd = (HWND)hwnd->m_parent;
  return hwnd;
}

static HWND swell_wlr_find_owner_output(HWND hwnd)
{
  HWND h = hwnd ? (HWND)hwnd->m_owner : NULL;
  for (int guard = 0; h && guard < 1024; guard++) {
    HWND top = swell_wlr_toplevel(h);
    if (top && swell_wlr_find_output(top)) return top;
    h = h->m_owner ? (HWND)h->m_owner : (HWND)h->m_parent;
  }
  return NULL;
}

static HWND swell_wlr_host_for_embedded(HWND hwnd)
{
  HWND host = swell_wlr_find_owner_output(hwnd);
  if (host) return host;
  if (g_swell_focused_oswindow_hwnd &&
      swell_wlr_find_output(g_swell_focused_oswindow_hwnd))
    return g_swell_focused_oswindow_hwnd;
  return NULL;
}

static bool swell_wlr_should_embed(HWND hwnd)
{
  if (!hwnd || hwnd->m_parent) return false;
  if (hwnd->m_owner && swell_wlr_host_for_embedded(hwnd)) return true;
  const bool popupish =
      !(hwnd->m_style & WS_CAPTION) || (hwnd->m_style & WS_CHILD);
  return popupish && swell_wlr_host_for_embedded(hwnd);
}

static void swell_wlr_remove(swell_wlr_window *win)
{
  if (!win) return;
  swell_wlr_window **pp = &g_wlr_windows;
  while (*pp && *pp != win) pp = &(*pp)->next;
  if (*pp) *pp = win->next;
}

static void swell_wlr_configure_output(swell_wlr_window *win, int w, int h)
{
  if (!win || !win->output) return;
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  wlr_wl_output_set_title(win->output,
      win->hwnd && win->hwnd->m_title.Get()[0] ? win->hwnd->m_title.Get() : "SWELL");
  if (g_swell_appname && *g_swell_appname)
    wlr_wl_output_set_app_id(win->output, g_swell_appname);

  wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  wlr_output_state_set_custom_mode(&state, w, h, 0);
  wlr_output_commit_state(win->output, &state);
  wlr_output_state_finish(&state);
}

static void swell_wlr_make_backingstore(HWND hwnd, int w, int h)
{
  if (!hwnd) return;
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  sk_sp<SkImage> old_image;
  if (hwnd->m_backingstore)
    old_image = hwnd->m_backingstore->makeImageSnapshot();

  hwnd->m_backingstore = SkSurfaces::Raster(
      SkImageInfo::Make(w, h, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
      &g_swell_surfprops);
  if (hwnd->m_backingstore) {
    SkCanvas *c = hwnd->m_backingstore->getCanvas();
    if (c) {
      c->clear(SK_ColorTRANSPARENT);
      if (old_image) c->drawImage(old_image, 0, 0);
    }
  }
}

void swell_oswindow_manage(HWND hwnd, bool wantFocus)
{
  if (!hwnd) return;

  const bool haveOS = hwnd->m_oswindow != NULL;
  const bool wantOS = !hwnd->m_parent && hwnd->m_visible;
  if (!wantOS && haveOS) {
    swell_oswindow_destroy(hwnd);
    return;
  }
  if (!wantOS || haveOS) return;

  if (!swell_wlr_init()) return;

  const int w = wdl_max(hwnd->m_position.right - hwnd->m_position.left, 1);
  const int h = wdl_max(hwnd->m_position.bottom - hwnd->m_position.top, 1);

  swell_wlr_window *win = new swell_wlr_window();
  memset(win, 0, sizeof(*win));
  win->hwnd = hwnd;
  if (swell_wlr_should_embed(hwnd)) {
    win->host_hwnd = swell_wlr_host_for_embedded(hwnd);
  } else {
    win->output = wlr_wl_output_create(g_wlr.backend);
    if (!win->output) {
      fprintf(stderr, "SWELL WLR: wlr_wl_output_create failed\n");
      delete win;
      return;
    }
    if (!wlr_output_init_render(win->output, g_wlr.allocator, g_wlr.renderer)) {
      fprintf(stderr, "SWELL WLR: wlr_output_init_render failed\n");
      wlr_output_destroy(win->output);
      delete win;
      return;
    }
  }
  win->next = g_wlr_windows;
  g_wlr_windows = win;
  hwnd->m_oswindow = win;

  if (win->output) swell_wlr_configure_output(win, w, h);
  swell_wlr_make_backingstore(hwnd, w, h);

  if (wantFocus) {
    g_swell_focused_oswindow_hwnd = hwnd;
    g_swell_foreground = hwnd;
    if (win->output) g_wlr.pointer_host = hwnd;
  }
}

void swell_oswindow_destroy(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  hwnd->m_oswindow = NULL;
  hwnd->m_backingstore.reset();
  swell_wlr_remove(win);
  if (win->texture) wlr_texture_destroy(win->texture);
  if (win->output) wlr_output_destroy(win->output);
  delete win;
}

void swell_oswindow_resize(HWND hwnd, int reposflag, RECT *r)
{
  (void)reposflag;
  if (!hwnd || !hwnd->m_oswindow || !r) return;
  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  const int w = wdl_max(r->right - r->left, 1);
  const int h = wdl_max(r->bottom - r->top, 1);
  if (win->output) swell_wlr_configure_output(win, w, h);
  swell_wlr_make_backingstore(hwnd, w, h);
  if (win->texture) {
    wlr_texture_destroy(win->texture);
    win->texture = NULL;
    win->tex_w = win->tex_h = 0;
  }
}

void swell_oswindow_focus(HWND hwnd)
{
  if (!hwnd) {
    g_swell_focused_oswindow_hwnd = NULL;
    return;
  }
  while (hwnd->m_parent) hwnd = (HWND)hwnd->m_parent;
  if (hwnd->m_oswindow) {
    g_swell_focused_oswindow_hwnd = hwnd;
    g_swell_foreground = hwnd;
    if (swell_wlr_find_output(hwnd)) g_wlr.pointer_host = hwnd;
  }
}

static void swell_wlr_reclassify_window(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow || !hwnd->m_visible || hwnd->m_parent)
    return;

  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  const bool wants_embed = swell_wlr_should_embed(hwnd);
  const bool is_embed = win && !win->output;
  if (wants_embed == is_embed) {
    if (is_embed) win->host_hwnd = swell_wlr_host_for_embedded(hwnd);
    return;
  }

  swell_oswindow_destroy(hwnd);
  swell_oswindow_manage(hwnd, false);
}

void swell_oswindow_update_owner(HWND hwnd)
{
  swell_wlr_reclassify_window(hwnd);
}

void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle)
{
  (void)oldstyle;
  swell_wlr_reclassify_window(hwnd);
}
void swell_oswindow_update_enable(HWND hwnd) { (void)hwnd; }

void swell_oswindow_update_text(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  if (win->output)
    wlr_wl_output_set_title(win->output,
        hwnd->m_title.Get()[0] ? hwnd->m_title.Get() : "SWELL");
}

void swell_oswindow_invalidate(HWND hwnd, const RECT *r)
{
  (void)hwnd;
  (void)r;
}

static bool swell_wlr_update_texture(swell_wlr_window *win)
{
  if (!win || !win->hwnd || !win->hwnd->m_backingstore || !g_wlr.renderer)
    return false;
  SkPixmap pm;
  if (!win->hwnd->m_backingstore->peekPixels(&pm)) return false;
  const int w = pm.width();
  const int h = pm.height();
  if (w <= 0 || h <= 0) return false;

  if (!win->texture || win->tex_w != w || win->tex_h != h) {
    if (win->texture) wlr_texture_destroy(win->texture);
    win->texture = wlr_texture_from_pixels(g_wlr.renderer, DRM_FORMAT_ARGB8888,
                                           (uint32_t)pm.rowBytes(),
                                           (uint32_t)w, (uint32_t)h,
                                           pm.addr());
    win->tex_w = win->texture ? w : 0;
    win->tex_h = win->texture ? h : 0;
  } else {
    // wlroots 0.20 has no direct pixels-update API; recreate to keep content
    // correct until this backend grows a reusable shm/wlr_buffer upload path.
    wlr_texture_destroy(win->texture);
    win->texture = wlr_texture_from_pixels(g_wlr.renderer, DRM_FORMAT_ARGB8888,
                                           (uint32_t)pm.rowBytes(),
                                           (uint32_t)w, (uint32_t)h,
                                           pm.addr());
  }
  return win->texture != NULL;
}

static void swell_wlr_add_window_texture(wlr_render_pass *pass,
                                         swell_wlr_window *win,
                                         int x, int y)
{
  if (!pass || !win || !win->texture) return;
  wlr_render_texture_options tex = {};
  tex.texture = win->texture;
  tex.dst_box = { x, y, win->tex_w, win->tex_h };
  tex.filter_mode = WLR_SCALE_FILTER_NEAREST;
  tex.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
  wlr_render_pass_add_texture(pass, &tex);
}

static void swell_wlr_present_output(swell_wlr_window *host)
{
  if (!host || !host->output || !host->texture) return;

  const int w = host->tex_w;
  const int h = host->tex_h;
  if (w <= 0 || h <= 0) return;

  wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_render_pass *pass = wlr_output_begin_render_pass(host->output, &state, NULL);
  if (!pass) {
    wlr_output_state_finish(&state);
    return;
  }

  wlr_render_rect_options clear = {};
  clear.box = { 0, 0, w, h };
  clear.color = { 0.0f, 0.0f, 0.0f, 0.0f };
  clear.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
  wlr_render_pass_add_rect(pass, &clear);

  swell_wlr_add_window_texture(pass, host, 0, 0);

  for (HWND overlay = g_swell_top_level_list; overlay; overlay = overlay->m_next) {
    if (!overlay || overlay == host->hwnd || !overlay->m_visible ||
        !overlay->m_oswindow)
      continue;
    swell_wlr_window *ow = (swell_wlr_window *)overlay->m_oswindow;
    if (!ow || ow->output || ow->host_hwnd != host->hwnd || !ow->texture)
      continue;
    const int ox = overlay->m_position.left - host->hwnd->m_position.left;
    const int oy = overlay->m_position.top - host->hwnd->m_position.top;
    swell_wlr_add_window_texture(pass, ow, ox, oy);
  }

  if (wlr_render_pass_submit(pass))
    wlr_output_commit_state(host->output, &state);
  wlr_output_state_finish(&state);
}

void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r)
{
  (void)r;
  if (!hwnd || !hwnd->m_oswindow) return;

  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  if (!swell_wlr_update_texture(win)) return;

  if (win->output) {
    swell_wlr_present_output(win);
    return;
  }

  swell_wlr_window *host = swell_wlr_find_output(win->host_hwnd);
  if (host) swell_wlr_present_output(host);
}

void swell_oswindow_maximize(HWND hwnd) { (void)hwnd; }

HWND swell_oswindow_to_hwnd(SWELL_OSWINDOW osw)
{
  return osw ? osw->hwnd : NULL;
}

SWELL_OSWINDOW swell_oswindow_from_hwnd(HWND hwnd)
{
  return hwnd ? hwnd->m_oswindow : NULL;
}

void SWELL_RunEvents()
{
  if (!g_wlr.started || !g_wlr.event_loop) return;
  while (wl_event_loop_dispatch(g_wlr.event_loop, 0) > 0) {}
  wl_display_flush_clients(g_wlr.display);
}

void swell_scaling_init(bool no_auto_hidpi)
{
  (void)no_auto_hidpi;
  if (g_swell_ui_scale == 0) g_swell_ui_scale = 256;
}

#ifndef SWELL_TARGET_OSX
void SWELL_initargs(int *argc, char ***argv)
{
  (void)argc;
  (void)argv;
  swell_scaling_init(false);
  int themeMode = SWELL_THEME_LIGHT;
  const char *th = getenv("SWELL_THEME");
  if (th && (!strcmp(th, "dark") || !strcmp(th, "DARK")))
    themeMode = SWELL_THEME_DARK;
  swell_theme_init(themeMode);
}
#endif

HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *r)
{
  (void)viewpar;
  (void)r;
  if (wref) *wref = NULL;
  return NULL;
}

void *SWELL_GetOSWindow(HWND hwnd, const char *type)
{
  if (!hwnd || !type) return NULL;
  if (!strcmp(type, "!wlr")) return (void *)hwnd->m_oswindow;
  if (!strcmp(type, "wl_surface") && hwnd->m_oswindow)
    return (void *)wlr_wl_output_get_surface(hwnd->m_oswindow->output);
  return NULL;
}

void *SWELL_GetOSEvent(const char *type)
{
  (void)type;
  return NULL;
}

void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
{
  (void)sourcerect;
  (void)wantWork;
  if (!r) return;
  r->left = 0;
  r->top = 0;
  r->right = 1024;
  r->bottom = 768;
}
