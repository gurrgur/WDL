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
      const char *faces[] = { "Roboto", "Noto Sans", "Segoe UI",
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

// Metrics are now sourced from the global theme (already in physical pixels).
static inline int menu_item_h()  { return g_swell_theme.menu_item_height; }
static inline int menu_sep_h()   { return g_swell_theme.menu_separator_height; }
static inline int menu_lpad()    { return g_swell_theme.padding_menu_item_h * 2 + g_swell_theme.checkbox_size; }
static inline int menu_rpad()    { return g_swell_theme.padding_menu_item_h + g_swell_theme.checkbox_size; }
static inline int menu_vpad()    { return g_swell_theme.padding_menu_item_v; }
static inline int menu_min_w()   { return SWELL_UI_SCALE(160); }
static inline int menu_font_sz() { return g_swell_theme.default_font_size; }
static inline int menu_corner_r(){ return g_swell_theme.corner_radius_large; }

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
  SDL_Window    *sdlwin;
  SDL_Renderer  *renderer;
  SDL_Texture   *texture;
  sk_sp<SkSurface> surface;
  SkFont         font;
  int            w, h;
  int            hovered;   // -1 = none
  int            n_items;
  int           *item_y;    // top Y of each item (size n_items)
  bool           done;
  int            result;    // selected item ID, 0 = cancelled

  MenuWindow() : menu(NULL), sdlwin(NULL), renderer(NULL), texture(NULL),
    hovered(-1), done(false), result(0), w(0), h(0),
    n_items(0), item_y(NULL) {}
  ~MenuWindow() {
    free(item_y);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (sdlwin) SDL_DestroyWindow(sdlwin);
  }
};

// Forward declaration for submenu
static int run_menu_window(HMENU hMenu, int sx, int sy, HWND owner_hwnd,
                           MenuWindow *parent_mw, int parent_item_y);

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
      mw->font.measureText(label, strlen(label), SkTextEncoding::kUTF8, &bounds);
      int tw = (int)(bounds.width() + 0.5f);
      if (tw > maxLabelW) maxLabelW = tw;
    }

    const char *shortcut = menu_get_shortcut(raw);
    if (shortcut && shortcut[0]) {
      SkRect bounds;
      mw->font.measureText(shortcut, strlen(shortcut), SkTextEncoding::kUTF8, &bounds);
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
  mw->h = y + menu_vpad();
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

  const int item_pad = th.padding_menu_item_h / 2;

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
      cp.setStrokeWidth(2.0f);
      cp.setStyle(SkPaint::kStroke_Style);
      cp.setStrokeCap(SkPaint::kRound_Cap);
      cp.setStrokeJoin(SkPaint::kRound_Join);
      float cy = (float)(iy + ih / 2);
      float cx = (float)(item_pad + th.checkbox_size / 2);
      SkPath ck;
      ck.moveTo(cx - 5.0f, cy);
      ck.lineTo(cx - 1.0f, cy + 4.0f);
      ck.lineTo(cx + 5.0f, cy - 5.0f);
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

        c->drawSimpleText(txt, strlen(txt), SkTextEncoding::kUTF8,
                          (float)menu_lpad(), ty, mw->font, tp);
      }

      // Shortcut text (right-aligned, skip if submenu to avoid arrow overlap)
      if (!is_popup) {
        const char *shortcut = menu_get_shortcut(raw_txt);
        if (shortcut && shortcut[0]) {
          SkRect sb;
          mw->font.measureText(shortcut, strlen(shortcut), SkTextEncoding::kUTF8, &sb);
          float sw = sb.width();
          float sx = (float)(mw->w - g_swell_theme.padding_menu_item_h - sw);

          SkPaint sp;
          sp.setAntiAlias(true);
          sp.setColor(textCol);

          SkFontMetrics fm;
          mw->font.getMetrics(&fm);
          float ty = (float)iy + (float)ih / 2.0f - (fm.fAscent + fm.fDescent) / 2.0f;

          c->drawSimpleText(shortcut, strlen(shortcut), SkTextEncoding::kUTF8,
                            sx, ty, mw->font, sp);
        }
      }
    }

    // Submenu arrow
    if (is_popup) {
      SkPaint ap;
      ap.setAntiAlias(true);
      ap.setColor(textCol);
      ap.setStyle(SkPaint::kFill_Style);
      float ax = (float)(mw->w - item_pad - 6);
      float ay = (float)(iy + ih / 2);
      SkPath arrow;
      arrow.moveTo(ax,       ay - 4.0f);
      arrow.lineTo(ax + 5.0f, ay);
      arrow.lineTo(ax,       ay + 4.0f);
      arrow.close();
      c->drawPath(arrow, ap);
    }
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
    SDL_SetTextureBlendMode(mw->texture, SDL_BLENDMODE_NONE);
  }

  SDL_Rect r = { 0, 0, pw, ph };
  SDL_UpdateTexture(mw->texture, &r, pm.addr(), (int)pm.rowBytes());
  SDL_FRect fr = { 0, 0, (float)pw, (float)ph };
  SDL_RenderTexture(mw->renderer, mw->texture, &fr, &fr);
  SDL_RenderPresent(mw->renderer);
}

