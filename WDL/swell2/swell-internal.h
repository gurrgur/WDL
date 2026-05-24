#ifndef _SWELL_INTERNAL_H_
#define _SWELL_INTERNAL_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell.h"
#include "../mutex.h"
#include "../wdlstring.h"
#include "../ptrlist.h"
#include "../heapbuf.h"
#include "../assocarray.h"
#include <chrono>
#include <cstdint>

// swell-functions.h defines Polygon(a,b,c) as SWELL_Polygon which clashes
// with SkPath::Polygon. Undefine it here since all swell-internal.h consumers
// transitively include Skia headers via SkFont.h → SkPath.h.
#undef Polygon

#include <core/SkCanvas.h>
#include <core/SkSurface.h>
#include <core/SkColor.h>
#include <core/SkRect.h>
#include <core/SkPaint.h>
#include <core/SkBitmap.h>
#include <core/SkImage.h>
#include <core/SkFont.h>
#include <core/SkSurfaceProps.h>

// Shared surface properties: kRGB_H_SkPixelGeometry enables LCD subpixel AA.
static const SkSurfaceProps g_swell_surfprops(0, kRGB_H_SkPixelGeometry);

#define WDL_FastString WDL_String

// ---- macro helpers ----

#ifndef SWELL_TO_SKCOLOR
#define SWELL_TO_SKCOLOR(col, a) SkColorSetARGB((a), GetRValue(col), GetGValue(col), GetBValue(col))
#endif

#define TYPE_PEN    1
#define TYPE_BRUSH  2
#define TYPE_FONT   3
#define TYPE_BITMAP 4

#define PS_SOLID 0
#define NULL_PEN  1
#define NULL_BRUSH 2

// ---- forward declarations ----

class SWELL_MenuItem;
class buttonWindowState;
struct __SWELL_editControlState;

bool IsModalDialogBox(HWND hwnd);

extern HWND g_dlg_parent;
struct listViewState;
class SWELL_ListView_Row;
struct SWELL_ListView_Rec;
struct SWELL_ListView_Col;
struct treeViewState;
class __SWELL_ComboBoxInternalState;
class __SWELL_ComboBoxInternalState_rec;
struct tabControlState;

struct TimerInfoRec;
struct PMQ_rec;

// ---- HGDIOBJ__ ----

struct HGDIOBJ__ {
  int type;
  int additional_refcnt;
  int color;
  int wid;
  float alpha;
  void *typedata;
  bool _infreelist;
  struct HGDIOBJ__ *_next;
};

// ---- HDC__ ----

struct HDC__ {
  SkCanvas *canvas;
  sk_sp<SkSurface> surface;
  POINT surface_offs;

  RECT dirty_rect;
  bool dirty_rect_valid;

  int clip_save_count;
  int getdc_savecount;

  HGDIOBJ__ *curpen;
  HGDIOBJ__ *curbrush;
  HGDIOBJ__ *curfont;
  SkFont cached_skfont;
  const HGDIOBJ__ *cached_font_ptr;

  bool cached_fm_valid;
  float cached_fm_ascent;
  float cached_fm_descent;
  float cached_fm_rowH;
  float cached_fm_leading;

  int cur_text_color_int;
  int curbkcol;
  int curbkmode;
  float lastpos_x, lastpos_y;

  bool _infreelist;
  struct HDC__ *_next;
};

// ---- swell_gdpLocalContext ----

struct swell_gdpLocalContext {
  HDC__ ctx;
  RECT clipr;
};

// ---- SWELL_OSWINDOW ----

#ifdef SWELL_TARGET_SDL3
#include <SDL3/SDL.h>
typedef SDL_Window *SWELL_OSWINDOW;
#else
typedef void *SWELL_OSWINDOW;
#endif

// ---- HWND__ ----

struct HWND__ {
  HWND__ *m_parent;
  WDL_PtrList<HWND__> m_children;
  HWND__ *m_next;
  HWND__ *m_prev;
  HWND__ *m_owner;
  WDL_PtrList<HWND__> m_owned;

