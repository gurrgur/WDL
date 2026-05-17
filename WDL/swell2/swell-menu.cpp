/*
  SWELL2 menu module — HMENU lifecycle, item manipulation, TrackPopupMenu,
  menu bar drawing, resource loading.

  Modern clean-room implementation: flat design, crisp Skia rendering,
  keyboard navigation, cascading submenus.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
#include "swell-menugen.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

// swell-functions.h defines Polygon(a,b,c) which clashes with SkPath::Polygon
#undef Polygon

// MIIM constants not defined in swell-types.h
#ifndef MIIM_STRING
#define MIIM_STRING     0x0040
#define MIIM_FTYPE      0x0100
#define MIIM_CHECKMARKS 0x0008
#endif

#ifdef SWELL_TARGET_SDL3
#include <SDL3/SDL.h>
#include <core/SkCanvas.h>
#include <core/SkPaint.h>
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkFontMgr.h>
#include <core/SkTypeface.h>
#include <core/SkRRect.h>
#include <ports/SkFontMgr_fontconfig.h>
#include <ports/SkFontScanner_FreeType.h>
#endif

// ============================================================================
// Default / current menu globals
// ============================================================================

static HMENU g_default_window_menu = NULL;
static HMENU g_default_modal_window_menu = NULL;
static HMENU g_current_menu = NULL;

HMENU SWELL_GetDefaultWindowMenu()       { return g_default_window_menu; }
void  SWELL_SetDefaultWindowMenu(HMENU m) { g_default_window_menu = m; }
HMENU SWELL_GetDefaultModalWindowMenu()  { return g_default_modal_window_menu; }
void  SWELL_SetDefaultModalWindowMenu(HMENU m) { g_default_modal_window_menu = m; }
HMENU SWELL_GetCurrentMenu()             { return g_current_menu; }
void  SWELL_SetCurrentMenu(HMENU m)      { g_current_menu = m; }

// macOS-only; on Linux just stored for compatibility
void SWELL_SetMenuDestination(HMENU menu, HWND hwnd) { (void)menu; (void)hwnd; }

// ============================================================================
// HMENU lifecycle
// ============================================================================

HMENU CreatePopupMenu()
{
  return new HMENU__();
}

HMENU CreatePopupMenuEx(const char *title)
{
  (void)title;
  return new HMENU__();
}

void DestroyMenu(HMENU hMenu)
{
  if (!hMenu) return;
  // WDL_PtrList_DeleteOnDestroy frees all SWELL_MenuItems.
  // Each item's m_submenu is owned by the item if MF_POPUP is set.
  // We destroy submenus here.
  int n = hMenu->m_items.GetSize();
  for (int i = 0; i < n; i++) {
    SWELL_MenuItem *it = hMenu->m_items.Get(i);
    if (it && (it->m_flags & MF_POPUP) && it->m_submenu) {
      DestroyMenu(it->m_submenu);
      it->m_submenu = NULL;
    }
  }
  delete hMenu;
}

HMENU SWELL_DuplicateMenu(HMENU src)
{
  if (!src) return NULL;
  HMENU dst = new HMENU__();
  int n = src->m_items.GetSize();
  for (int i = 0; i < n; i++) {
    SWELL_MenuItem *si = src->m_items.Get(i);
    if (!si) continue;
    SWELL_MenuItem *di = new SWELL_MenuItem();
    di->m_name    = si->m_name;
    di->m_id      = si->m_id;
    di->m_flags   = si->m_flags;
    di->m_userdata = si->m_userdata;
    di->m_mod_flag = si->m_mod_flag;
    di->m_mod_code = si->m_mod_code;
    di->m_mod_mask = si->m_mod_mask;
    if ((si->m_flags & MF_POPUP) && si->m_submenu)
      di->m_submenu = SWELL_DuplicateMenu(si->m_submenu);
    dst->m_items.Add(di);
  }
  return dst;
}

// ============================================================================
// Item lookup helpers
// ============================================================================

static SWELL_MenuItem *menu_find_by_pos(HMENU hMenu, int pos)
{
  if (!hMenu || pos < 0 || pos >= hMenu->m_items.GetSize()) return NULL;
  return hMenu->m_items.Get(pos);
}

// Find item by command ID (recursive through submenus)
static SWELL_MenuItem *menu_find_by_id(HMENU hMenu, int id)
{
  if (!hMenu) return NULL;
  int n = hMenu->m_items.GetSize();
  for (int i = 0; i < n; i++) {
    SWELL_MenuItem *it = hMenu->m_items.Get(i);
    if (!it) continue;
    if ((it->m_flags & MF_SEPARATOR) == 0 && it->m_id == id)
      return it;
    if ((it->m_flags & MF_POPUP) && it->m_submenu) {
      SWELL_MenuItem *r = menu_find_by_id(it->m_submenu, id);
      if (r) return r;
    }
  }
  return NULL;
}

// Resolve byPos/byCommand to item pointer
static SWELL_MenuItem *menu_resolve(HMENU hMenu, int idx, BOOL byPos)
{
  if (!hMenu) return NULL;
  if (byPos) return menu_find_by_pos(hMenu, idx);
  return menu_find_by_id(hMenu, idx);
}

// ============================================================================
// Item manipulation
// ============================================================================

int AddMenuItem(HMENU hMenu, int pos, const char *name, int tagid)
{
  if (!hMenu) return -1;
  SWELL_MenuItem *it = new SWELL_MenuItem();
  if (name) it->m_name.Set(name);
  it->m_id = tagid;
  int sz = hMenu->m_items.GetSize();
  if (pos < 0 || pos >= sz)
    hMenu->m_items.Add(it);
  else
    hMenu->m_items.Insert(pos, it);
  return pos < 0 || pos >= sz ? hMenu->m_items.GetSize() - 1 : pos;
}

void SWELL_InsertMenu(HMENU menu, int pos, unsigned int flag, UINT_PTR idx, const char *str)
{
  if (!menu) return;
  SWELL_MenuItem *it = new SWELL_MenuItem();
  it->m_flags = flag & ~MF_BYPOSITION;
  if (flag & MF_POPUP)
    it->m_submenu = (HMENU)idx;
  else
    it->m_id    = (int)idx;
  if (str && !(flag & MF_SEPARATOR)) it->m_name.Set(str);

  bool byPos = (flag & MF_BYPOSITION) != 0;
  int insertAt = (int)pos;
  if (!byPos) insertAt = menu->m_items.GetSize(); // byCommand: append
  if (insertAt < 0 || insertAt > menu->m_items.GetSize())
    insertAt = menu->m_items.GetSize();

  menu->m_items.Insert(insertAt, it);
}

void InsertMenuItem(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  if (!hMenu || !mi) return;
  SWELL_MenuItem *it = new SWELL_MenuItem();

  if (mi->fMask & MIIM_STRING) {
    if (mi->dwTypeData) it->m_name.Set(mi->dwTypeData);
  } else if (mi->fMask & MIIM_TYPE) {
    if (mi->fType & MFT_SEPARATOR) {
      it->m_flags |= MF_SEPARATOR;
    } else if (mi->dwTypeData) {
      it->m_name.Set(mi->dwTypeData);
    }
  }
  if (mi->fMask & MIIM_ID)       it->m_id = (int)mi->wID;
  if (mi->fMask & MIIM_FTYPE) {
    it->m_flags = (it->m_flags & ~0x8FF) | (mi->fType & 0x8FF);
    if (mi->fType & MFT_SEPARATOR) it->m_name.Set(""); // separators carry no text
  }
  if (mi->fMask & MIIM_STATE)    it->m_flags = (it->m_flags & ~0x0F) | (mi->fState & 0x0F);
  if (mi->fMask & MIIM_SUBMENU) {
    it->m_submenu = mi->hSubMenu;
    if (it->m_submenu) it->m_flags |= MF_POPUP;
  }
  if (mi->fMask & MIIM_DATA)    it->m_userdata = (DWORD_PTR)mi->dwItemData;
  if (mi->fMask & MIIM_CHECKMARKS) {
    it->m_checked_icon   = (HICON)mi->hbmpChecked;
    it->m_unchecked_icon = (HICON)mi->hbmpUnchecked;
  }

  int insertAt = pos;
  if (!byPos) insertAt = hMenu->m_items.GetSize();
  if (insertAt < 0 || insertAt > hMenu->m_items.GetSize())
    insertAt = hMenu->m_items.GetSize();
  hMenu->m_items.Insert(insertAt, it);
}

BOOL GetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  if (!mi) return FALSE;
  SWELL_MenuItem *it = menu_resolve(hMenu, pos, byPos);
  if (!it) return FALSE;

  if (mi->fMask & MIIM_TYPE) {
    mi->fType = it->m_flags & (MFT_STRING|MFT_SEPARATOR|MFT_BITMAP);
    if (it->m_flags & MF_SEPARATOR) mi->fType |= MFT_SEPARATOR;
    if (mi->dwTypeData && mi->cch > 0) {
      lstrcpyn(mi->dwTypeData, it->m_name.Get() ? it->m_name.Get() : "", (int)mi->cch);
    }
  }
  if (mi->fMask & MIIM_STRING) {
    if (mi->dwTypeData && mi->cch > 0)
      lstrcpyn(mi->dwTypeData, it->m_name.Get() ? it->m_name.Get() : "", (int)mi->cch);
  }
  if (mi->fMask & MIIM_ID)       mi->wID = (UINT)it->m_id;
  if (mi->fMask & MIIM_FTYPE)    mi->fType = it->m_flags & 0x8FF;
  if (mi->fMask & MIIM_STATE)    mi->fState = it->m_flags & 0x0F;
  if (mi->fMask & MIIM_SUBMENU)  mi->hSubMenu = it->m_submenu;
  if (mi->fMask & MIIM_DATA)     mi->dwItemData = (ULONG_PTR)it->m_userdata;
  if (mi->fMask & MIIM_CHECKMARKS) {
    mi->hbmpChecked   = (HBITMAP)it->m_checked_icon;
    mi->hbmpUnchecked = (HBITMAP)it->m_unchecked_icon;
  }
  return TRUE;
}

BOOL SetMenuItemInfo(HMENU hMenu, int pos, BOOL byPos, MENUITEMINFO *mi)
{
  if (!mi) return FALSE;
  SWELL_MenuItem *it = menu_resolve(hMenu, pos, byPos);
  if (!it) return FALSE;

  if (mi->fMask & MIIM_TYPE) {
    if (mi->fType & MFT_SEPARATOR) {
      it->m_flags |= MF_SEPARATOR;
    } else {
      it->m_flags &= ~MF_SEPARATOR;
      if (mi->dwTypeData) it->m_name.Set(mi->dwTypeData);
    }
  }
  if (mi->fMask & MIIM_STRING) {
    if (mi->dwTypeData) it->m_name.Set(mi->dwTypeData);
  }
  if (mi->fMask & MIIM_ID)      it->m_id = (int)mi->wID;
  if (mi->fMask & MIIM_FTYPE)   it->m_flags = (it->m_flags & ~0x8FF) | (mi->fType & 0x8FF);
  if (mi->fMask & MIIM_STATE)   it->m_flags = (it->m_flags & ~0x0F) | (mi->fState & 0x0F);
  if (mi->fMask & MIIM_SUBMENU) {
    it->m_submenu = mi->hSubMenu;
    if (it->m_submenu) it->m_flags |= MF_POPUP;
    else               it->m_flags &= ~MF_POPUP;
  }
  if (mi->fMask & MIIM_DATA)    it->m_userdata = (DWORD_PTR)mi->dwItemData;
  if (mi->fMask & MIIM_CHECKMARKS) {
    it->m_checked_icon   = (HICON)mi->hbmpChecked;
    it->m_unchecked_icon = (HICON)mi->hbmpUnchecked;
  }
  return TRUE;
}

bool SetMenuItemModifier(HMENU hMenu, int idx, int flag, int code, unsigned int mask)
{
  SWELL_MenuItem *it = menu_resolve(hMenu, idx, (flag & MF_BYPOSITION) ? TRUE : FALSE);
  if (!it) return false;
  it->m_mod_flag = flag;
  it->m_mod_code = code;
  it->m_mod_mask = mask;
  return true;
}

bool SetMenuItemText(HMENU hMenu, int idx, int flag, const char *text)
{
  SWELL_MenuItem *it = menu_resolve(hMenu, idx, (flag & MF_BYPOSITION) ? TRUE : FALSE);
  if (!it) return false;
  if (text) it->m_name.Set(text);
  return true;
}

bool EnableMenuItem(HMENU hMenu, int idx, int en)
{
  SWELL_MenuItem *it = menu_resolve(hMenu, idx, (en & MF_BYPOSITION) ? TRUE : FALSE);
  if (!it) return false;
  it->m_flags &= ~(MF_GRAYED|MF_DISABLED);
  it->m_flags |= (en & (MF_GRAYED|MF_DISABLED));
  return true;
}

bool DeleteMenu(HMENU hMenu, int idx, int flag)
{
  if (!hMenu) return false;
  bool byPos = (flag & MF_BYPOSITION) != 0;
  SWELL_MenuItem *it = menu_resolve(hMenu, idx, byPos ? TRUE : FALSE);
  if (!it) return false;

  int n = hMenu->m_items.GetSize();
  for (int i = 0; i < n; i++) {
    if (hMenu->m_items.Get(i) == it) {
      if ((it->m_flags & MF_POPUP) && it->m_submenu) {
        DestroyMenu(it->m_submenu);
        it->m_submenu = NULL;
      }
      hMenu->m_items.Delete(i, true);
      return true;
    }
  }
  return false;
}

bool CheckMenuItem(HMENU hMenu, int idx, int chk)
{
  SWELL_MenuItem *it = menu_resolve(hMenu, idx, (chk & MF_BYPOSITION) ? TRUE : FALSE);
  if (!it) return false;
  it->m_flags &= ~MF_CHECKED;
  it->m_flags |= (chk & MF_CHECKED);
  return true;
}

// ============================================================================
// Query
// ============================================================================

HMENU GetSubMenu(HMENU hMenu, int pos)
{
  SWELL_MenuItem *it = menu_find_by_pos(hMenu, pos);
  if (!it) return NULL;
  return it->m_submenu;
}

int GetMenuItemCount(HMENU hMenu)
{
  return hMenu ? hMenu->m_items.GetSize() : 0;
}

int GetMenuItemID(HMENU hMenu, int pos)
{
  SWELL_MenuItem *it = menu_find_by_pos(hMenu, pos);
  if (!it) return -1;
  if (it->m_flags & MF_POPUP) return -1;
  return it->m_id;
}

// ============================================================================
// Window menu bar
// ============================================================================

BOOL SetMenu(HWND hwnd, HMENU menu)
{
  if (!hwnd) return FALSE;
  HMENU oldmenu = hwnd->m_menu;
  hwnd->m_menu = menu;

  // Preserve client area across menu add/remove on top-level windows by
  // resizing the window by menubar_height. Without this, NCCALCSIZE shrinks
  // the client area under existing controls and the menubar paints on top
  // of them. Swap m_wndproc to DefWindowProc during the resize so the host
  // does not see a phantom WM_SIZE for the synthesized growth.
  if (!hwnd->m_parent && !!hwnd->m_menu != !!oldmenu)
  {
    WNDPROC oldwc = hwnd->m_wndproc;
    hwnd->m_wndproc = DefWindowProc;
    RECT r;
    GetWindowRect(hwnd, &r);
    if (oldmenu) r.bottom -= g_swell_theme.menubar_height;
    else         r.bottom += g_swell_theme.menubar_height;
    SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    hwnd->m_wndproc = oldwc;
  }

  InvalidateRect(hwnd, NULL, FALSE);
  return TRUE;
}

HMENU GetMenu(HWND hwnd)
{
  return hwnd ? hwnd->m_menu : NULL;
}

void DrawMenuBar(HWND hwnd)
{
  if (!hwnd || !hwnd->m_menu) return;
  InvalidateRect(hwnd, NULL, FALSE);
}

// ============================================================================
// Resource loading
// ============================================================================

HMENU SWELL_LoadMenu(struct SWELL_MenuResourceIndex *head, const char *resid)
{
  if (!head) return NULL;
  for (SWELL_MenuResourceIndex *r = head; r; r = r->_next) {
    bool match;
    if ((size_t)r->resid <= 0xFFFF && (size_t)resid <= 0xFFFF)
      match = (r->resid == resid);
    else if ((size_t)r->resid > 0xFFFF && (size_t)resid > 0xFFFF)
      match = (strcmp(r->resid, resid) == 0);
    else
      match = false;
    if (match) {
      HMENU m = CreatePopupMenu();
      if (r->createFunc) r->createFunc(m);
      return m;
    }
  }
  return NULL;
}

// ============================================================================
// Internal menu generation
// ============================================================================

void SWELL_Menu_AddMenuItem(HMENU hMenu, const char *name, int idx, unsigned int flags)
{
  if (!hMenu) return;
  SWELL_MenuItem *it = new SWELL_MenuItem();
  if (name) it->m_name.Set(name);
  it->m_id = idx;
  it->m_flags = flags & ~MF_BYPOSITION;
  hMenu->m_items.Add(it);
}

int SWELL_GenerateMenuFromList(HMENU hMenu, const void *list, int listsz)
{
  if (!hMenu || !list || listsz <= 0) return 0;
  const SWELL_MenuGen_Entry *e = (const SWELL_MenuGen_Entry *)list;
  int idx = 0;
  while (idx < listsz) {
    const SWELL_MenuGen_Entry *cur = &e[idx++];
    if (!cur->name) {
      // separator (name==NULL, idx==0xffff)
      SWELL_MenuItem *it = new SWELL_MenuItem();
      it->m_flags = MF_SEPARATOR;
      hMenu->m_items.Add(it);
    } else if (strncmp(cur->name, SWELL_MENUGEN_POPUP_PREFIX,
                       strlen(SWELL_MENUGEN_POPUP_PREFIX)) == 0) {
      // submenu
      HMENU sub = CreatePopupMenu();
      int consumed = SWELL_GenerateMenuFromList(sub, &e[idx], listsz - idx);
      idx += consumed;
      SWELL_MenuItem *it = new SWELL_MenuItem();
      it->m_name.Set(cur->name + strlen(SWELL_MENUGEN_POPUP_PREFIX));
      it->m_flags = MF_POPUP | (cur->flags & ~MF_BYPOSITION);
      it->m_id = (int)cur->idx;
      it->m_submenu = sub;
      hMenu->m_items.Add(it);
    } else if (strcmp(cur->name, SWELL_MENUGEN_ENDPOPUP) == 0) {
      // end of submenu
      break;
    } else {
      SWELL_MenuItem *it = new SWELL_MenuItem();
      it->m_name.Set(cur->name);
      it->m_id = (int)cur->idx;
      it->m_flags = cur->flags & ~MF_BYPOSITION;
      hMenu->m_items.Add(it);
    }
  }
  return idx;
}

// ============================================================================
// TrackPopupMenu
// ============================================================================

#ifdef SWELL_TARGET_SDL3

// ---------------------------------------------------------------------------
// Font helpers for menu rendering
// ---------------------------------------------------------------------------

static sk_sp<SkTypeface> menu_get_typeface()
{
  static sk_sp<SkTypeface> s_tf;
  static bool s_tried = false;
  if (!s_tried) {
    s_tried = true;
    sk_sp<SkFontMgr> fm = SkFontMgr_New_FontConfig(
        nullptr, SkFontScanner_Make_FreeType());
    if (fm) {
      SkFontStyle style(SkFontStyle::kNormal_Weight,
                        SkFontStyle::kNormal_Width,
                        SkFontStyle::kUpright_Slant);
      // Try modern system fonts in order
      const char *faces[] = { "Arial", "Noto Sans", "Segoe UI",
                               "DejaVu Sans", "Liberation Sans",
                               "FreeSans", "Arial", nullptr };
      for (int i = 0; faces[i]; i++) {
        s_tf = fm->matchFamilyStyle(faces[i], style);
        if (s_tf) break;
      }
    }
  }
  return s_tf;
}

// ---------------------------------------------------------------------------
// Menu layout constants (base, scaled at runtime by DPI)
// ---------------------------------------------------------------------------

static inline int menu_scaled_px(int px)
{
  int v = SWELL_UI_SCALE(px);
  return v > 0 ? v : 1;
}

// Metrics are now sourced from the global theme (already in physical pixels).
static inline int menu_item_h()  { return g_swell_theme.menu_item_height; }
static inline int menu_sep_h()   { return g_swell_theme.menu_separator_height; }
static inline int menu_lpad()    { return g_swell_theme.padding_menu_item_h * 2 + g_swell_theme.checkbox_size; }
static inline int menu_rpad()    { return g_swell_theme.padding_menu_item_h + g_swell_theme.checkbox_size; }
static inline int menu_vpad()    { return menu_scaled_px(4); }
static inline int menu_min_w()   { return SWELL_UI_SCALE(160); }
static inline int menu_font_sz() { return g_swell_theme.default_font_size; }
static inline int menu_corner_r(){ return g_swell_theme.corner_radius_large; }
static inline int menu_outer_pad(){ return menu_scaled_px(4); }

// Convert native SWELL RGB color to SkColor (premul alpha)
static SkColor swell_to_sk(int c, uint8_t a = 255)
{
  return SkColorSetARGB(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

// ---------------------------------------------------------------------------
// Single menu window state
// ---------------------------------------------------------------------------

struct MenuWindow {
  HMENU          menu;
  HWND           owner_hwnd;
  int            flags;
  int            sx, sy;     // screen position, physical pixels
  SDL_Window    *sdlwin;
  SDL_Renderer  *renderer;
  SDL_Texture   *texture;
  sk_sp<SkSurface> surface;
  SkFont         font;
  int            w, h;
  int            content_h;
  int            scroll_y;
  int            l_sx, l_sy; // SDL logical screen position
  int            hovered;   // -1 = none
  int            scroll_hover_dir;
  int            n_items;
  int           *item_y;    // top Y of each item (size n_items)
  bool           done;
  bool           sync_waiting;
  bool           ignore_initial_button_up;
  int            result;    // selected item ID, 0 = cancelled
  SDL_WindowID   window_id;
  SDL_TimerID    scroll_timer;
  MenuWindow    *parent;
  MenuWindow    *child;

  MenuWindow() : menu(NULL), owner_hwnd(NULL), flags(0), sx(0), sy(0),
    sdlwin(NULL), renderer(NULL), texture(NULL),
    w(0), h(0), content_h(0), scroll_y(0), l_sx(0), l_sy(0),
    hovered(-1), scroll_hover_dir(0), n_items(0), item_y(NULL),
    done(false), sync_waiting(false), ignore_initial_button_up(false),
    result(0), window_id(0), scroll_timer(0),
    parent(NULL), child(NULL) {}
  ~MenuWindow() {
    if (scroll_timer) SDL_RemoveTimer(scroll_timer);
    if (child) delete child;
    if (!parent && owner_hwnd)
      RemoveProp(owner_hwnd, "SWELL_MenuOwner");
    free(item_y);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (sdlwin) SDL_DestroyWindow(sdlwin);
    if (owner_hwnd) owner_hwnd->Release();
  }
};

static MenuWindow *g_active_menu = NULL;

static Uint32 menu_scroll_timer_event_type()
{
  static Uint32 s_type = 0;
  if (!s_type) s_type = SDL_RegisterEvents(1);
  return s_type;
}

static Uint32 SDLCALL menu_scroll_timer_cb(void *, SDL_TimerID, Uint32 interval)
{
  Uint32 type = menu_scroll_timer_event_type();
  if (type != (Uint32)-1) {
    SDL_Event evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = type;
    SDL_PushEvent(&evt);
  }
  return interval;
}

// ---------------------------------------------------------------------------
// Strip Win32 & accelerator prefix for display, stopping at \t shortcut sep.
// "&File\tCtrl+O" -> "File", "Save && Exit" -> "Save & Exit"
// Returns label text before \t (or full string if no \t). Any text after \t
// is the shortcut — use menu_get_shortcut() to retrieve it.
// Not thread-safe; only call from main thread (as all menu code is).
// ---------------------------------------------------------------------------
static const char *menu_strip_accel(const char *src, WDL_FastString &buf)
{
  buf.Set("");
  if (!src) return "";
  while (*src) {
    if (*src == '\t') break;
    if (*src == '&') {
      src++;
      if (*src == '&') { buf.Append("&", 1); src++; }
      // else: bare & — skip it, next char is accelerator (keep in output)
    } else {
      buf.Append(src, 1);
      src++;
    }
  }
  return buf.Get();
}

// Return the shortcut text after \t in a menu label, or nullptr if none.
// "&File\tCtrl+O" -> "Ctrl+O"
static const char *menu_get_shortcut(const char *src)
{
  if (!src) return nullptr;
  const char *p = strstr(src, "\t");
  return p ? p + 1 : nullptr;
}

// ---------------------------------------------------------------------------
// Measure and position menu items
// ---------------------------------------------------------------------------

static void menu_measure(MenuWindow *mw)
{
  if (!mw->menu) return;
  int n = mw->menu->m_items.GetSize();
  mw->n_items = n;
  free(mw->item_y);
  mw->item_y = (int *)malloc(sizeof(int) * (n + 1));

  // Compute max label and shortcut widths
  int maxLabelW = 0, maxShortcutW = 0;
  WDL_FastString stripBuf;
  for (int i = 0; i < n; i++) {
    SWELL_MenuItem *it = mw->menu->m_items.Get(i);
    if (!it || (it->m_flags & MF_SEPARATOR)) continue;
    const char *raw = it->m_name.Get();
    if (!raw || !raw[0]) continue;

    const char *label = menu_strip_accel(raw, stripBuf);
    if (label && label[0]) {
      SkRect bounds;
      swell_skfont_measure_utf8(mw->font, label, (int)strlen(label), &bounds);
      int tw = (int)(bounds.width() + 0.5f);
      if (tw > maxLabelW) maxLabelW = tw;
    }

    const char *shortcut = menu_get_shortcut(raw);
    if (shortcut && shortcut[0]) {
      SkRect bounds;
      swell_skfont_measure_utf8(mw->font, shortcut, (int)strlen(shortcut), &bounds);
      int tw = (int)(bounds.width() + 0.5f);
      if (tw > maxShortcutW) maxShortcutW = tw;
    }
  }

  if (maxShortcutW > 0) {
    const int gap = g_swell_theme.padding_menu_item_h;
    mw->w = menu_lpad() + maxLabelW + gap + maxShortcutW + gap;
  } else {
    mw->w = menu_lpad() + maxLabelW + menu_rpad();
  }
  if (mw->w < menu_min_w()) mw->w = menu_min_w();

  int y = menu_vpad();
  for (int i = 0; i < n; i++) {
    mw->item_y[i] = y;
    SWELL_MenuItem *it = mw->menu->m_items.Get(i);
    if (it && (it->m_flags & MF_SEPARATOR))
      y += menu_sep_h();
    else
      y += menu_item_h();
  }
  mw->item_y[n] = y;
  mw->content_h = y + menu_vpad();
  mw->h = mw->content_h;
}

static int menu_scroll_indicator_h()
{
  int h = menu_scaled_px(22);
  if (h > menu_item_h()) h = menu_item_h();
  return h;
}

static int menu_max_scroll(MenuWindow *mw)
{
  if (!mw || mw->content_h <= mw->h) return 0;
  return mw->content_h - mw->h;
}

static void menu_clamp_scroll(MenuWindow *mw)
{
  if (!mw) return;
  const int max_scroll = menu_max_scroll(mw);
  if (mw->scroll_y < 0) mw->scroll_y = 0;
  if (mw->scroll_y > max_scroll) mw->scroll_y = max_scroll;
}

static int menu_scroll_zone_at(MenuWindow *mw, int local_y)
{
  if (!mw || menu_max_scroll(mw) <= 0) return 0;
  const int ind_h = menu_scroll_indicator_h();
  if (mw->scroll_y > 0 && local_y >= 0 && local_y < ind_h)
    return -1;
  if (mw->scroll_y < menu_max_scroll(mw) &&
      local_y >= mw->h - ind_h && local_y < mw->h)
    return 1;
  return 0;
}

// ---------------------------------------------------------------------------
// Hit-test: which item is at screen-relative y?
// ---------------------------------------------------------------------------

static int menu_hittest(MenuWindow *mw, int local_y)
{
  for (int i = 0; i < mw->n_items; i++) {
    SWELL_MenuItem *it = mw->menu->m_items.Get(i);
    if (!it || (it->m_flags & MF_SEPARATOR)) continue;
    if (local_y >= mw->item_y[i] && local_y < mw->item_y[i + 1])
      return i;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Draw the menu onto the Skia surface
// ---------------------------------------------------------------------------

static void menu_draw(MenuWindow *mw)
{
  if (!mw->surface) return;
  SkCanvas *c = mw->surface->getCanvas();
  if (!c) return;

  const swell_theme &th = g_swell_theme;
  const float radius = (float)menu_corner_r();

  // Clear to transparent (rounded card sits on transparent SDL window bg)
  SkPaint clearp;
  clearp.setBlendMode(SkBlendMode::kSrc);
  clearp.setColor(SK_ColorTRANSPARENT);
  c->drawPaint(clearp);

  // Surface fill — rounded card
  SkRect full = SkRect::MakeWH((float)mw->w, (float)mw->h);
  SkRRect outer; outer.setRectXY(full, radius, radius);

  SkPaint bgp;
  bgp.setAntiAlias(true);
  bgp.setColor(swell_to_sk(th.bg_menu));
  c->drawRRect(outer, bgp);

  // 1-px subtle border
  SkPaint border;
  border.setStyle(SkPaint::kStroke_Style);
  border.setColor(swell_to_sk(th.border));
  border.setStrokeWidth((float)th.border_width);
  border.setAntiAlias(true);
  SkRect innerR = SkRect::MakeLTRB(0.5f, 0.5f,
                                   (float)mw->w - 0.5f, (float)mw->h - 0.5f);
  SkRRect innerRR; innerRR.setRectXY(innerR, radius, radius);
  c->drawRRect(innerRR, border);

  // Clip subsequent drawing to the rounded card
  c->save();
  c->clipRRect(outer, true);

  const int item_pad = menu_outer_pad();

  c->save();
  c->translate(0.0f, (float)-mw->scroll_y);

  for (int i = 0; i < mw->n_items; i++) {
    SWELL_MenuItem *it = mw->menu->m_items.Get(i);
    if (!it) continue;

    int iy = mw->item_y[i];
    int ih = mw->item_y[i + 1] - iy;

    if (it->m_flags & MF_SEPARATOR) {
      SkPaint sep;
      sep.setColor(swell_to_sk(th.border, 160));
      sep.setStrokeWidth((float)th.border_width);
      sep.setAntiAlias(false);
      float sy = iy + menu_sep_h() / 2.0f + 0.5f;
      c->drawLine((float)item_pad, sy,
                  (float)(mw->w - item_pad), sy, sep);
      continue;
    }

    bool hovered  = (i == mw->hovered);
    bool disabled = (it->m_flags & (MF_GRAYED|MF_DISABLED)) != 0;
    bool checked  = (it->m_flags & MF_CHECKED) != 0;
    bool is_popup = (it->m_flags & MF_POPUP) != 0;

    // Hover highlight: rounded accent bar inset by item_pad
    if (hovered && !disabled) {
      SkPaint hp;
      hp.setAntiAlias(true);
      hp.setColor(swell_to_sk(th.bg_menu_hover));
      SkRect hr = SkRect::MakeLTRB((float)item_pad, (float)iy + 1,
                                   (float)(mw->w - item_pad),
                                   (float)(iy + ih - 1));
      SkRRect rr;
      rr.setRectXY(hr, (float)th.corner_radius_large / 2.0f,
                       (float)th.corner_radius_large / 2.0f);
      c->drawRRect(rr, hp);
    }

    SkColor textCol;
    if (disabled)      textCol = swell_to_sk(th.fg_text_disabled);
    else if (hovered)  textCol = swell_to_sk(th.fg_on_accent);
    else               textCol = swell_to_sk(th.fg_text);

    // Checkmark
    if (checked) {
      SkPaint cp;
      cp.setAntiAlias(true);
      cp.setColor(textCol);
      cp.setStrokeWidth((float)menu_scaled_px(2));
      cp.setStyle(SkPaint::kStroke_Style);
      cp.setStrokeCap(SkPaint::kRound_Cap);
      cp.setStrokeJoin(SkPaint::kRound_Join);
      const float s = (float)menu_scaled_px(10);
      const float l = ((float)item_pad + (float)menu_lpad()) * 0.5f - s * 0.5f;
      const float t = (float)iy + ((float)ih - s) * 0.5f;
      SkPath ck;
      ck.moveTo(l + s * 0.28f, t + s * 0.53f);
      ck.lineTo(l + s * 0.45f, t + s * 0.70f);
      ck.lineTo(l + s * 0.72f, t + s * 0.31f);
      c->drawPath(ck, cp);
    }

    // Text
    const char *raw_txt = it->m_name.Get();
    if (raw_txt && raw_txt[0]) {
      WDL_FastString stripBuf2;
      const char *txt = menu_strip_accel(raw_txt, stripBuf2);
      if (txt && txt[0]) {
        SkPaint tp;
        tp.setAntiAlias(true);
        tp.setColor(textCol);

        SkFontMetrics fm;
        mw->font.getMetrics(&fm);
        float ty = (float)iy + (float)ih / 2.0f - (fm.fAscent + fm.fDescent) / 2.0f;

        swell_skcanvas_drawtext_utf8(c, txt, (int)strlen(txt),
                                     (float)menu_lpad(), ty, mw->font, tp);
      }

      // Shortcut text (right-aligned, skip if submenu to avoid arrow overlap)
      if (!is_popup) {
        const char *shortcut = menu_get_shortcut(raw_txt);
        if (shortcut && shortcut[0]) {
          SkRect sb;
          swell_skfont_measure_utf8(mw->font, shortcut, (int)strlen(shortcut), &sb);
          float sw = sb.width();
          float sx = (float)(mw->w - g_swell_theme.padding_menu_item_h - sw);

          SkPaint sp;
          sp.setAntiAlias(true);
          sp.setColor(textCol);

          SkFontMetrics fm;
          mw->font.getMetrics(&fm);
          float ty = (float)iy + (float)ih / 2.0f - (fm.fAscent + fm.fDescent) / 2.0f;

          swell_skcanvas_drawtext_utf8(c, shortcut, (int)strlen(shortcut),
                                       sx, ty, mw->font, sp);
        }
      }
    }

    // Submenu arrow
    if (is_popup) {
      SkPaint ap;
      ap.setAntiAlias(true);
      ap.setColor(textCol);
      ap.setStyle(SkPaint::kStroke_Style);
      ap.setStrokeCap(SkPaint::kRound_Cap);
      ap.setStrokeJoin(SkPaint::kRound_Join);
      ap.setStrokeWidth((float)menu_scaled_px(1));
      float ax = (float)(mw->w - item_pad - menu_scaled_px(10));
      float ay = (float)(iy + ih / 2);
      float hw = (float)menu_scaled_px(2);
      float hh = (float)menu_scaled_px(3);
      SkPath arrow;
      arrow.moveTo(ax - hw * 0.5f, ay - hh);
      arrow.lineTo(ax + hw * 0.5f, ay);
      arrow.lineTo(ax - hw * 0.5f, ay + hh);
      c->drawPath(arrow, ap);
    }
  }

  c->restore();

  const int max_scroll = menu_max_scroll(mw);
  const int ind_h = menu_scroll_indicator_h();
  if (max_scroll > 0) {
    auto draw_scroll_overlay = [&](bool top) {
      SkRect rr = top ?
          SkRect::MakeLTRB(0.0f, 0.0f, (float)mw->w, (float)ind_h) :
          SkRect::MakeLTRB(0.0f, (float)(mw->h - ind_h),
                           (float)mw->w, (float)mw->h);

      SkPaint op;
      op.setAntiAlias(false);
      op.setColor(swell_to_sk(th.bg_menu, 220));
      c->drawRect(rr, op);

      SkPaint ap;
      ap.setAntiAlias(true);
      ap.setColor(swell_to_sk(th.fg_text, 210));
      ap.setStyle(SkPaint::kFill_Style);

      const float cx = (float)mw->w * 0.5f;
      const float cy = top ? (float)ind_h * 0.48f
                           : (float)mw->h - (float)ind_h * 0.48f;
      const float hw = (float)menu_scaled_px(5);
      const float hh = (float)menu_scaled_px(4);
      SkPath arrow;
      if (top) {
        arrow.moveTo(cx, cy - hh);
        arrow.lineTo(cx - hw, cy + hh);
        arrow.lineTo(cx + hw, cy + hh);
      } else {
        arrow.moveTo(cx, cy + hh);
        arrow.lineTo(cx - hw, cy - hh);
        arrow.lineTo(cx + hw, cy - hh);
      }
      arrow.close();
      c->drawPath(arrow, ap);
    };

    if (mw->scroll_y > 0) draw_scroll_overlay(true);
    if (mw->scroll_y < max_scroll) draw_scroll_overlay(false);
  }

  c->restore();
}

// ---------------------------------------------------------------------------
// Flush Skia surface to SDL texture/renderer
// ---------------------------------------------------------------------------

static void menu_present(MenuWindow *mw)
{
  if (!mw->surface || !mw->renderer) return;
  SkPixmap pm;
  if (!mw->surface->peekPixels(&pm)) return;

  int pw = mw->surface->width();
  int ph = mw->surface->height();

  if (!mw->texture) {
    mw->texture = SDL_CreateTexture(mw->renderer,
        SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, pw, ph);
    if (!mw->texture) return;
    SDL_SetTextureBlendMode(mw->texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
  }

  SDL_SetRenderDrawColor(mw->renderer, 0, 0, 0, 0);
  SDL_RenderClear(mw->renderer);

  SDL_Rect r = { 0, 0, pw, ph };
  SDL_UpdateTexture(mw->texture, &r, pm.addr(), (int)pm.rowBytes());
  SDL_FRect fr = { 0, 0, (float)pw, (float)ph };
  SDL_RenderTexture(mw->renderer, mw->texture, &fr, &fr);
  SDL_RenderPresent(mw->renderer);
}

static void menu_close_child(MenuWindow *mw);

static bool menu_scroll_to(MenuWindow *mw, int y)
{
  if (!mw) return false;
  const int old_y = mw->scroll_y;
  mw->scroll_y = y;
  menu_clamp_scroll(mw);
  if (mw->scroll_y == old_y) return false;
  menu_close_child(mw);
  mw->hovered = -1;
  menu_draw(mw);
  menu_present(mw);
  return true;
}

static bool menu_scroll_by(MenuWindow *mw, int dy)
{
  return menu_scroll_to(mw, mw ? mw->scroll_y + dy : 0);
}

static void menu_ensure_item_visible(MenuWindow *mw, int idx)
{
  if (!mw || idx < 0 || idx >= mw->n_items) return;
  const int ind_h = menu_scroll_indicator_h();
  int view_top = mw->scroll_y;
  int view_bottom = mw->scroll_y + mw->h;
  if (mw->scroll_y > 0) view_top += ind_h;
  if (mw->scroll_y < menu_max_scroll(mw)) view_bottom -= ind_h;

  int new_scroll = mw->scroll_y;
  if (mw->item_y[idx] < view_top)
    new_scroll = mw->item_y[idx] - (mw->scroll_y > 0 ? ind_h : 0);
  else if (mw->item_y[idx + 1] > view_bottom)
    new_scroll = mw->item_y[idx + 1] - mw->h +
                 (mw->scroll_y < menu_max_scroll(mw) ? ind_h : 0);

  if (new_scroll != mw->scroll_y)
    menu_scroll_to(mw, new_scroll);
}

static void menu_clear_scroll_hover(MenuWindow *mw)
{
  while (mw) {
    mw->scroll_hover_dir = 0;
    mw = mw->child;
  }
}

// ---------------------------------------------------------------------------
// Active menu tree: SDL events are offered by SWELL_RunEvents before normal
// HWND dispatch. This keeps menubar popups out of a nested SDL wait loop.
// ---------------------------------------------------------------------------

static MenuWindow *menu_root(MenuWindow *mw)
{
  while (mw && mw->parent) mw = mw->parent;
  return mw;
}

static MenuWindow *menu_leaf(MenuWindow *mw)
{
  while (mw && mw->child) mw = mw->child;
  return mw;
}

static void menu_hide_tree(MenuWindow *mw)
{
  if (!mw) return;
  if (mw->child) menu_hide_tree(mw->child);
  if (mw->sdlwin) SDL_HideWindow(mw->sdlwin);
}

static void menu_close_child(MenuWindow *mw)
{
  if (mw && mw->child) {
    delete mw->child;
    mw->child = NULL;
  }
}

static void menu_finish(MenuWindow *mw, int result)
{
  MenuWindow *root = menu_root(mw);
  if (!root || root->done) return;
  root->result = result;
  root->done = true;
  menu_hide_tree(root);
  if (g_active_menu == root) g_active_menu = NULL;

  if (!root->sync_waiting) {
    HWND owner = root->owner_hwnd;
    const int flags = root->flags;
    if (owner) owner->Retain();
    if (result && !(flags & TPM_RETURNCMD) && !(flags & TPM_NONOTIFY))
      SendMessage(owner, WM_COMMAND, (WPARAM)result, 0);
    if (owner) owner->Release();
    delete root;
  }
}

static MenuWindow *menu_find_by_window_id(MenuWindow *mw, SDL_WindowID id)
{
  if (!mw) return NULL;
  if (mw->window_id == id) return mw;
  return menu_find_by_window_id(mw->child, id);
}

static bool menu_local_point_to_item(MenuWindow *mw, float logical_x,
                                     float logical_y, int *idx)
{
  if (!mw || !idx) return false;
  int x = (int)(swell_log_to_phys(logical_x) + 0.5f);
  int y = (int)(swell_log_to_phys(logical_y) + 0.5f);
  if (x < 0 || x >= mw->w || y < 0 || y >= mw->h) {
    *idx = -1;
    return false;
  }
  if (menu_scroll_zone_at(mw, y)) {
    *idx = -1;
    return true;
  }
  *idx = menu_hittest(mw, y + mw->scroll_y);
  return true;
}

static bool menu_point_is_leaving_toward_child(MenuWindow *mw, float logical_x,
                                               float logical_y)
{
  if (!mw || !mw->child || mw->hovered < 0 || mw->hovered >= mw->n_items)
    return false;

  const int x = (int)(swell_log_to_phys(logical_x) + 0.5f);
  const int y = (int)(swell_log_to_phys(logical_y) + 0.5f);
  const int top = mw->item_y[mw->hovered] - mw->scroll_y - menu_scaled_px(2);
  const int bottom = mw->item_y[mw->hovered + 1] - mw->scroll_y + menu_scaled_px(2);
  const int slack = menu_scaled_px(3);

  return x >= mw->w - slack && x <= mw->w + slack &&
         y >= top && y < bottom;
}

static bool menu_outside_down_is_other_menubar_item(MenuWindow *root,
                                                    const SDL_Event *evt)
{
  if (!root || !evt || evt->type != SDL_EVENT_MOUSE_BUTTON_DOWN ||
      evt->button.button != SDL_BUTTON_LEFT)
    return false;

  HWND owner = root->owner_hwnd;
  if (!owner || !owner->m_menu || owner->m_parent) return false;

  SDL_Window *owner_win = (SDL_Window *)owner->m_oswindow;
  if (!owner_win || evt->button.windowID != SDL_GetWindowID(owner_win))
    return false;

  const int wx = (int)(swell_log_to_phys(evt->button.x) + 0.5f);
  const int wy = (int)(swell_log_to_phys(evt->button.y) + 0.5f);
  const int sx = owner->m_position.left + wx;
  const int sy = owner->m_position.top + wy;
  if (SendMessage(owner, WM_NCHITTEST, 0, MAKELPARAM(sx, sy)) != HTMENU)
    return false;

  RECT item_sr = {};
  const int idx = swell_menubar_hittest(owner, wx, &item_sr);
  return idx >= 0 && GetSubMenu(owner->m_menu, idx) != root->menu;
}

static int menu_menubar_hittest_from_event(MenuWindow *root,
                                           const SDL_Event *evt,
                                           RECT *item_sr)
{
  if (!root || !evt) return -1;
  HWND owner = root->owner_hwnd;
  if (!owner || !owner->m_menu || owner->m_parent) return -1;

  SDL_WindowID window_id = 0;
  float ex = 0.0f, ey = 0.0f;
  if (evt->type == SDL_EVENT_MOUSE_MOTION) {
    window_id = evt->motion.windowID;
    ex = evt->motion.x;
    ey = evt->motion.y;
  } else if (evt->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             evt->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    window_id = evt->button.windowID;
    ex = evt->button.x;
    ey = evt->button.y;
  } else {
    return -1;
  }

  SDL_Window *owner_win = (SDL_Window *)owner->m_oswindow;
  if (!owner_win || window_id != SDL_GetWindowID(owner_win)) return -1;

  const int wx = (int)(swell_log_to_phys(ex) + 0.5f);
  const int wy = (int)(swell_log_to_phys(ey) + 0.5f);
  const int sx = owner->m_position.left + wx;
  const int sy = owner->m_position.top + wy;
  if (SendMessage(owner, WM_NCHITTEST, 0, MAKELPARAM(sx, sy)) != HTMENU)
    return -1;

  return swell_menubar_hittest(owner, wx, item_sr);
}

static bool menu_is_owner_menubar_submenu(MenuWindow *root)
{
  if (!root || !root->owner_hwnd || !root->owner_hwnd->m_menu) return false;
  HMENU bar = root->owner_hwnd->m_menu;
  const int n = GetMenuItemCount(bar);
  for (int i = 0; i < n; i++) {
    if (GetSubMenu(bar, i) == root->menu) return true;
  }
  return false;
}

static int menu_phys_to_log_i(int phys)
{
  const float v = swell_phys_to_log((float)phys);
  return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static int menu_phys_to_log_ceil_i(int phys)
{
  const float v = swell_phys_to_log((float)phys);
  int i = (int)v;
  if ((float)i < v) i++;
  return i;
}

static MenuWindow *menu_create_window(HMENU hMenu, int sx, int sy,
                                      HWND owner_hwnd, int flags,
                                      MenuWindow *parent)
{
  if (!hMenu || !owner_hwnd) return NULL;

  MenuWindow *mw = new MenuWindow;
  mw->menu = hMenu;
  mw->owner_hwnd = owner_hwnd;
  mw->owner_hwnd->Retain();
  mw->flags = flags;
  mw->parent = parent;

  mw->font.setTypeface(menu_get_typeface());
  mw->font.setSize((float)menu_font_sz());
  if (g_swell_subpixel_text) {
    mw->font.setSubpixel(true);
    mw->font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
  } else {
    mw->font.setEdging(SkFont::Edging::kAntiAlias);
  }

  menu_measure(mw);

  SDL_Window *parent_sdlwin = parent ? parent->sdlwin : NULL;
  int parent_lx = 0, parent_ly = 0, parent_lw = 0, parent_lh = 0;
  if (parent_sdlwin) {
    parent_lx = parent->l_sx;
    parent_ly = parent->l_sy;
    parent_lw = parent->w;
    parent_lh = parent->h;
  } else {
    HWND__ *osw = owner_hwnd;
    while (osw && !osw->m_oswindow)
      osw = osw->m_owner ? osw->m_owner : (HWND__*)osw->m_parent;
    parent_sdlwin = osw ? (SDL_Window *)osw->m_oswindow : NULL;
    if (parent_sdlwin) {
      SDL_GetWindowPosition(parent_sdlwin, &parent_lx, &parent_ly);
      SDL_GetWindowSize(parent_sdlwin, &parent_lw, &parent_lh);
    }
  }
  if (!parent_sdlwin) {
    delete mw;
    return NULL;
  }

  int l_sx = menu_phys_to_log_i(sx);
  int l_sy = menu_phys_to_log_i(sy);
  int l_w = menu_phys_to_log_ceil_i(mw->w);
  int l_content_h = menu_phys_to_log_ceil_i(mw->content_h);
  if (l_w < 1) l_w = 1;
  if (l_content_h < 1) l_content_h = 1;

  RECT work_phys = { 0, 0, 1024, 768 };
  RECT source_phys = { sx, sy, sx + 1, sy + 1 };
  SWELL_GetViewPort(&work_phys, &source_phys, true);
  SDL_Rect work = {
    menu_phys_to_log_i(work_phys.left),
    menu_phys_to_log_i(work_phys.top),
    menu_phys_to_log_i(work_phys.right - work_phys.left),
    menu_phys_to_log_i(work_phys.bottom - work_phys.top)
  };
  if (work.w < 1) work.w = 1;
  if (work.h < 1) work.h = 1;

  if (l_sx + l_w > work.x + work.w) l_sx = work.x + work.w - l_w;
  if (l_sx < work.x) l_sx = work.x;

  const bool anchored_menu =
      parent != NULL || (!parent && menu_is_owner_menubar_submenu(mw));
  const char *video_driver = SDL_GetCurrentVideoDriver();
  const bool root_menubar_relative =
      anchored_menu && !parent && parent_lh > 0 &&
      video_driver && !strcmp(video_driver, "wayland");

  int l_h = l_content_h;
  if (root_menubar_relative) {
    if (l_sy < 0) l_sy = 0;
    if (l_sy >= parent_lh) l_sy = parent_lh - 1;
    int avail_h = parent_lh - l_sy;
    if (avail_h < 1) avail_h = 1;
    if (l_h > avail_h) l_h = avail_h;
  } else if (anchored_menu) {
    if (l_sy < work.y) l_sy = work.y;
    if (l_sy >= work.y + work.h) l_sy = work.y + work.h - 1;
    int avail_h = work.y + work.h - l_sy;
    if (avail_h < 1) avail_h = 1;
    if (l_h > avail_h) l_h = avail_h;
  } else {
    if (l_content_h > work.h && work.h > 0) {
      l_sy = work.y;
      l_h = work.h;
    } else {
      if (l_sy + l_content_h > work.y + work.h)
        l_sy = work.y + work.h - l_content_h;
      if (l_sy < work.y) l_sy = work.y;
      l_h = l_content_h;
    }
  }
  if (l_h < 1) l_h = 1;
  mw->h = (l_h >= l_content_h) ? mw->content_h : (int)swell_log_to_phys((float)l_h);
  if (mw->h < 1) mw->h = 1;
  menu_clamp_scroll(mw);
  sx = (int)swell_log_to_phys((float)l_sx);
  sy = (int)swell_log_to_phys((float)l_sy);
  mw->sx = sx;
  mw->sy = sy;
  mw->l_sx = l_sx;
  mw->l_sy = l_sy;

  const int rel_lx = l_sx - parent_lx;
  const int rel_ly = l_sy - parent_ly;

  mw->sdlwin = SDL_CreatePopupWindow(parent_sdlwin,
      rel_lx, rel_ly, l_w, l_h,
      SDL_WINDOW_POPUP_MENU | SDL_WINDOW_BORDERLESS |
      SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_TRANSPARENT);
  if (!mw->sdlwin) {
    delete mw;
    return NULL;
  }

  mw->window_id = SDL_GetWindowID(mw->sdlwin);
  mw->renderer = SDL_CreateRenderer(mw->sdlwin, NULL);
  if (!mw->renderer) {
    delete mw;
    return NULL;
  }

  int pix_w = mw->w, pix_h = mw->h;
  SDL_GetWindowSizeInPixels(mw->sdlwin, &pix_w, &pix_h);
  if (pix_w < 1) pix_w = mw->w;
  if (pix_h < 1) pix_h = mw->h;
  mw->w = pix_w;
  mw->h = pix_h;
  menu_clamp_scroll(mw);
  mw->surface = SkSurfaces::Raster(
      SkImageInfo::Make(pix_w, pix_h, kBGRA_8888_SkColorType, kPremul_SkAlphaType),
      &g_swell_surfprops);
  if (!mw->surface) {
    delete mw;
    return NULL;
  }

  menu_draw(mw);
  menu_present(mw);
  SDL_ShowWindow(mw->sdlwin);
  if (!mw->parent) {
    SetProp(mw->owner_hwnd, "SWELL_MenuOwner", (HANDLE)1);
    Uint32 type = menu_scroll_timer_event_type();
    if (type != (Uint32)-1)
      mw->scroll_timer = SDL_AddTimer(35, menu_scroll_timer_cb, NULL);
  }
  return mw;
}

static bool menu_switch_to_menubar_item(MenuWindow *root, const SDL_Event *evt)
{
  if (!root || !menu_is_owner_menubar_submenu(root)) return false;

  RECT item_sr = {};
  const int idx = menu_menubar_hittest_from_event(root, evt, &item_sr);
  if (idx < 0) return false;

  HWND owner = root->owner_hwnd;
  HMENU sub = GetSubMenu(owner->m_menu, idx);
  if (!sub) return false;
  if (sub == root->menu) return true;

  const int flags = root->flags;
  const bool ignore_up = root->ignore_initial_button_up;
  owner->Retain();
  menu_finish(root, 0);

  SendMessage(owner, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(idx, FALSE));
  MenuWindow *nw = menu_create_window(sub, item_sr.left, item_sr.bottom,
                                      owner, flags, NULL);
  if (nw) {
    nw->ignore_initial_button_up = ignore_up;
    g_active_menu = nw;
  }
  owner->Release();
  return true;
}

static bool menu_open_child(MenuWindow *mw, int idx)
{
  if (!mw || idx < 0) return false;
  SWELL_MenuItem *it = mw->menu->m_items.Get(idx);
  if (!it || !(it->m_flags & MF_POPUP) || !it->m_submenu) return false;
  if (it->m_flags & (MF_GRAYED|MF_DISABLED|MF_SEPARATOR)) return false;

  menu_close_child(mw);
  SendMessage(mw->owner_hwnd, WM_INITMENUPOPUP, (WPARAM)it->m_submenu,
              MAKELPARAM(idx, FALSE));
  mw->child = menu_create_window(it->m_submenu, mw->sx + mw->w,
                                 mw->sy + mw->item_y[idx] - mw->scroll_y,
                                 mw->owner_hwnd, mw->flags, mw);
  return mw->child != NULL;
}

static void menu_activate_item(MenuWindow *mw, int idx)
{
  if (!mw || idx < 0) return;
  SWELL_MenuItem *it = mw->menu->m_items.Get(idx);
  if (!it || (it->m_flags & (MF_GRAYED|MF_DISABLED|MF_SEPARATOR))) return;

  mw->hovered = idx;
  menu_ensure_item_visible(mw, idx);
  mw->hovered = idx;
  menu_draw(mw);
  menu_present(mw);

  if ((it->m_flags & MF_POPUP) && it->m_submenu) {
    menu_open_child(mw, idx);
  } else {
    menu_finish(mw, it->m_id);
  }
}

static void menu_select_delta(MenuWindow *mw, int dir)
{
  if (!mw || mw->n_items <= 0) return;
  int i = mw->hovered;
  for (int step = 0; step < mw->n_items; step++) {
    i += dir;
    if (i < 0) i = mw->n_items - 1;
    else if (i >= mw->n_items) i = 0;
    SWELL_MenuItem *it = mw->menu->m_items.Get(i);
    if (it && !(it->m_flags & (MF_SEPARATOR|MF_GRAYED|MF_DISABLED))) {
      if (i != mw->hovered) {
        menu_close_child(mw);
        mw->hovered = i;
        menu_ensure_item_visible(mw, i);
        mw->hovered = i;
        menu_draw(mw);
        menu_present(mw);
      }
      return;
    }
  }
}

static void menu_select_edge(MenuWindow *mw, int dir)
{
  if (!mw) return;
  int i = dir > 0 ? 0 : mw->n_items - 1;
  while (i >= 0 && i < mw->n_items) {
    SWELL_MenuItem *it = mw->menu->m_items.Get(i);
    if (it && !(it->m_flags & (MF_SEPARATOR|MF_GRAYED|MF_DISABLED))) {
      menu_close_child(mw);
      mw->hovered = i;
      menu_ensure_item_visible(mw, i);
      mw->hovered = i;
      menu_draw(mw);
      menu_present(mw);
      return;
    }
    i += dir;
  }
}

bool swell_menu_sdl_handle_event(SDL_Event *evt)
{
  if (!evt || !g_active_menu) return false;

  MenuWindow *root = g_active_menu;
  if (evt->type == menu_scroll_timer_event_type()) {
    for (MenuWindow *mw = root; mw; mw = mw->child) {
      if (mw->scroll_hover_dir) {
        const int step = menu_item_h() > 1 ? menu_item_h() / 2 : 1;
        menu_scroll_by(mw, mw->scroll_hover_dir * step);
        return true;
      }
    }
    return true;
  }

  switch (evt->type) {
    case SDL_EVENT_QUIT:
      menu_finish(root, 0);
      return false;

    case SDL_EVENT_KEY_DOWN: {
      MenuWindow *mw = menu_leaf(root);
      if (!mw) return false;
      SDL_Keycode key = evt->key.key;
      if (key == SDLK_ESCAPE) {
        menu_finish(root, 0);
      } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        menu_activate_item(mw, mw->hovered);
      } else if (key == SDLK_DOWN) {
        menu_select_delta(mw, 1);
      } else if (key == SDLK_UP) {
        menu_select_delta(mw, -1);
      } else if (key == SDLK_HOME) {
        menu_select_edge(mw, 1);
      } else if (key == SDLK_END) {
        menu_select_edge(mw, -1);
      } else if (key == SDLK_RIGHT) {
        if (mw->hovered >= 0) menu_open_child(mw, mw->hovered);
      } else if (key == SDLK_LEFT) {
        if (mw->parent) {
          menu_close_child(mw->parent);
        }
      }
      return true;
    }

    case SDL_EVENT_MOUSE_MOTION: {
      MenuWindow *mw = menu_find_by_window_id(root, evt->motion.windowID);
      if (!mw) {
        menu_clear_scroll_hover(root);
        return menu_switch_to_menubar_item(root, evt);
      }

      const int local_y = (int)(swell_log_to_phys(evt->motion.y) + 0.5f);
      const int scroll_zone = menu_scroll_zone_at(mw, local_y);
      menu_clear_scroll_hover(root);
      if (scroll_zone) {
        mw->scroll_hover_dir = scroll_zone;
        if (mw->hovered != -1 || mw->child) {
          menu_close_child(mw);
          mw->hovered = -1;
          menu_draw(mw);
          menu_present(mw);
        }
        const int step = menu_item_h() > 1 ? menu_item_h() / 2 : 1;
        menu_scroll_by(mw, scroll_zone * step);
        return true;
      }

      int newhov = -1;
      menu_local_point_to_item(mw, evt->motion.x, evt->motion.y, &newhov);
      if (newhov != mw->hovered) {
        if (newhov < 0 &&
            menu_point_is_leaving_toward_child(mw, evt->motion.x, evt->motion.y))
          return true;
        menu_close_child(mw);
        mw->hovered = newhov;
        menu_draw(mw);
        menu_present(mw);
        if (newhov >= 0) menu_open_child(mw, newhov);
      }
      return true;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
      MenuWindow *mw = menu_find_by_window_id(root, evt->wheel.windowID);
      if (!mw) return false;
      menu_clear_scroll_hover(root);
      if (menu_max_scroll(mw) <= 0) return true;

      const float wy = evt->wheel.y;
      int dy = (int)(-wy * (float)menu_item_h() * 3.0f);
      if (dy == 0 && wy != 0.0f)
        dy = wy > 0.0f ? -menu_item_h() : menu_item_h();
      if (dy != 0) menu_scroll_by(mw, dy);
      return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      MenuWindow *mw = menu_find_by_window_id(root, evt->button.windowID);
      if (!mw) {
        const bool other_menubar_item =
            menu_outside_down_is_other_menubar_item(root, evt);
        menu_finish(root, 0);
        return !other_menubar_item;
      }
      menu_clear_scroll_hover(root);
      int idx = -1;
      if (!menu_local_point_to_item(mw, evt->button.x, evt->button.y, &idx)) {
        menu_finish(root, 0);
        return false;
      }
      if (idx != mw->hovered) {
        menu_close_child(mw);
        mw->hovered = idx;
        menu_draw(mw);
        menu_present(mw);
      }
      return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
      if (root->ignore_initial_button_up) {
        root->ignore_initial_button_up = false;
        return true;
      }

      MenuWindow *mw = menu_find_by_window_id(root, evt->button.windowID);
      if (!mw) {
        menu_finish(root, 0);
        return false;
      }
      menu_clear_scroll_hover(root);
      int idx = -1;
      if (!menu_local_point_to_item(mw, evt->button.x, evt->button.y, &idx)) {
        menu_finish(root, 0);
        return false;
      }
      menu_activate_item(mw, idx);
      return true;
    }

    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
      MenuWindow *mw = menu_find_by_window_id(root, evt->window.windowID);
      if (mw) {
        mw->scroll_hover_dir = 0;
        if (!mw->child) {
          mw->hovered = -1;
          menu_draw(mw);
          menu_present(mw);
        }
        return true;
      }
      return false;
    }

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_DESTROYED:
      if (menu_find_by_window_id(root, evt->window.windowID)) {
        menu_finish(root, 0);
        return true;
      }
      return false;

    default:
      return false;
  }
}

int TrackPopupMenu(HMENU hMenu, int flags, int xpos, int ypos,
                   int resvd, HWND hwnd, const RECT *r)
{
  (void)resvd; (void)r;
  fprintf(stderr, "[SWELL] TrackPopupMenu(hMenu=%p, flags=0x%x, xy=%d,%d, hwnd=%p title='%s' parent=%p)\n",
          (void*)hMenu, flags, xpos, ypos, (void*)hwnd, hwnd ? hwnd->m_title.Get() : "(null)", hwnd ? (void*)hwnd->m_parent : nullptr);
  if (!hMenu || !hwnd) return 0;

  ReleaseCapture();
  if (g_active_menu) menu_finish(g_active_menu, 0);

  // Send WM_INITMENUPOPUP before showing
  if (hwnd) SendMessage(hwnd, WM_INITMENUPOPUP, (WPARAM)hMenu,
                        MAKELPARAM(0, FALSE));

  MenuWindow *root = menu_create_window(hMenu, xpos, ypos, hwnd, flags, NULL);
  if (!root) return 0;
  root->ignore_initial_button_up =
      (g_swell_sdl_current_event_type == SDL_EVENT_MOUSE_BUTTON_DOWN);
  g_active_menu = root;

  if (!(flags & TPM_RETURNCMD)) {
    return TRUE;
  }

  root->sync_waiting = true;
  while (!root->done) {
    SWELL_RunEvents();
    SWELL_MessageQueue_Flush();
    if (!root->done) Sleep(1);
  }

  int cmd = root->result;
  delete root;
  return cmd;
}

#else  // headless stub for TrackPopupMenu

int TrackPopupMenu(HMENU hMenu, int flags, int xpos, int ypos,
                   int resvd, HWND hwnd, const RECT *r)
{
  (void)hMenu; (void)flags; (void)xpos; (void)ypos;
  (void)resvd; (void)hwnd; (void)r;
  return 0;
}

#endif // SWELL_TARGET_SDL3

// ============================================================================
// WM_NCPAINT / menu bar drawing (called from DefWindowProc in swell-wnd.cpp)
// ============================================================================

// Check if a menu item name requests right-alignment via a leading
// non-alphanumeric, non-ampersand character (Win32/REAPER convention).
static bool wantRightAlignedMenuBarItem(const char *p)
{
  if (!p) return false;
  char c = *p;
  return c > 0 && c != '&' && !isalnum((unsigned char)c);
}

// Shared layout helper: iterates menu bar items measuring widths.
// If hdc is non-NULL, draws each item (with highlight if hilight_idx matches).
// If hit_x >= 0, finds which item contains that window-x coordinate.
// Returns item index of hit (or -1), writes item rect (window coords) to rect_out if non-NULL.
static int menubar_layout(HWND hwnd, HDC hdc, int hit_x, int hilight_idx, RECT *rect_out)
{
  if (!hwnd || !hwnd->m_menu) return -1;
  HMENU menu = hwnd->m_menu;
  const swell_theme &th = g_swell_theme;
  int barH = th.menubar_height;

  // Need a scratch DC for text measurement when no paint DC is available
  HDC mdc = hdc;
  bool free_mdc = false;
  if (!mdc) {
    mdc = SWELL_CreateMemContext(NULL, 16, 16);
    free_mdc = true;
  }
  if (!mdc) return -1;

  HFONT oldf = (HFONT)SelectObject(mdc, SWELL_GetDefaultFont());

  int n = menu->m_items.GetSize();
  const int pad_h = th.padding_button_h;
  const int margin = pad_h / 2;

  // Get window width for right-alignment computation.
  // Use m_position (pre-NCCALCSIZE) so the bar spans the full window width.
  const int winW = hwnd->m_position.right - hwnd->m_position.left;

  // --- Pass 1: measure items, detect right-aligned last item, compute positions ---
  WDL_FastString stripMb;
  bool lastRight = false;
  int itemX[128], itemW[128];
  if (n > 128) n = 128; // safety clamp

  {
    int x = margin;
    for (int i = 0; i < n; i++) {
      SWELL_MenuItem *it = menu->m_items.Get(i);
      itemX[i] = x;
      itemW[i] = 0;
      if (!it) continue;
      const char *raw = it->m_name.Get();
      if (!raw || !raw[0]) continue;

      if (i == n - 1)
        lastRight = wantRightAlignedMenuBarItem(raw);

      const char *txt = menu_strip_accel(raw, stripMb);
      if (!txt || !txt[0]) continue;

      RECT msz = { 0, 0, 0, 0 };
      SWELL_DrawText(mdc, txt, -1, &msz, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
      itemW[i] = (msz.right - msz.left) + pad_h;
      x += itemW[i] + 2;
    }

    // Reposition last item if right-aligned
    if (lastRight && n > 0 && itemW[n - 1] > 0) {
      const int right_x = winW - margin - itemW[n - 1];
      // wdl_max: only move right if it won't collide with left-flow items
      if (right_x > itemX[n - 1]) {
        itemX[n - 1] = right_x;
      }
    }
  }

  // --- Pass 2: hit-test ---
  int hit = -1;
  if (hit_x >= 0) {
    for (int i = 0; i < n; i++) {
      if (itemW[i] == 0) continue;
      if (hit_x >= itemX[i] && hit_x < itemX[i] + itemW[i]) {
        hit = i;
        if (rect_out) {
          rect_out->left   = itemX[i];
          rect_out->right  = itemX[i] + itemW[i];
          rect_out->top    = 0;
          rect_out->bottom = barH;
        }
        break;
      }
    }
  }

  // --- Pass 3: draw (only when paint DC is available) ---
  if (hdc) {
    const int item_r = th.corner_radius / 2;
    for (int i = 0; i < n; i++) {
      if (itemW[i] == 0) continue;
      SWELL_MenuItem *it = menu->m_items.Get(i);
      if (!it) continue;
      const char *raw = it->m_name.Get();
      if (!raw || !raw[0]) continue;
      const char *txt = menu_strip_accel(raw, stripMb);
      if (!txt || !txt[0]) continue;

      RECT itemR = { itemX[i], 0, itemX[i] + itemW[i], barH };

      bool hl = (i == hilight_idx);
      if (hl) {
        HPEN np = (HPEN)GetStockObject(NULL_PEN);
        HBRUSH bgbr = CreateSolidBrush((COLORREF)th.bg_menubar_hover);
        HGDIOBJ op = SelectObject(hdc, np);
        HGDIOBJ ob = SelectObject(hdc, bgbr);
        int inset = 2;
        RoundRect(hdc, itemR.left, itemR.top + inset,
                  itemR.right, itemR.bottom - inset,
                  item_r * 2, item_r * 2);
        SelectObject(hdc, op);
        SelectObject(hdc, ob); DeleteObject(bgbr);
      }
      SetTextColor(hdc, (COLORREF)th.fg_text);
      SetBkMode(hdc, TRANSPARENT);
      SWELL_DrawText(hdc, txt, -1, &itemR,
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
  }

  SelectObject(mdc, oldf);
  if (free_mdc) SWELL_DeleteGfxContext(mdc);
  return hit;
}

void swell_paint_menubar(HWND hwnd, HDC hdc)
{
  if (!hwnd || !hwnd->m_menu || !hdc) return;
  const swell_theme &th = g_swell_theme;
  int barH = th.menubar_height;

  RECT cr; GetClientRect(hwnd, &cr);
  RECT barR = { 0, 0, cr.right, barH };
  HBRUSH br = CreateSolidBrush(th.bg_menubar);
  FillRect(hdc, &barR, br);
  DeleteObject(br);

  // 1-px separator at bottom of the bar
  HPEN sp = CreatePen(PS_SOLID, th.border_width, (COLORREF)th.border);
  HGDIOBJ osp = SelectObject(hdc, sp);
  MoveToEx(hdc, 0, barH - 1, NULL);
  LineTo(hdc, cr.right, barH - 1);
  SelectObject(hdc, osp); DeleteObject(sp);

  menubar_layout(hwnd, hdc, -1, -1, NULL);
}

// Returns top-level menu item index hit at window-x coordinate win_x, or -1.
// item_screen_rect_out receives the item rect in screen coords if non-NULL.
int swell_menubar_hittest(HWND hwnd, int win_x, RECT *item_screen_rect_out)
{
  if (!hwnd || !hwnd->m_menu) return -1;
  RECT wr = { 0, 0, 0, 0 };
  int hit = menubar_layout(hwnd, NULL, win_x, -1, &wr);
  if (hit >= 0 && item_screen_rect_out) {
    int sx = hwnd->m_position.left;
    int sy = hwnd->m_position.top;
#ifdef SWELL_TARGET_SDL3
    if (!hwnd->m_parent && hwnd->m_oswindow) {
      int lx = 0, ly = 0;
      SDL_GetWindowPosition((SDL_Window *)hwnd->m_oswindow, &lx, &ly);
      sx = (int)swell_log_to_phys((float)lx);
      sy = (int)swell_log_to_phys((float)ly);
    }
#endif
    item_screen_rect_out->left   = sx + wr.left;
    item_screen_rect_out->top    = sy + wr.top;
    item_screen_rect_out->right  = sx + wr.right;
    item_screen_rect_out->bottom = sy + wr.bottom;
  }
  return hit;
}
