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

#include <core/SkCanvas.h>
#include <core/SkSurface.h>
#include <core/SkColor.h>
#include <core/SkRect.h>
#include <core/SkPaint.h>
#include <core/SkBitmap.h>
#include <core/SkImage.h>

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
  int max_height;
  int max_width;
  int cache_linelen_w;
  int cache_linelen_strlen;
  WDL_TypedBuf<int> cache_linelen_bytes;
  bool m_disable_contextmenu;

  __SWELL_editControlState() : cursor_pos(0), sel1(-1), sel2(-1),
    cursor_state(0), cursor_timer(0), scroll_x(0), scroll_y(0),
    max_height(0), max_width(0), cache_linelen_w(0),
    cache_linelen_strlen(0), m_disable_contextmenu(false) {}
};

class SWELL_ListView_Row {
public:
  WDL_TypedBuf<SWELL_ListView_Rec> m_cols;
  LPARAM m_param;
  int m_tmp;
  SWELL_ListView_Row() : m_param(0), m_tmp(0) {}
};

struct SWELL_ListView_Rec {
  char *txt;
  int image_idx;
  SWELL_ListView_Rec() : txt(NULL), image_idx(-1) {}
  ~SWELL_ListView_Rec() { free(txt); }
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
  int m_last_row_height;
  ListViewCapMode m_capmode_state;
  int m_capmode_data1, m_capmode_data2;
  HIMAGELIST m_status_imagelist;
  int m_status_imagelist_type;
  int m_extended_style;
  int m_fastclick_mask;
  int m_color_bg, m_color_bg_sel, m_color_bg_sel_inactive;
  int m_color_text, m_color_text_sel, m_color_text_sel_inactive;
  int m_color_grid;
  int m_color_extras[4];

  listViewState() : m_owner_data_size(-1), m_selitem(-1),
    m_is_multisel(false), m_is_listbox(false), m_scroll_x(0), m_scroll_y(0),
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
  int m_capmode;
  int m_color_bg, m_color_text;

  treeViewState() : m_sel(NULL), m_last_row_height(16),
    m_scroll_x(0), m_scroll_y(0), m_capmode(0),
    m_color_bg(0xFFFFFFFF), m_color_text(0xFF000000) {
    m_root = new HTREEITEM__();
    m_root->m_state = TVIS_EXPANDED;
  }
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
  WDL_PtrList_DeleteOnDestroy<__SWELL_ComboBoxInternalState_rec> items;
  __SWELL_editControlState editstate;
  __SWELL_ComboBoxInternalState() : selidx(-1) {}
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

extern int g_swell_ui_scale;              // DPI scaling: 256 = 1.0x
#define SWELL_UI_SCALE(x) (((x)*g_swell_ui_scale)/256)

extern const char *g_swell_deffont_face;

extern HFONT g_swell_default_font;
HFONT SWELL_GetDefaultFont();

// ---- swell_colortheme ----

struct swell_colortheme {
  int _3dface, _3dshadow, _3dhilight, _3ddkshadow;
  int button_bg, button_text, button_text_disabled, button_shadow, button_hilight;
  int checkbox_bg, checkbox_text, checkbox_text_disabled;
  int scrollbar, scrollbar_fg, scrollbar_bg;
  int edit_bg, edit_text, edit_text_sel, edit_bg_sel, edit_cursor;
  int info_bg, info_text;
  int menu_bg, menu_text, menu_hilight_bg, menu_hilight_text;
  int menubar_bg, menubar_text, menubar_hilight_bg, menubar_hilight_text;
  int menubar_height;
  int trackbar_bg, trackbar_fg, trackbar_thumb;
  int progress;
  int label_text;
  int combo_bg, combo_text;
  int listview_bg, listview_text, listview_header_bg, listview_header_text;
  int treeview_bg, treeview_text;
  int tab_bg, tab_text, tab_sel_bg, tab_sel_text;
  int focusrect;
  int group_bg, group_text;
  int focus_hilight;
  int smscrollbar_width;
  int default_font_size;

  swell_colortheme();
};

extern swell_colortheme g_swell_ctheme;

// ---- Internal function declarations ----

// swell-gdi.cpp internal
void swell_DirtyContext(HDC__ *ctx, int l, int t, int r, int b);
void SWELL_internalSkiaPaint(HWND hwnd, SkCanvas *canvas,
    int bmout_xpos, int bmout_ypos, bool forceref);

// swell-backend-headless.cpp
void swell_oswindow_manage(HWND hwnd, bool wantFocus);
void swell_oswindow_destroy(HWND hwnd);
void swell_oswindow_resize(HWND hwnd, int reposflag, RECT *r);
void swell_oswindow_focus(HWND hwnd);
void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle);
void swell_oswindow_update_enable(HWND hwnd);
void swell_oswindow_update_text(HWND hwnd);
void swell_oswindow_invalidate(HWND hwnd, const RECT *r);
void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r);
HWND  swell_oswindow_to_hwnd(SWELL_OSWINDOW osw);
SWELL_OSWINDOW swell_oswindow_from_hwnd(HWND hwnd);

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

// swell-wnd.cpp: invoke registered custom control creators
HWND swell_invoke_control_creators(HWND parent, const char *cname, int idx,
                                   const char *classname, int style,
                                   int x, int y, int w, int h);

#endif // _SWELL_INTERNAL_H_