  WDL_FastString m_title;
  RECT m_position;
  DWORD m_style;
  DWORD m_exstyle;
  int m_id;
  WNDPROC m_wndproc;
  DLGPROC m_dlgproc;
  const char *m_classname;
  HFONT m_font;
  INT_PTR m_private_data;
  bool m_invalidated;
  bool m_child_invalidated;
  RECT m_dirty_rect;
  bool m_dirty_rect_valid;
  int m_perf_invalidates;
  bool m_visible;
  bool m_enabled;
  bool m_wantfocus;
  HWND__ *m_focused_child;
  HMENU m_menu;
  swell_gdpLocalContext *m_paintctx;
  int m_hashaddestroy;
  sk_sp<SkSurface> m_backingstore;
  SWELL_OSWINDOW m_oswindow;
  LONG_PTR m_userdata;
  INT_PTR m_extra[64];

  int m_oswindow_private;
  int m_oswindow_fullscreen;

  WDL_StringKeyedArray<void *> m_props;

  HWND__(HWND__ *parent, int id, const RECT *r, const char *label,
         bool visible, WNDPROC proc);
  ~HWND__();
  void Retain();
  void Release();
  int refcnt;
};

// ---- HMENU__ ----

class SWELL_MenuItem {
public:
  WDL_FastString m_name;
  int m_id;
  unsigned int m_flags;
  HMENU m_submenu;
  DWORD_PTR m_userdata;
  HICON m_checked_icon;
  HICON m_unchecked_icon;
  HBITMAP m_bitmap;
  int m_mod_flag;
  int m_mod_code;
  unsigned int m_mod_mask;

  SWELL_MenuItem() : m_id(0), m_flags(0), m_submenu(NULL), m_userdata(0),
    m_checked_icon(NULL), m_unchecked_icon(NULL), m_bitmap(NULL),
    m_mod_flag(0), m_mod_code(0), m_mod_mask(0) {}
};

struct HMENU__ {
  WDL_PtrList_DeleteOnDestroy<SWELL_MenuItem> m_items;
  HMENU__() {}
};

// ---- HIMAGELIST__ ----

struct HIMAGELIST__ {
  struct Entry {
    HGDIOBJ__ *image; // HICON or HBITMAP
    HGDIOBJ__ *mask;  // HBITMAP mask (or NULL)
  };
  WDL_PtrList_DeleteOnDestroy<Entry> m_entries;
};

// ---- HTREEITEM__ ----

struct HTREEITEM__ {
  WDL_FastString m_value;
  LPARAM m_param;
  int m_state;
  bool m_haschildren;
  int m_image;
  int m_selimage;
  WDL_PtrList<HTREEITEM__> m_children;

  HTREEITEM__() : m_param(0), m_state(0), m_haschildren(false),
    m_image(-1), m_selimage(-1) {}
  ~HTREEITEM__() {
    for (int i = 0; i < m_children.GetSize(); i++)
      delete m_children.Get(i);
  }
};

// ---- Control state types ----

struct buttonWindowState {
  HICON bitmap;
  int bitmap_mode;
  int state;
  buttonWindowState() : bitmap(NULL), bitmap_mode(0), state(0) {}
};

struct __SWELL_editControlState {
  int cursor_pos;
  int sel1, sel2;
  int cursor_state;
  int cursor_timer;
  int scroll_x, scroll_y;
  int m_sb_dragging;
  int m_sb_drag_mouse;
  int m_sb_drag_scroll;
  int m_sb_hover;
  int max_height;
  int max_width;
  int cache_linelen_w;
  int cache_linelen_strlen;
  WDL_TypedBuf<int> cache_linelen_bytes;
  bool m_disable_contextmenu;
  bool m_mouse_sel_active;
  int m_mouse_sel_anchor;

  // multiline display-line cache
  WDL_FastString ml_cached_text;
  WDL_TypedBuf<int> ml_dline_starts;
  WDL_TypedBuf<int> ml_dline_ends;
  WDL_TypedBuf<int> ml_char2dline;
  int ml_cached_w;
  int ml_cached_multiline;
  int ml_cached_rowh;
  // Sticky scrollbar bit: once a layout overflows the viewport we remember
  // that the scrollbar is present, so the next edit_prepare_layout call can
  // skip the initial wide-width layout pass and start directly at the
  // narrower (with-scrollbar) width. Without this the layout cache slot
  // alternates between two widths every frame and never hits.
  int ml_known_needs_scrollbar;
  // Cached UTF-8 character count of ml_cached_text. Recomputed only on
  // layout cache miss. Avoids WDL_utf8_get_charlen() (linear scan of the
  // whole edit buffer) every paint.
  int ml_cached_text_chars;
  WDL_TypedBuf<int> ml_dline_char_starts;
  WDL_TypedBuf<int> ml_dline_char_ends;
  WDL_TypedBuf<int> ml_dline_xidx;
  WDL_TypedBuf<int> ml_dline_widths;
  WDL_TypedBuf<int> ml_xpos;
  WDL_TypedBuf<int> ml_bpos;