// ---------------------------------------------------------------------------
// run_menu_window: synchronous menu loop
// Returns selected item ID or 0 if cancelled.
// ---------------------------------------------------------------------------

static int run_menu_window(HMENU hMenu, int sx, int sy, HWND owner_hwnd,
                           MenuWindow *parent_mw, int parent_item_y)
{
  (void)parent_mw; (void)parent_item_y;
  if (!hMenu) return 0;

  MenuWindow mw;
  mw.menu = hMenu;

  // Build font
  mw.font.setTypeface(menu_get_typeface());
  mw.font.setSize((float)menu_font_sz());
  mw.font.setEdging(SkFont::Edging::kAntiAlias);

  menu_measure(&mw);

  // Clamp to screen (physical pixels)
  RECT screen;
  SWELL_GetViewPort(&screen, NULL, true);
  if (sx + mw.w > screen.right)  sx = screen.right - mw.w;
  if (sy + mw.h > screen.bottom) sy = screen.bottom - mw.h;
  if (sx < screen.left)          sx = screen.left;
  if (sy < screen.top)           sy = screen.top;

  // Convert physical → logical for SDL3
  int l_sx = swell_phys_to_log(sx);
  int l_sy = swell_phys_to_log(sy);
  int l_w  = swell_phys_to_log(mw.w);
  int l_h  = swell_phys_to_log(mw.h);
  if (l_w < 1) l_w = 1;
  if (l_h < 1) l_h = 1;

  // Walk owner_hwnd up via m_owner / m_parent to find the SDL OS window.
  // owner_hwnd may be a child HWND with no m_oswindow of its own.
  HWND__ *osw = owner_hwnd;
  while (osw && !osw->m_oswindow)
    osw = osw->m_owner ? osw->m_owner : (HWND__*)osw->m_parent;
  SDL_Window *parent_sdlwin = osw ? (SDL_Window *)osw->m_oswindow : NULL;
  if (!parent_sdlwin) return 0;

  int px, py;
  SDL_GetWindowPosition(parent_sdlwin, &px, &py);
  mw.sdlwin = SDL_CreatePopupWindow(parent_sdlwin,
      l_sx - px, l_sy - py, l_w, l_h,
      SDL_WINDOW_POPUP_MENU | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);

  if (!mw.sdlwin) return 0;

  SDL_WindowID mw_id = SDL_GetWindowID(mw.sdlwin);
  mw.renderer = SDL_CreateRenderer(mw.sdlwin, NULL);
  if (!mw.renderer) return 0;

  // Skia surface: use physical pixel dimensions for HiDPI
  int pix_w = mw.w, pix_h = mw.h;
  SDL_GetWindowSizeInPixels(mw.sdlwin, &pix_w, &pix_h);
  if (pix_w < 1) pix_w = mw.w;
  if (pix_h < 1) pix_h = mw.h;
  mw.surface = SkSurfaces::Raster(
      SkImageInfo::Make(pix_w, pix_h, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
  if (!mw.surface) return 0;

  // Initial render
  menu_draw(&mw);
  menu_present(&mw);
  SDL_ShowWindow(mw.sdlwin);

  // ---- Synchronous event loop ----
  int submenu_open = -1;  // index of currently open submenu item

  while (!mw.done) {
    SDL_Event evt;
    // Wait briefly then check timers
    if (!SDL_WaitEventTimeout(&evt, 16)) {
      continue;
    }

    switch (evt.type) {
      case SDL_EVENT_QUIT:
        mw.done = true;
        mw.result = 0;
        break;

      case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode key = evt.key.key;
        if (key == SDLK_ESCAPE) {
          mw.done = true; mw.result = 0;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
          if (mw.hovered >= 0) {
            SWELL_MenuItem *it = mw.menu->m_items.Get(mw.hovered);
            if (it && !(it->m_flags & (MF_GRAYED|MF_DISABLED|MF_SEPARATOR))) {
              if ((it->m_flags & MF_POPUP) && it->m_submenu) {
                int item_sx = swell_phys_to_log(sx + mw.w);
                int item_sy = swell_phys_to_log(sy + mw.item_y[mw.hovered]);
                SendMessage(owner_hwnd, WM_INITMENUPOPUP,
                            (WPARAM)it->m_submenu,
                            MAKELPARAM(mw.hovered, FALSE));
                int r = run_menu_window(it->m_submenu, item_sx, item_sy,
                                        owner_hwnd, &mw,
                                        mw.item_y[mw.hovered]);
                if (r != 0) { mw.done = true; mw.result = r; }
              } else {
                mw.done = true;
                mw.result = it->m_id;
              }
            }
          }
        } else if (key == SDLK_DOWN) {
          int next = mw.hovered + 1;
          while (next < mw.n_items) {
            SWELL_MenuItem *it = mw.menu->m_items.Get(next);
            if (it && !(it->m_flags & (MF_SEPARATOR|MF_GRAYED|MF_DISABLED))) break;
            next++;
          }
          if (next < mw.n_items) {
            mw.hovered = next;
            menu_draw(&mw); menu_present(&mw);
          }
        } else if (key == SDLK_UP) {
          int prev = (mw.hovered < 0 ? mw.n_items : mw.hovered) - 1;
          while (prev >= 0) {
            SWELL_MenuItem *it = mw.menu->m_items.Get(prev);
            if (it && !(it->m_flags & (MF_SEPARATOR|MF_GRAYED|MF_DISABLED))) break;
            prev--;
          }
          if (prev >= 0) {
            mw.hovered = prev;
            menu_draw(&mw); menu_present(&mw);
          }
        } else if (key == SDLK_HOME) {
          int first = 0;
          while (first < mw.n_items) {
            SWELL_MenuItem *it = mw.menu->m_items.Get(first);
            if (it && !(it->m_flags & (MF_SEPARATOR|MF_GRAYED|MF_DISABLED))) break;
            first++;
          }
          if (first < mw.n_items) {
            mw.hovered = first;
            menu_draw(&mw); menu_present(&mw);
          }
        } else if (key == SDLK_END) {
          int last = mw.n_items - 1;
          while (last >= 0) {
            SWELL_MenuItem *it = mw.menu->m_items.Get(last);
            if (it && !(it->m_flags & (MF_SEPARATOR|MF_GRAYED|MF_DISABLED))) break;
            last--;
          }
          if (last >= 0) {
            mw.hovered = last;
            menu_draw(&mw); menu_present(&mw);
          }
        }
        break;
      }

      case SDL_EVENT_MOUSE_MOTION: {
        if (evt.motion.windowID == mw_id) {
          // SDL3 mouse y is logical; menu_hittest expects physical
          int newhov = menu_hittest(&mw, swell_log_to_phys((int)evt.motion.y));
          if (newhov != mw.hovered) {
            submenu_open = -1;
            mw.hovered = newhov;
            menu_draw(&mw); menu_present(&mw);
          }
        } else {
          // Mouse moved outside menu window — dismiss
          // (But allow moving into parent — handled by checking coordinates)
          // For now only dismiss on click outside
        }
        break;
      }

      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (evt.button.windowID != mw_id) {
          // Click outside menu — dismiss
          mw.done = true;
          mw.result = 0;
        }
        break;
      }

      case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (evt.button.windowID == mw_id) {
          // SDL3 mouse y is logical; menu_hittest expects physical
          int idx = menu_hittest(&mw, swell_log_to_phys((int)evt.button.y));
          if (idx >= 0) {
            SWELL_MenuItem *it = mw.menu->m_items.Get(idx);
            if (it && !(it->m_flags & (MF_GRAYED|MF_DISABLED|MF_SEPARATOR))) {
              if ((it->m_flags & MF_POPUP) && it->m_submenu) {
                if (submenu_open != idx) {
                  submenu_open = idx;
                  int item_sx = swell_phys_to_log(sx + mw.w);
                  int item_sy = swell_phys_to_log(sy + mw.item_y[idx]);
                  SendMessage(owner_hwnd, WM_INITMENUPOPUP,
                              (WPARAM)it->m_submenu,
                              MAKELPARAM(idx, FALSE));
                  int r = run_menu_window(it->m_submenu, item_sx, item_sy,
                                          owner_hwnd, &mw,
                                          mw.item_y[idx]);
                  submenu_open = -1;
                  if (r != 0) { mw.done = true; mw.result = r; }
                }
              } else {
                mw.done = true;
                mw.result = it->m_id;
              }
            }
          }
        }
        break;
      }

      case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        if (evt.window.windowID == mw_id) {
          mw.hovered = -1;
          menu_draw(&mw); menu_present(&mw);
        }
        break;
      }

      default:
        break;
    }
  }

  return mw.result;
}

