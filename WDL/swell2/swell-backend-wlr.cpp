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
#include <wlr/types/wlr_output.h>
#undef static
}

#include <drm_fourcc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct swell_wlr_backend {
  wl_display *display;
  wl_event_loop *event_loop;
  wlr_backend *backend;
  wlr_renderer *renderer;
  wlr_allocator *allocator;
  bool started;
};

struct swell_wlr_window {
  HWND hwnd;
  wlr_output *output;
  wlr_texture *texture;
  int tex_w;
  int tex_h;
  swell_wlr_window *next;
};

static swell_wlr_backend g_wlr;
static swell_wlr_window *g_wlr_windows = NULL;

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
  if (!hwnd || hwnd->m_oswindow) return;
  if (!swell_wlr_init()) return;

  const int w = wdl_max(hwnd->m_position.right - hwnd->m_position.left, 1);
  const int h = wdl_max(hwnd->m_position.bottom - hwnd->m_position.top, 1);
  wlr_output *output = wlr_wl_output_create(g_wlr.backend);
  if (!output) {
    fprintf(stderr, "SWELL WLR: wlr_wl_output_create failed\n");
    return;
  }
  if (!wlr_output_init_render(output, g_wlr.allocator, g_wlr.renderer)) {
    fprintf(stderr, "SWELL WLR: wlr_output_init_render failed\n");
    return;
  }

  swell_wlr_window *win = new swell_wlr_window();
  memset(win, 0, sizeof(*win));
  win->hwnd = hwnd;
  win->output = output;
  win->next = g_wlr_windows;
  g_wlr_windows = win;
  hwnd->m_oswindow = win;

  swell_wlr_configure_output(win, w, h);
  swell_wlr_make_backingstore(hwnd, w, h);

  if (wantFocus) {
    g_swell_focused_oswindow_hwnd = hwnd;
    g_swell_foreground = hwnd;
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
  swell_wlr_configure_output(win, w, h);
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
  }
}

void swell_oswindow_update_owner(HWND hwnd) { (void)hwnd; }
void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle)
{
  (void)oldstyle;
  if (hwnd && hwnd->m_oswindow) swell_oswindow_manage(hwnd, false);
}
void swell_oswindow_update_enable(HWND hwnd) { (void)hwnd; }

void swell_oswindow_update_text(HWND hwnd)
{
  if (!hwnd || !hwnd->m_oswindow) return;
  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  wlr_wl_output_set_title(win->output,
      hwnd->m_title.Get()[0] ? hwnd->m_title.Get() : "SWELL");
}

void swell_oswindow_invalidate(HWND hwnd, const RECT *r)
{
  (void)hwnd;
  (void)r;
}

void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r)
{
  (void)r;
  if (!hwnd || !hwnd->m_oswindow || !hwnd->m_backingstore || !g_wlr.renderer)
    return;

  swell_wlr_window *win = (swell_wlr_window *)hwnd->m_oswindow;
  SkPixmap pm;
  if (!hwnd->m_backingstore->peekPixels(&pm)) return;
  const int w = pm.width();
  const int h = pm.height();
  if (w <= 0 || h <= 0) return;

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
  if (!win->texture) return;

  wlr_output_state state;
  wlr_output_state_init(&state);
  int buffer_age = 0;
  wlr_render_pass *pass = wlr_output_begin_render_pass(win->output, &state, NULL);
  (void)buffer_age;
  if (!pass) {
    wlr_output_state_finish(&state);
    return;
  }

  wlr_render_rect_options clear = {};
  clear.box = { 0, 0, w, h };
  clear.color = { 0.0f, 0.0f, 0.0f, 0.0f };
  clear.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
  wlr_render_pass_add_rect(pass, &clear);

  wlr_render_texture_options tex = {};
  tex.texture = win->texture;
  tex.dst_box = { 0, 0, w, h };
  tex.filter_mode = WLR_SCALE_FILTER_NEAREST;
  tex.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
  wlr_render_pass_add_texture(pass, &tex);

  if (wlr_render_pass_submit(pass))
    wlr_output_commit_state(win->output, &state);
  wlr_output_state_finish(&state);
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