  __SWELL_editControlState() : cursor_pos(0), sel1(-1), sel2(-1),
    cursor_state(0), cursor_timer(0), scroll_x(0), scroll_y(0),
    m_sb_dragging(0), m_sb_drag_mouse(0), m_sb_drag_scroll(0),
    m_sb_hover(0),
    max_height(0), max_width(0), cache_linelen_w(0),
    cache_linelen_strlen(0), m_disable_contextmenu(false),
    m_mouse_sel_active(false), m_mouse_sel_anchor(0),
    ml_cached_w(-1), ml_cached_multiline(-1), ml_cached_rowh(0),
    ml_known_needs_scrollbar(0), ml_cached_text_chars(0) {}
};

class SWELL_ListView_Row;

struct SWELL_ListView_Rec {
  char *txt;
  int image_idx;
  SWELL_ListView_Rec() : txt(NULL), image_idx(-1) {}
  ~SWELL_ListView_Rec() { free(txt); }
};

class SWELL_ListView_Row {
public:
  WDL_TypedBuf<SWELL_ListView_Rec> m_cols;
  LPARAM m_param;
  int m_tmp;
  SWELL_ListView_Row() : m_param(0), m_tmp(0) {}
  int get_img_idx(int x) const { return x >= 0 && x < m_cols.GetSize() ? m_cols.Get()[x].image_idx : 0; }
  void set_img_idx(int x, int index) {
    if (x >= 0 && x < m_cols.GetSize())
      m_cols.Get()[x].image_idx = index;
  }
};

struct SWELL_ListView_Col {
  char *name;
  int xwid;
  int sortindicator;
  int col_index;
  int fmt;
  SWELL_ListView_Col() : name(NULL), xwid(100), sortindicator(0),
    col_index(0), fmt(0) {}
  ~SWELL_ListView_Col() { free(name); }
};

enum ListViewCapMode {
  LISTVIEW_CAP_NONE = 0,
  LISTVIEW_CAP_XSCROLL,
  LISTVIEW_CAP_YSCROLL,
  LISTVIEW_CAP_DRAG,
  LISTVIEW_CAP_COLRESIZE,
  LISTVIEW_CAP_COLCLICK,
  LISTVIEW_CAP_COLREORDER,
};

struct listViewState {
  WDL_PtrList<SWELL_ListView_Row> m_data;
  WDL_TypedBuf<SWELL_ListView_Col> m_cols;
  int m_owner_data_size;
  int m_selitem;
  bool m_is_multisel;
  bool m_is_listbox;
  int m_scroll_x, m_scroll_y;
  int m_sb_dragging;
  int m_sb_drag_mouse_x, m_sb_drag_mouse_y;
  int m_sb_drag_scroll_x, m_sb_drag_scroll_y;
  int m_sb_hover;
  int m_last_row_height;
  ListViewCapMode m_capmode_state;
  int m_capmode_data1, m_capmode_data2;
  HIMAGELIST m_status_imagelist;
  int m_status_imagelist_type;
  bool hasStatusImage() const { return m_status_imagelist && m_status_imagelist_type == 1; }
  bool hasAnyImage() const { return m_status_imagelist && (m_status_imagelist_type == 2 || m_status_imagelist_type == 1); }
  int m_extended_style;
  int m_fastclick_mask;
  int m_color_bg, m_color_bg_sel, m_color_bg_sel_inactive;
  int m_color_text, m_color_text_sel, m_color_text_sel_inactive;
  int m_color_grid;
  int m_color_extras[4];