int TrackPopupMenu(HMENU hMenu, int flags, int xpos, int ypos,
                   int resvd, HWND hwnd, const RECT *r)
{
  (void)resvd; (void)r;
  if (!hMenu) return 0;

  ReleaseCapture();

  // Send WM_INITMENUPOPUP before showing
  if (hwnd) SendMessage(hwnd, WM_INITMENUPOPUP, (WPARAM)hMenu,
                        MAKELPARAM(0, FALSE));

  int cmd = run_menu_window(hMenu, xpos, ypos, hwnd, NULL, 0);

  if (cmd && !(flags & TPM_RETURNCMD) && !(flags & TPM_NONOTIFY)) {
    SendMessage(hwnd, WM_COMMAND, (WPARAM)cmd, 0);
  }
  return (flags & TPM_RETURNCMD) ? cmd : (cmd != 0);
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

  int x = th.padding_button_h / 2;
  int hit = -1;
  int n = menu->m_items.GetSize();
  WDL_FastString stripMb;
  const int pad_h = th.padding_button_h;
  const int item_r = th.corner_radius / 2;
  for (int i = 0; i < n; i++) {
    SWELL_MenuItem *it = menu->m_items.Get(i);
    if (!it) continue;
    const char *raw = it->m_name.Get();
    if (!raw || !raw[0]) continue;
    const char *txt = menu_strip_accel(raw, stripMb);
    if (!txt || !txt[0]) continue;

    RECT msz = { 0, 0, 0, 0 };
    SWELL_DrawText(mdc, txt, -1, &msz, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    int itemW = (msz.right - msz.left) + pad_h;
    RECT itemR = { x, 0, x + itemW, barH };

    if (hit_x >= x && hit_x < x + itemW) {
      hit = i;
      if (rect_out) *rect_out = itemR;
    }

    if (hdc) {
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

    x += itemW + 2;
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
    // convert window rect to screen by adding window's screen origin
    int sx = hwnd->m_position.left;
    int sy = hwnd->m_position.top;
    item_screen_rect_out->left   = sx + wr.left;
    item_screen_rect_out->top    = sy + wr.top;
    item_screen_rect_out->right  = sx + wr.right;
    item_screen_rect_out->bottom = sy + wr.bottom;
  }
  return hit;
}
