/*
  SWELL2 dialog creation and lifecycle.
  Implements SWELL_CreateDialog, SWELL_DialogBox, EndDialog,
  SWELL_Make* control constructors, and SWELL_GenerateDialogFromList.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
#include "swell-dlggen.h"
#include <cstring>
#include <cstdlib>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Current dialog-creation parameters (set by SWELL_MakeSetCurParms)
// ---------------------------------------------------------------------------

static float  g_dlg_xscale  = 1.9f;
static float  g_dlg_yscale  = 1.9f;
static float  g_dlg_xtrans  = 0.0f;
static float  g_dlg_ytrans  = 0.0f;
HWND   g_dlg_parent  = NULL;

static inline int scx(int x) { return (int)(x * g_dlg_xscale + g_dlg_xtrans + 0.5f); }
static inline int scy(int y) { return (int)(y * g_dlg_yscale + g_dlg_ytrans + 0.5f); }
static inline int scw(int w) { return (int)(w * g_dlg_xscale + 0.5f); }
static inline int sch(int h) { return (int)(h * g_dlg_yscale + 0.5f); }

static inline void clamp_combo_closed_height(RECT *r)
{
  const int maxh = SWELL_UI_SCALE(20);
  if (r && r->bottom > r->top + maxh) r->bottom = r->top + maxh;
}

void SWELL_MakeSetCurParms(float xscale, float yscale, float xtrans, float ytrans,
                           HWND parent, bool doauto, bool dosizetofit)
{
  if (xscale == 0.0f) xscale = 1.9f;
  if (yscale == 0.0f) yscale = 1.9f;

  if (g_swell_ui_scale != 256 && xscale != 1.0f && yscale != 1.0f)
  {
    const float m = g_swell_ui_scale / 256.0f;
    xscale *= m;
    yscale *= m;
  }

  g_dlg_xscale = xscale;
  g_dlg_yscale = yscale;
  g_dlg_xtrans = xtrans;
  g_dlg_ytrans = ytrans;
  g_dlg_parent = parent;
  (void)doauto; (void)dosizetofit;
}

// ---------------------------------------------------------------------------
// Spare OS window pool (kept between EndDialog calls to reduce flicker)
// ---------------------------------------------------------------------------

static HWND g_spare_oswindow_hwnd = NULL;

void swell_dlg_destroyspare()
{
  if (g_spare_oswindow_hwnd) {
    swell_oswindow_destroy(g_spare_oswindow_hwnd);
    g_spare_oswindow_hwnd = NULL;
  }
}

// ---------------------------------------------------------------------------
// Modal dialog state
// ---------------------------------------------------------------------------

struct ModalDlgState {
  HWND   hwnd;
  bool   has_ret;
  int    ret;
  ModalDlgState *prev;
};

static ModalDlgState *g_modal_stack = NULL;
static int            s_last_dlgret = -1;

// ---------------------------------------------------------------------------
// Helper: find first focusable child
// ---------------------------------------------------------------------------

static HWND find_first_focusable(HWND parent)
{
  if (!parent) return NULL;
  int n = parent->m_children.GetSize();
  for (int i = 0; i < n; i++) {
    HWND ch = parent->m_children.Get(i);
    if (ch && ch->m_visible && ch->m_enabled && ch->m_wantfocus)
      return ch;
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// SWELL_MakeButton
// ---------------------------------------------------------------------------

HWND SWELL_MakeButton(int def, const char *label, int idx,
                      int x, int y, int w, int h, int flags)
{
  DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
  style |= def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;
  if (flags) style |= (DWORD)flags;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, label ? label : "", true,
                         buttonWindowProc);
  hwnd->m_style     = style;
  hwnd->m_classname = "Button";
  buttonWindowProc(hwnd, WM_CREATE, 0, 0);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeEditField
// ---------------------------------------------------------------------------

HWND SWELL_MakeEditField(int idx, int x, int y, int w, int h, int flags)
{
  DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
  if (flags) style |= (DWORD)flags;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, "", true, editWindowProc);
  hwnd->m_style     = style;
  hwnd->m_classname = "Edit";
  editWindowProc(hwnd, WM_CREATE, 0, 0);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeLabel
// ---------------------------------------------------------------------------

HWND SWELL_MakeLabel(int align, const char *label, int idx,
                     int x, int y, int w, int h, int flags)
{
  DWORD style = WS_CHILD | WS_VISIBLE;
  if (align < 0)      style |= SS_LEFT;
  else if (align > 0) style |= SS_RIGHT;
  else                style |= SS_CENTER;
  if (flags) style |= (DWORD)flags;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, label ? label : "", true,
                         labelWindowProc);
  hwnd->m_style     = style;
  hwnd->m_classname = "Static";
  hwnd->m_wantfocus = false;
  labelWindowProc(hwnd, WM_CREATE, 0, 0);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeGroupBox
// ---------------------------------------------------------------------------

HWND SWELL_MakeGroupBox(const char *name, int idx,
                        int x, int y, int w, int h, int style)
{
  DWORD wstyle = WS_CHILD | WS_VISIBLE | BS_GROUPBOX;
  if (style) wstyle |= (DWORD)style;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, name ? name : "", true,
                         buttonWindowProc);
  hwnd->m_style     = wstyle;
  hwnd->m_classname = "Button";
  hwnd->m_wantfocus = false;
  buttonWindowProc(hwnd, WM_CREATE, 0, 0);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeCheckBox
// ---------------------------------------------------------------------------

HWND SWELL_MakeCheckBox(const char *name, int idx,
                        int x, int y, int w, int h, int flags)
{
  DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
  if (flags) style |= (DWORD)flags;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, name ? name : "", true,
                         buttonWindowProc);
  hwnd->m_style     = style;
  hwnd->m_classname = "Button";
  buttonWindowProc(hwnd, WM_CREATE, 0, 0);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeCombo
// ---------------------------------------------------------------------------

HWND SWELL_MakeCombo(int idx, int x, int y, int w, int h, int flags)
{
  DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
  if (flags) style |= (DWORD)flags;
  else       style |= CBS_DROPDOWNLIST;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  clamp_combo_closed_height(&r);
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, "", true, comboWindowProc);
  hwnd->m_style     = style;
  hwnd->m_classname = "ComboBox";
  comboWindowProc(hwnd, WM_CREATE, 0, 0);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeListBox
// ---------------------------------------------------------------------------

HWND SWELL_MakeListBox(int idx, int x, int y, int w, int h, int styles)
{
  DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER;
  if (styles) style |= (DWORD)styles;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, "", true, listViewWindowProc);
  hwnd->m_style     = style;
  hwnd->m_classname = "ListBox";
  // pass is_listbox=1 via lParam to WM_CREATE
  listViewWindowProc(hwnd, WM_CREATE, 0, 1);
  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_MakeControl — dispatch based on classname
// ---------------------------------------------------------------------------

HWND SWELL_MakeControl(const char *cname, int idx, const char *classname,
                       int style, int x, int y, int w, int h, int exstyle)
{
  if (!classname) return NULL;

  RECT r = { scx(x), scy(y), scx(x)+scw(w), scy(y)+sch(h) };
  DWORD wstyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (DWORD)style;

  WNDPROC proc = NULL;
  const char *klass = classname;
  bool is_listbox = false;
  bool no_focus = false;

  if (!strcasecmp(classname, "SysListView32")) {
    proc = listViewWindowProc;
    klass = "SysListView32";
  } else if (!strcasecmp(classname, "SysTreeView32")) {
    proc = treeViewWindowProc;
    klass = "SysTreeView32";
  } else if (!strcasecmp(classname, "SysTabControl32")) {
    proc = tabControlWindowProc;
    klass = "SysTabControl32";
    no_focus = true;
  } else if (!strcasecmp(classname, "msctls_trackbar32")) {
    proc = trackbarWindowProc;
    klass = "msctls_trackbar32";
  } else if (!strcasecmp(classname, "msctls_progress32")) {
    proc = progressWindowProc;
    klass = "msctls_progress32";
    no_focus = true;
  } else if (!strcasecmp(classname, "Button")) {
    proc = buttonWindowProc;
    klass = "Button";
  } else if (!strcasecmp(classname, "Edit")) {
    proc = editWindowProc;
    klass = "Edit";
  } else if (!strcasecmp(classname, "Static")) {
    proc = labelWindowProc;
    klass = "Static";
    no_focus = true;
  } else if (!strcasecmp(classname, "ComboBox")) {
    proc = comboWindowProc;
    klass = "ComboBox";
    clamp_combo_closed_height(&r);
  } else if (!strcasecmp(classname, "ListBox")) {
    proc = listViewWindowProc;
    klass = "ListBox";
    is_listbox = true;
  } else if (!strcasecmp(classname, "__SWELL_ICON")) {
    proc = labelWindowProc;
    klass = "Static";
    no_focus = true;
  }

  if (!proc) {
    int px = scx(x), py = scy(y), pw = scw(w), ph = sch(h);
    extern HWND swell_invoke_control_creators(HWND parent, const char *cname, int idx,
                                              const char *classname, int style,
                                              int x, int y, int w, int h);
    return swell_invoke_control_creators(g_dlg_parent, cname, idx, classname,
                                         style, px, py, pw, ph);
  }

  HWND hwnd = new HWND__(g_dlg_parent, idx, &r, cname ? cname : "", true, proc);
  hwnd->m_style     = wstyle;
  hwnd->m_exstyle   = (DWORD)exstyle;
  hwnd->m_classname = klass;

  if (no_focus) hwnd->m_wantfocus = false;

  proc(hwnd, WM_CREATE, 0, is_listbox ? 1 : 0);

  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_GenerateDialogFromList
// ---------------------------------------------------------------------------

void SWELL_GenerateDialogFromList(const void *list, int listsz)
{
  if (!list || listsz <= 0 || !g_dlg_parent) return;

  const SWELL_DlgResourceEntry *ents = (const SWELL_DlgResourceEntry *)list;

  for (int i = 0; i < listsz; i++) {
    const SWELL_DlgResourceEntry *e = &ents[i];
    if (!e->str1) continue;

    if (!strcmp(e->str1, "__SWELL_BUTTON")) {
      SWELL_MakeButton(e->flag1, e->str2, e->p1,
                       e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_EDIT")) {
      SWELL_MakeEditField(e->p1, e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_LABEL")) {
      SWELL_MakeLabel(e->flag1, e->str2, e->p1,
                      e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_COMBO")) {
      SWELL_MakeCombo(e->p1, e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_GROUP")) {
      SWELL_MakeGroupBox(e->str2, e->p1, e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_CHECKBOX")) {
      SWELL_MakeCheckBox(e->str2, e->p1, e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_LISTBOX")) {
      SWELL_MakeListBox(e->p1, e->p2, e->p3, e->p4, e->p5, e->p6);
    } else if (!strcmp(e->str1, "__SWELL_ICON")) {
      // str2 is an icon resource ID cast to (const char*) via dlggen.h ICON macro
      // — not a string pointer. Pass NULL for cname to avoid strlen crash.
      SWELL_MakeControl(NULL, -1, "__SWELL_ICON",
                        e->p5, e->p1, e->p2, e->p3, e->p4, e->p6);
    } else {
      // CONTROL entry: str1=cname, flag1=idx, str2=classname, p1=style, p2..p6=x,y,w,h,exstyle
      SWELL_MakeControl(e->str1, e->flag1, e->str2,
                        e->p1, e->p2, e->p3, e->p4, e->p5, e->p6);
    }
  }
}

// ---------------------------------------------------------------------------
// SWELL_CreateDialog
// ---------------------------------------------------------------------------

HWND SWELL_CreateDialog(struct SWELL_DialogResourceIndex *reshead,
                        const char *resid,
                        HWND parent,
                        DLGPROC dlgproc,
                        LPARAM param)
{

  // resid==NULL + parent means bare WNDPROC (opaque child window)
  bool bare_wndproc = (resid == NULL);

  SWELL_DialogResourceIndex *res = NULL;
  if (resid) {
    // resid may be MAKEINTRESOURCE(n) = (const char*)n where n < 0x10000
    bool resid_is_int = ((size_t)resid <= 0xFFFF);
    for (SWELL_DialogResourceIndex *r = reshead; r; r = r->_next) {
      bool match = (r->resid == resid);
      if (!match && !resid_is_int && r->resid && (size_t)r->resid > 0xFFFF)
        match = !strcmp(r->resid, resid);
      if (match) { res = r; break; }
    }
    if (!res) {
      if (resid_is_int)
        fprintf(stderr, "SWELL_CreateDialog: resource #%d not found\n", (int)(size_t)resid);
      else
        fprintf(stderr, "SWELL_CreateDialog: resource '%s' not found\n", resid);
      return NULL;
    }
  }

  int wflags = res ? res->windowTypeFlags : 0;

  // Matching original SWELL:
  //   bare WNDPROC with parent → child window (opaque child, spec)
  //   resource with SWELL_DLG_WS_CHILD → child window
  //   resource with parent but no WS_CHILD → top-level owned window
  //     (parent converted to owner, window gets own OS window)
  bool is_child = bare_wndproc ? (parent != NULL) : ((wflags & SWELL_DLG_WS_CHILD) != 0);
  HWND__ *owner = NULL;
  HWND__ *hwnd_parent = parent;

  if (!is_child && parent) {
    owner       = parent;
    hwnd_parent = NULL;   // top-level window — gets own OS window
  }

  DWORD style = WS_CLIPCHILDREN;
  DWORD exstyle = 0;

  if (!hwnd_parent) {
    // top-level (may be owned): always get caption+sysmenu
    style |= WS_CAPTION | WS_SYSMENU;
    if (wflags & SWELL_DLG_WS_RESIZABLE) style |= WS_THICKFRAME;
    if (wflags & SWELL_DLG_WS_DROPTARGET) exstyle |= WS_EX_ACCEPTFILES;
  } else {
    style |= WS_CHILD;
  }

  style |= wflags & WS_CLIPSIBLINGS;
  // Scale to physical device pixels (matching original SWELL dlg-generic.cpp:295).
  // SDL3 backend does NOT use HIGH_PIXEL_DENSITY — SWELL owns DPI scaling itself.
  int dlg_w = SWELL_UI_SCALE(res ? res->width  : 400);
  int dlg_h = SWELL_UI_SCALE(res ? res->height : 300);

  // Center on screen for top-level and owned windows
  int dlg_x = 0, dlg_y = 0;
  if (!hwnd_parent) {
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    dlg_x = (sx - dlg_w) / 2;
    dlg_y = (sy - dlg_h) / 2;
    if (dlg_x < 10) dlg_x = 10;
    if (dlg_y < 10) dlg_y = 10;
  }

  RECT r = { dlg_x, dlg_y, dlg_x + dlg_w, dlg_y + dlg_h };

  WNDPROC wproc = bare_wndproc ? (WNDPROC)dlgproc : SwellDialogDefaultWindowProc;

  // Create HWND invisible (matching original). ShowWindow will set visible
  // and trigger swell_oswindow_manage for top-level windows.
  HWND hwnd = new HWND__(hwnd_parent, 0, &r,
                         res ? (res->title ? res->title : "") : "",
                         false,  // not visible yet — ShowWindow handles this
                         wproc);
  hwnd->m_style   = style;
  hwnd->m_exstyle = exstyle;

  if (owner) {
    hwnd->m_owner = owner;
    owner->m_owned.Add(hwnd);
  }

  if (bare_wndproc) {
    // fire WM_CREATE instead of WM_INITDIALOG
    hwnd->m_wndproc(hwnd, WM_CREATE, 0, param);
    return (hwnd->m_hashaddestroy >= 2) ? NULL : hwnd;
  }

  hwnd->m_dlgproc = dlgproc;

  // Create controls
  if (res && res->createFunc) {
    HWND saved_parent = g_dlg_parent;
    g_dlg_parent = hwnd;
    res->createFunc(hwnd, wflags);
    g_dlg_parent = saved_parent;
  }

  // Find first focusable child
  HWND firstFocus = find_first_focusable(hwnd);

  // Fire WM_INITDIALOG — retain firstFocus around dlgproc
  // since the callback may destroy it
  INT_PTR initret = 0;
  if (firstFocus) firstFocus->Retain();
  if (dlgproc) {
    initret = dlgproc(hwnd, WM_INITDIALOG, (WPARAM)firstFocus, param);
  }

  if (hwnd->m_hashaddestroy >= 2) {
    if (firstFocus) firstFocus->Release();
    return NULL;
  }

  if (initret && firstFocus && firstFocus->m_hashaddestroy < 2 &&
      firstFocus->m_wantfocus && firstFocus->m_visible && firstFocus->m_enabled) {
    SetFocus(firstFocus);
  }
  if (firstFocus) firstFocus->Release();

  return hwnd;
}

// ---------------------------------------------------------------------------
// SWELL_DialogBox  (modal)
// ---------------------------------------------------------------------------

int SWELL_DialogBox(struct SWELL_DialogResourceIndex *reshead,
                    const char *resid,
                    HWND parent,
                    DLGPROC dlgproc,
                    LPARAM param)
{
  // Reject child-flagged resources for modal dialogs (matching original)
  if (resid) {
    bool resid_is_int = ((size_t)resid <= 0xFFFF);
    SWELL_DialogResourceIndex *r = NULL;
    for (SWELL_DialogResourceIndex *p = reshead; p; p = p->_next) {
      bool match = (p->resid == resid);
      if (!match && !resid_is_int && p->resid && (size_t)p->resid > 0xFFFF)
        match = !strcmp(p->resid, resid);
      if (match) { r = p; break; }
    }
    if (!r || (r->windowTypeFlags & SWELL_DLG_WS_CHILD)) return -1;
  }
  else if (parent)
  {
    resid = (const char *)(INT_PTR)(0x400002); // force non-child, force no minimize box
  }

  HWND dlg = SWELL_CreateDialog(reshead, resid, parent, dlgproc, param);
  if (!dlg) return s_last_dlgret;

  dlg->Retain(); // keep alive past EndDialog → DestroyWindow

  ReleaseCapture(); // force end of any captures

  // Push modal state
  ModalDlgState ms;
  ms.hwnd    = dlg;
  ms.has_ret = false;
  ms.ret     = -1;
  ms.prev    = g_modal_stack;
  g_modal_stack = &ms;

  // Disable other top-level windows
  WDL_PtrList<HWND__> disabled_list;
  for (HWND w = g_swell_top_level_list; w; w = w->m_next) {
    if (w != dlg && w->m_enabled && !w->m_parent) {
      EnableWindow(w, FALSE);
      disabled_list.Add(w);
    }
  }

  // Show the dialog — ShowWindow calls swell_oswindow_manage for top-level
  // windows that don't have an OS window yet (matching original).
  ShowWindow(dlg, SW_SHOW);

  // Modal loop
  while (!ms.has_ret && dlg->m_hashaddestroy < 2) {
    SWELL_RunMessageLoop();
    usleep(10000); // 10ms
  }

  // Re-enable disabled windows
  for (int i = 0; i < disabled_list.GetSize(); i++) {
    HWND w = disabled_list.Get(i);
    if (w && w->m_hashaddestroy < 2) {
      EnableWindow(w, TRUE);
    }
  }

  int ret = ms.ret;
  g_modal_stack = ms.prev;

  dlg->Release();
  return ret;
}

// ---------------------------------------------------------------------------
// EndDialog
// ---------------------------------------------------------------------------

void EndDialog(HWND hwnd, int result)
{
  if (!hwnd) return;

  bool found = false;
  for (ModalDlgState *ms = g_modal_stack; ms; ms = ms->prev) {
    if (ms->hwnd == hwnd) {
      ms->has_ret = true;
      ms->ret = result;
      found = true;
      break;
    }
  }

  if (!found) s_last_dlgret = result;

  DestroyWindow(hwnd);
}

bool IsModalDialogBox(HWND hwnd)
{
  if (!hwnd) return false;
  for (const ModalDlgState *ms = g_modal_stack; ms; ms = ms->prev) {
    if (ms->hwnd == hwnd) return true;
  }
  return false;
}

void SWELL_CloseWindow(HWND hwnd)
{
  DestroyWindow(hwnd);
}

// ---------------------------------------------------------------------------
// SWELL_ModalWindowStart / Run / End
// ---------------------------------------------------------------------------

void *SWELL_ModalWindowStart(HWND hwnd)
{
  if (!hwnd) return NULL;

  ModalDlgState *ms = new ModalDlgState();
  ms->hwnd    = hwnd;
  ms->has_ret = false;
  ms->ret     = -1;
  ms->prev    = g_modal_stack;
  g_modal_stack = ms;

  return (void *)ms;
}

bool SWELL_ModalWindowRun(void *ctx, int *ret)
{
  if (!ctx) return false;

  ModalDlgState *ms = (ModalDlgState *)ctx;

  if (ms->has_ret || ms->hwnd->m_hashaddestroy >= 2) {
    if (ret) *ret = ms->ret;
    return false;
  }

  SWELL_RunMessageLoop();
  return true;
}

void SWELL_ModalWindowEnd(void *ctx)
{
  if (!ctx) return;

  ModalDlgState *ms = (ModalDlgState *)ctx;

  if (g_modal_stack == ms) {
    g_modal_stack = ms->prev;
  } else {
    for (ModalDlgState *cur = g_modal_stack; cur; cur = cur->prev) {
      if (cur->prev == ms) { cur->prev = ms->prev; break; }
    }
  }

  delete ms;
}