  listViewState() : m_owner_data_size(-1), m_selitem(-1),
    m_is_multisel(false), m_is_listbox(false), m_scroll_x(0), m_scroll_y(0),
    m_sb_dragging(0), m_sb_drag_mouse_x(0), m_sb_drag_mouse_y(0),
    m_sb_drag_scroll_x(0), m_sb_drag_scroll_y(0), m_sb_hover(0),
    m_last_row_height(16), m_capmode_state(LISTVIEW_CAP_NONE),
    m_capmode_data1(0), m_capmode_data2(0),
    m_status_imagelist(NULL), m_status_imagelist_type(0),
    m_extended_style(0), m_fastclick_mask(0),
    m_color_bg(0xFFFFFFFF), m_color_bg_sel(0xFF0000FF),
    m_color_bg_sel_inactive(0xFF888888),
    m_color_text(0xFF000000), m_color_text_sel(0xFFFFFFFF),
    m_color_text_sel_inactive(0xFF000000), m_color_grid(0) {
      memset(m_color_extras, 0, sizeof(m_color_extras));
    }
};

struct treeViewState {
  HTREEITEM m_root;
  HTREEITEM m_sel;
  int m_last_row_height;
  int m_scroll_x, m_scroll_y;
  int m_sb_dragging;
  int m_sb_drag_mouse;
  int m_sb_drag_scroll;
  int m_sb_hover;
  int m_capmode;
  int m_color_bg, m_color_text;

  treeViewState() : m_sel(NULL), m_last_row_height(16),
    m_scroll_x(0), m_scroll_y(0),
    m_sb_dragging(0), m_sb_drag_mouse(0), m_sb_drag_scroll(0), m_sb_hover(0),
    m_capmode(0),
    m_color_bg(0xFFFFFFFF), m_color_text(0xFF000000) {
    m_root = new HTREEITEM__();
    m_root->m_state = TVIS_EXPANDED;
  }
  ~treeViewState() { delete m_root; }
};

class __SWELL_ComboBoxInternalState_rec {
public:
  char *desc;
  LPARAM parm;
  __SWELL_ComboBoxInternalState_rec() : desc(NULL), parm(0) {}
  ~__SWELL_ComboBoxInternalState_rec() { free(desc); }
};

class __SWELL_ComboBoxInternalState {
public:
  int selidx;
  bool dropdown_armed;
  WDL_PtrList_DeleteOnDestroy<__SWELL_ComboBoxInternalState_rec> items;
  __SWELL_editControlState editstate;
  __SWELL_ComboBoxInternalState() : selidx(-1), dropdown_armed(false) {}
};

struct tabControlState {
  int m_curtab;
  WDL_PtrList<char> m_tabs;
  tabControlState() : m_curtab(-1) {}
};

// ---- Timer ----

struct TimerInfoRec {
  HWND hwnd;
  UINT_PTR timerid;
  UINT interval;
  DWORD lastFire;
  TIMERPROC tProc;
  int refcnt;
  TimerInfoRec *_next;

  TimerInfoRec() : hwnd(NULL), timerid(0), interval(0), lastFire(0),
    tProc(NULL), refcnt(0), _next(NULL) {}
};

// ---- PostMessage queue ----

struct PMQ_rec {
  HWND hwnd;
  UINT msg;
  WPARAM wParam;
  LPARAM lParam;
  PMQ_rec *_next;

  PMQ_rec() : hwnd(NULL), msg(0), wParam(0), lParam(0), _next(NULL) {}
};

// ---- Global state (internal) ----

extern HWND g_swell_capture;
extern HWND g_swell_focus;
extern HWND g_swell_foreground;
extern HWND g_swell_focused_oswindow_hwnd;

extern HWND g_swell_top_level_list;       // doubly-linked list of top-level windows
extern HWND g_swell_top_level_list_end;
extern int g_swell_event_dispatch_depth;

extern int g_swell_ui_scale;              // DPI scaling: 256 = 1.0x
extern bool g_swell_subpixel_text;       // subpixel positioning + LCD AA
#define SWELL_UI_SCALE(x) (((x)*g_swell_ui_scale)/256)
void swell_scaling_init(bool no_auto_hidpi); // auto-detect DPI via hidden SDL test window

// swell keeps public coordinates in physical device pixels. SDL backends are
// not uniform here: Wayland reports logical/CSS pixels, while X11 reports
// physical pixels even when display scale is not 100%.
inline bool swell_sdl_uses_logical_coords()
{
#ifdef SWELL_TARGET_SDL3
  const char *driver = SDL_GetCurrentVideoDriver();
  return !driver || strcmp(driver, "x11");
#else
  return true;
#endif
}

inline float swell_phys_to_log(float phys) {
  return swell_sdl_uses_logical_coords() ? (phys * 256.0f) / g_swell_ui_scale : phys;
}

inline float swell_log_to_phys(float log) {
  return swell_sdl_uses_logical_coords() ? (log * g_swell_ui_scale) / 256.0f : log;
}

inline void swell_phys_rect_to_log(const RECT *phys, RECT *log) {
  log->left = swell_phys_to_log(phys->left);
  log->top = swell_phys_to_log(phys->top);
  log->right = swell_phys_to_log(phys->right);
  log->bottom = swell_phys_to_log(phys->bottom);
}

inline void swell_log_rect_to_phys(const RECT *log, RECT *phys) {
  phys->left = swell_log_to_phys(log->left);
  phys->top = swell_log_to_phys(log->top);
  phys->right = swell_log_to_phys(log->right);
  phys->bottom = swell_log_to_phys(log->bottom);
}

extern const char *g_swell_deffont_face;

extern HFONT g_swell_default_font;
HFONT SWELL_GetDefaultFont();

// ---- swell_theme ----
// Semantic, non-user-configurable theme inspired by 2026 Adwaita and macOS
// Mojave. Light and dark variants are both defined; the active theme is
// selected once at startup. Metrics are stored in logical pixels and scaled
// up to physical pixels by swell_theme_rescale() when DPI changes.

struct swell_theme {
  // ---------- Colors (semantic) ----------
  // Surfaces
  int bg_window;          // dialog/window content
  int bg_surface;         // elevated surface (popups, group box)
  int bg_input;           // edit / list / tree / combo content
  int bg_input_alt;       // alternating row
  int bg_header;          // listview column header

  // Push button
  int bg_button;
  int bg_button_hover;
  int bg_button_pressed;

  // Accent (selection, default button, focus, progress)
  int accent;
  int accent_hover;
  int accent_pressed;
  int fg_on_accent;

  // Menu / menubar
  int bg_menu;
  int bg_menu_hover;
  int bg_menubar;
  int bg_menubar_hover;

  // Tabs
  int bg_tab;
  int bg_tab_active;

  // Scrollbar / trackbar / progress
  int bg_scrollbar;
  int scrollbar_thumb;
  int scrollbar_thumb_hover;
  int trackbar_track;
  int trackbar_fill;
  int trackbar_thumb;
  int progress_track;
  int progress_fill;

  // Text
  int fg_text;
  int fg_text_dim;
  int fg_text_disabled;

  // Borders + focus
  int border;
  int border_strong;
  int focus_ring;

  // Tooltip
  int info_bg;
  int info_text;

  // Caret
  int caret;

  // Drop shadow (solid approximation)
  int shadow;

  // ---------- Metrics (logical px @ 1.0x; rescaled into place) ----------
  int corner_radius;          // standard control corner (Win11 ~6 px)
  int corner_radius_large;    // popups / menus (~8 px)
  int border_width;
  int focus_ring_width;
  int focus_ring_offset;

  int padding_button_h, padding_button_v;
  int padding_edit_h, padding_edit_v;
  int padding_menu_item_h, padding_menu_item_v;
  int padding_listheader_h, padding_listheader_v;

  int button_min_h;
  int edit_min_h;
  int menubar_height;
  int menu_item_height;
  int menu_separator_height;
  int tab_height;
  int scrollbar_width;
  int scrollbar_min_thumb_height;
  int trackbar_track_h;
  int trackbar_thumb_r;
  int checkbox_size;
  int radio_size;

  int default_font_size;
  int small_font_size;
};

// Active theme. Populated by swell_theme_init() at startup.
extern swell_theme g_swell_theme;

// Theme mode. Dark mode plumbing is in place but not yet user-exposed.
enum swell_theme_mode { SWELL_THEME_LIGHT = 0, SWELL_THEME_DARK = 1 };
extern int g_swell_theme_mode;

void swell_theme_init(int mode);   // populate g_swell_theme (logical units)
void swell_theme_rescale();        // apply g_swell_ui_scale to metrics

// ---- Internal function declarations ----

// GDI API instrumentation. When env SWELL_GDI_PROFILE_LOG=/path is set at
// startup, every SGDI_PROF() scope accumulates call count + nanoseconds
// into a static per-function bucket. atexit dumps a CSV to the configured
// path. Overhead when disabled is one predictable branch + one chrono call
// skipped.
struct swell_gdi_prof_stat {
  const char *name;
  uint64_t calls;
  uint64_t nanos;
};
extern bool g_swell_gdi_prof_enabled;
swell_gdi_prof_stat *swell_gdi_prof_register(const char *name);
void swell_gdi_prof_dump_now(void);  // safe to call any time

struct swell_gdi_prof_scope {
  swell_gdi_prof_stat *st;
  std::chrono::steady_clock::time_point t0;
  swell_gdi_prof_scope(swell_gdi_prof_stat *s) : st(nullptr) {
    if (g_swell_gdi_prof_enabled) {
      st = s;
      t0 = std::chrono::steady_clock::now();
    }
  }
  ~swell_gdi_prof_scope() {
    if (st) {
      auto dur = std::chrono::steady_clock::now() - t0;
      __atomic_add_fetch(&st->calls, 1, __ATOMIC_RELAXED);
      __atomic_add_fetch(&st->nanos,
          (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count(),
          __ATOMIC_RELAXED);
    }
  }
};

#define SGDI_PROF(NAME) \
  static swell_gdi_prof_stat *_sgdi_stat_##NAME = swell_gdi_prof_register(#NAME); \
  swell_gdi_prof_scope _sgdi_prof_##NAME(_sgdi_stat_##NAME)

// swell-gdi.cpp internal
void swell_DirtyContext(HDC__ *ctx, int l, int t, int r, int b);
SkFont swell_make_skfont_from_hdc(HDC hdc);
void SWELL_DrawArc(HDC ctx, int l, int t, int r, int b,
                   float start_deg, float sweep_deg);
void SWELL_SetClipRoundRect(HDC ctx, int l, int t, int r, int b, int radius);
void SWELL_internalSkiaPaint(HWND hwnd, SkCanvas *canvas,
    int bmout_xpos, int bmout_ypos, bool forceref);

// Sanitize possibly-invalid UTF-8 (interpret stray bytes as CP1252) so Skia's
// kUTF8 decoder cannot trip on invalid sequences. Returns either buf (when
// already valid) or a pointer into tmp; out_len receives the resulting length.
const char *swell_text_for_skia(const char *buf, int len,
                                WDL_FastString &tmp, int *out_len);

// Safe wrappers around SkFont/SkCanvas text APIs that route through
// swell_text_for_skia first. All other callsites should use these instead of
// calling SkFont::measureText / SkCanvas::drawSimpleText directly with kUTF8.
static inline void swell_skfont_measure_utf8(const SkFont &font,
                                             const char *buf, int len,
                                             SkRect *bounds)
{
  if (bounds) *bounds = SkRect::MakeEmpty();
  if (len <= 0 || !buf) return;
  WDL_FastString tmp;
  int olen = len;
  const char *p = swell_text_for_skia(buf, len, tmp, &olen);
  if (olen > 0) font.measureText(p, olen, SkTextEncoding::kUTF8, bounds);
}

static inline void swell_skcanvas_drawtext_utf8(SkCanvas *canvas,
                                                const char *buf, int len,
                                                float x, float y,
                                                const SkFont &font,
                                                const SkPaint &paint)
{
  if (!canvas || len <= 0 || !buf) return;
  WDL_FastString tmp;
  int olen = len;
  const char *p = swell_text_for_skia(buf, len, tmp, &olen);
  if (olen > 0)
    canvas->drawSimpleText(p, olen, SkTextEncoding::kUTF8, x, y, font, paint);
}

// swell-backend-headless.cpp
void swell_oswindow_manage(HWND hwnd, bool wantFocus);
void swell_oswindow_destroy(HWND hwnd);
void swell_oswindow_resize(HWND hwnd, int reposflag, RECT *r);
void swell_oswindow_focus(HWND hwnd);
void swell_oswindow_update_owner(HWND hwnd);
void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle);
void swell_oswindow_update_enable(HWND hwnd);
void swell_oswindow_update_text(HWND hwnd);
void swell_oswindow_invalidate(HWND hwnd, const RECT *r);
void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r);
void swell_oswindow_maximize(HWND hwnd);
HWND  swell_oswindow_to_hwnd(SWELL_OSWINDOW osw);
SWELL_OSWINDOW swell_oswindow_from_hwnd(HWND hwnd);

// Lightweight frame profiler. Enabled for CSV logging by SWELL_PROFILE_LOG=path.
struct swell_perf_frame_stats {
  unsigned long long frame_index;
  double frame_ms;
  double paint_ms;
  double upload_ms;
  double present_ms;
  int invalidates;
  int paint_windows;
  int upload_x, upload_y, upload_w, upload_h;
  int surface_w, surface_h;
  int upload_pixels;
  bool full_upload;
};

bool swell_perf_is_active();
void swell_perf_frame_begin(HWND hwnd);
void swell_perf_note_paint_window(HWND hwnd);
void swell_perf_note_paint_time(double paint_ms);
void swell_perf_note_upload_rect(int x, int y, int w, int h,
                                 int surface_w, int surface_h,
                                 double upload_ms, double present_ms);
void swell_perf_frame_end(HWND hwnd, double frame_ms, bool painted);
const swell_perf_frame_stats *swell_perf_last_frame();

// swell-wnd.cpp internal
void SWELL_Internal_PostMessage_Init();
BOOL SWELL_Internal_PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SWELL_Internal_PMQ_ClearAllMessages(HWND hwnd);
void swell_destroyWindow_internal(HWND hwnd);
LRESULT SwellDialogDefaultWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SWELL_RunEvents();

// swell-controls.cpp internal
LRESULT buttonWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT editWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT labelWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT listViewWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT treeViewWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT comboWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT tabControlWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT trackbarWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT progressWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// timer list
extern TimerInfoRec *g_timer_list;
extern WDL_Mutex g_timer_mutex;

// pmq
extern PMQ_rec *g_pmq_head;
extern PMQ_rec *g_pmq_tail;
extern WDL_Mutex g_pmq_mutex;
extern int g_pmq_count;

// swell-dlg.cpp spare window
void swell_dlg_destroyspare();

// swell-menu.cpp: paint menu bar into NC area of top-level window
void swell_paint_menubar(HWND hwnd, HDC hdc);
// swell-menu.cpp: hit-test menu bar; returns item index or -1.
// item_screen_rect_out (if non-NULL) receives item rect in screen coords.
int swell_menubar_hittest(HWND hwnd, int win_x, RECT *item_screen_rect_out);
#ifdef SWELL_TARGET_SDL3
extern int g_swell_sdl_current_event_type;
bool swell_menu_sdl_handle_event(SDL_Event *evt);
#endif

// swell-wnd.cpp: invoke registered custom control creators
HWND swell_invoke_control_creators(HWND parent, const char *cname, int idx,
                                   const char *classname, int style,
                                   int x, int y, int w, int h);

// swell-misc.cpp: drag-drop callback function pointers (set via SWELL_ExtendedAPI)
extern void (*SWELL_DDrop_onDragLeave)(void);
extern void (*SWELL_DDrop_onDragOver)(HWND, int, int);
extern void (*SWELL_DDrop_onDragEnter)(void *, HWND, int, int);
extern const char *(*SWELL_DDrop_getDroppedFileTargetPath)(const char *);

// swell-misc.cpp: application name (set via SWELL_ExtendedAPI "APPNAME")
extern const char *g_swell_appname;

// swell-lua.cpp: Lua scripting engine (no-ops when SWELL2_HAS_LUA undefined)
void swell_lua_init();
void swell_lua_tick();
void swell_lua_notify_frame(HWND hwnd, double frame_ms, bool painted);
void swell_lua_shutdown();

bool SWELL_IsMenuTracking(); // true while TrackPopupMenu is active

// swell-inspector.cpp: interactive HWND tree inspector
extern HWND g_swell_inspector_highlight;
void swell_inspector_init();
void swell_inspector_tick();
void swell_inspector_notify_frame(HWND hwnd, double frame_ms);
void inspector_open();
void inspector_close();
void inspector_toggle();
bool swell_inspector_check_hotkey(WPARAM vk, bool ctrl, bool shift);

#endif // _SWELL_INTERNAL_H_
