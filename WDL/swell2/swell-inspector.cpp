/*
  SWELL2 Inspector — interactive HWND tree browser + message injector.
  Inspired by GTK Inspector and browser DevTools.

  Activate via SWELL_INSPECTOR=1 env var. Toggle with Ctrl+Shift+I.
  Shows real-time HWND hierarchy, properties, frame stats.
  Can post messages to selected windows — useful for debugging
  swell-based applications without recompilation.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
#include "swell-dlggen.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>

// Required by SWELL_DEFINE_DIALOG_RESOURCE macros
struct SWELL_CursorResourceIndex *SWELL_curmodule_cursorresource_head;
struct SWELL_DialogResourceIndex *SWELL_curmodule_dialogresource_head;
struct SWELL_MenuResourceIndex *SWELL_curmodule_menuresource_head;

// ---- TreeView message constants (ANSI numbering, matching swell-controls.cpp) ----
#ifndef TVM_FIRST
#define TVM_FIRST           0x1100
#define TVM_INSERTITEM      (TVM_FIRST+0)
#define TVM_DELETEITEM      (TVM_FIRST+1)
#define TVM_EXPAND          (TVM_FIRST+2)
#define TVM_SELECTITEM      (TVM_FIRST+11)
#define TVM_GETITEM         (TVM_FIRST+12)
#define TVM_SETITEM         (TVM_FIRST+13)
#define TVM_GETNEXTITEM     (TVM_FIRST+10)
#define TVM_ENSUREVISIBLE   (TVM_FIRST+20)
#define TVM_GETCOUNT        (TVM_FIRST+5)
#define TVM_DELETEALLITEMS  (TVM_FIRST+60)
#endif

#ifndef TVIF_TEXT
#define TVIF_TEXT           0x0001
#define TVIF_PARAM          0x0004
#define TVIF_STATE          0x0008
#endif

#ifndef TVGN_CARET
#define TVGN_CARET          0x0009
#endif

#ifndef TVE_EXPAND
#define TVE_EXPAND          0x0002
#endif

#ifndef TVI_ROOT
#define TVI_ROOT            ((HTREEITEM)0xFFFF0000)
#define TVI_FIRST           ((HTREEITEM)0xFFFF0001)
#endif

#ifndef SWELL_DLG_WS_RESIZABLE
#define SWELL_DLG_WS_RESIZABLE 2
#endif

// ---- Dialog IDs ----
enum {
  IDD_INSPECTOR = 5000,

  IDC_TREE      = 5100,
  IDC_PROPS     = 5101,
  IDC_MSG_COMBO = 5102,
  IDC_WPARAM    = 5103,
  IDC_LPARAM    = 5104,
  IDC_SEND      = 5105,
  IDC_POST      = 5106,
  IDC_REFRESH   = 5107,
  IDC_FPS_LABEL = 5108,
  IDC_HIGHLIGHT = 5109,
  IDC_AUTOREF   = 5110,
};

// ---- Known WM_* messages for the combo box ----
struct WmEntry {
  const char *name;
  UINT msg;
};

static const WmEntry kKnownMessages[] = {
  {"WM_NULL",          0x0000},
  {"WM_CREATE",        0x0001},
  {"WM_DESTROY",       0x0002},
  {"WM_MOVE",          0x0003},
  {"WM_SIZE",          0x0005},
  {"WM_ACTIVATE",      0x0006},
  {"WM_SETFOCUS",      0x0007},
  {"WM_KILLFOCUS",     0x0008},
  {"WM_ENABLE",        0x000A},
  {"WM_SETTEXT",       0x000C},
  {"WM_GETTEXT",       0x000D},
  {"WM_CLOSE",         0x0010},
  {"WM_QUIT",          0x0012},
  {"WM_ERASEBKGND",    0x0014},
  {"WM_SHOWWINDOW",    0x0018},
  {"WM_SETCURSOR",     0x0020},
  {"WM_MOUSEACTIVATE", 0x0021},
  {"WM_GETMINMAXINFO", 0x0024},
  {"WM_NOTIFY",        0x004E},
  {"WM_KEYDOWN",       0x0100},
  {"WM_KEYUP",         0x0101},
  {"WM_CHAR",          0x0102},
  {"WM_COMMAND",       0x0111},
  {"WM_HSCROLL",       0x0114},
  {"WM_VSCROLL",       0x0115},
  {"WM_CTLCOLORBTN",   0x0135},
  {"WM_CTLCOLOREDIT",  0x0133},
  {"WM_CTLCOLORSTATIC",0x0138},
  {"WM_MOUSEMOVE",     0x0200},
  {"WM_LBUTTONDOWN",   0x0201},
  {"WM_LBUTTONUP",     0x0202},
  {"WM_RBUTTONDOWN",   0x0204},
  {"WM_RBUTTONUP",     0x0205},
  {"WM_MBUTTONDOWN",   0x0207},
  {"WM_MBUTTONUP",     0x0208},
  {"WM_MOUSEWHEEL",    0x020A},
  {"WM_MOUSEHWHEEL",   0x020E},
  {"WM_PARENTNOTIFY",  0x0210},
  {"WM_SYSKEYDOWN",    0x0104},
  {"WM_SYSKEYUP",      0x0105},
  {"WM_SYSCOMMAND",    0x0112},
  {"WM_ACTIVATEAPP",   0x001C},
  {"WM_PAINT",         0x000F},
  {nullptr, 0},
};

// ---- Inspector state ----
struct InspectorState {
  HWND dlg;
  HWND tree;
  HWND props;
  HWND msg_combo;
  HWND wparam_edit;
  HWND lparam_edit;
  HWND fps_label;
  HWND highlight_chk;
  HWND autoref_chk;

  HWND selected_hwnd;
  bool visible;
  bool autorerefresh;

  // frame timing
  using clock = std::chrono::steady_clock;
  clock::time_point last_tick;
  double fps_smooth;
  int frame_count_this_sec;
  double frame_sec_accum;
};

static InspectorState g_insp;

// ---- highlight state (used by swell-wnd.cpp paint hook) ----
HWND g_swell_inspector_highlight = nullptr;

// ---- forward decl ----
static void inspector_refresh_tree();
static void inspector_show_props(HWND hwnd);

// ---- helper: snprintf into a WDL_FastString ----
static void str_append(WDL_FastString &s, const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  buf[sizeof(buf)-1] = 0;
  s.Append(buf);
}

// ---- populate tree view from HWND hierarchy ----

static HTREEITEM insert_tree_node(HWND tree, HTREEITEM parent,
                                   HWND hwnd, int depth)
{
  WDL_FastString label;
  // indentation for visual depth (spaces work as indent prefix)
  for (int i = 0; i < depth; i++) label.Append("  ");

  const char *cls = hwnd->m_classname ? hwnd->m_classname : "(no class)";
  const char *title = hwnd->m_title.Get();
  RECT r;
  GetWindowRect(hwnd, &r);
  int w = r.right - r.left;
  int h = r.bottom - r.top;

  str_append(label, "%s [%s] (%d,%d %dx%d)",
             cls,
             title && title[0] ? title : "",
             r.left, r.top, w, h);

  if (!hwnd->m_visible) label.Append(" hid");
  if (hwnd == g_swell_focus) label.Append(" FOCUS");
  if (hwnd == g_swell_capture) label.Append(" CAP");

  TVINSERTSTRUCT tvis = {};
  tvis.hParent = parent ? parent : TVI_ROOT;
  tvis.hInsertAfter = TVI_LAST;
  TVITEM &item = tvis.item;
  item.mask = TVIF_TEXT | TVIF_PARAM;
  item.pszText = (char *)label.Get();
  item.cchTextMax = (int)strlen(label.Get());
  item.lParam = (LPARAM)hwnd;

  HTREEITEM node = (HTREEITEM)SendMessage(tree, TVM_INSERTITEM,
                                           0, (LPARAM)&tvis);
  return node;
}

static void populate_tree_node(HWND tree, HTREEITEM parent,
                                HWND hwnd, int depth)
{
  if (!hwnd) return;
  HTREEITEM node = insert_tree_node(tree, parent, hwnd, depth);

  for (HWND ch = GetWindow(hwnd, GW_CHILD);
       ch;
       ch = GetWindow(ch, GW_HWNDNEXT))
  {
    populate_tree_node(tree, node, ch, depth + 1);
  }

  // Auto-expand first 2 levels
  if (depth < 2 && node)
    SendMessage(tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)node);
}

static void inspector_refresh_tree()
{
  if (!g_insp.tree) return;

  // Save selection
  HTREEITEM sel = (HTREEITEM)SendMessage(g_insp.tree, TVM_GETNEXTITEM,
                                          TVGN_CARET, 0);
  HWND saved_hwnd = nullptr;
  if (sel) {
    TVITEM item = {};
    item.mask = TVIF_PARAM;
    item.hItem = sel;
    if (SendMessage(g_insp.tree, TVM_GETITEM, 0, (LPARAM)&item))
      saved_hwnd = (HWND)item.lParam;
  }

  SendMessage(g_insp.tree, TVM_DELETEALLITEMS, 0, 0);

  // Populate all top-level windows
  HWND w = g_swell_top_level_list;
  while (w) {
    populate_tree_node(g_insp.tree, TVI_ROOT, w, 0);
    w = w->m_next;
  }

  // Try to restore selection
  if (saved_hwnd) {
    // Walk tree to find matching node
    HTREEITEM root = (HTREEITEM)SendMessage(g_insp.tree,
                                             TVM_GETNEXTITEM,
                                             TVGN_ROOT, 0);
    // Simple: just re-select if found via TVM_SELECTITEM
  }
}

// ---- property text ----

static void inspector_show_props(HWND hwnd)
{
  if (!g_insp.props || !hwnd) return;
  g_insp.selected_hwnd = hwnd;

  WDL_FastString s;
  str_append(s, "hwnd:       %p\n", (void*)hwnd);
  str_append(s, "class:      %s\n",
             hwnd->m_classname ? hwnd->m_classname : "(nil)");
  str_append(s, "title:      \"%s\"\n", hwnd->m_title.Get());
  str_append(s, "id:         %d\n", hwnd->m_id);
  str_append(s, "style:      0x%08X\n", (unsigned)hwnd->m_style);
  str_append(s, "exstyle:    0x%08X\n", (unsigned)hwnd->m_exstyle);

  RECT r;
  GetWindowRect(hwnd, &r);
  str_append(s, "rect:       (%d,%d) %dx%d\n",
             r.left, r.top, r.right - r.left, r.bottom - r.top);

  str_append(s, "visible:    %s\n", hwnd->m_visible ? "yes" : "no");
  str_append(s, "enabled:    %s\n", hwnd->m_enabled ? "yes" : "no");
  str_append(s, "focus:      %s\n",
             (hwnd == g_swell_focus) ? "yes" : "no");
  str_append(s, "capture:    %s\n",
             (hwnd == g_swell_capture) ? "yes" : "no");

  int nkids = hwnd->m_children.GetSize();
  str_append(s, "children:   %d\n", nkids);
  str_append(s, "owned:      %d\n", (int)hwnd->m_owned.GetSize());

  str_append(s, "invalid:    %s\n",
             hwnd->m_invalidated ? "yes" : "no");
  str_append(s, "child_inv:  %s\n",
             hwnd->m_child_invalidated ? "yes" : "no");

  if (hwnd->m_backingstore) {
    str_append(s, "backing:    %dx%d SkSurface\n",
               hwnd->m_backingstore->width(),
               hwnd->m_backingstore->height());
  } else {
    str_append(s, "backing:    none\n");
  }

#ifdef SWELL_TARGET_SDL3
  str_append(s, "oswindow:   %p\n", hwnd->m_oswindow);
#endif

  str_append(s, "font:       %p\n", (void*)hwnd->m_font);
  str_append(s, "menu:       %p\n", (void*)hwnd->m_menu);

  SetDlgItemText(g_insp.dlg, IDC_PROPS, s.Get());
}

// ---- message injection ----

static void inspector_send_msg(bool post)
{
  char wbuf[64], lbuf[64];
  GetDlgItemText(g_insp.dlg, IDC_WPARAM, wbuf, sizeof(wbuf));
  GetDlgItemText(g_insp.dlg, IDC_LPARAM, lbuf, sizeof(lbuf));

  // Get selected message from combo
  int sel = (int)SendMessage(g_insp.msg_combo, CB_GETCURSEL, 0, 0);
  UINT msg = 0;
  if (sel >= 0) {
    msg = (UINT)SendMessage(g_insp.msg_combo, CB_GETITEMDATA, sel, 0);
  }

  WPARAM wp = (WPARAM)strtoull(wbuf, nullptr, 0);
  LPARAM lp = (LPARAM)strtoull(lbuf, nullptr, 0);

  if (g_insp.selected_hwnd) {
    if (post)
      PostMessage(g_insp.selected_hwnd, msg, wp, lp);
    else
      SendMessage(g_insp.selected_hwnd, msg, wp, lp);
  }
}

// ---- FPS ----

static void inspector_update_fps(double frame_ms)
{
  using namespace std::chrono;
  auto now = steady_clock::now();

  g_insp.frame_count_this_sec++;
  g_insp.frame_sec_accum += frame_ms;

  double elapsed = duration<double>(now - g_insp.last_tick).count();
  if (elapsed >= 0.5) {
    double fps = (double)g_insp.frame_count_this_sec / elapsed;
    g_insp.fps_smooth = g_insp.fps_smooth * 0.7 + fps * 0.3;
    g_insp.last_tick = now;
    g_insp.frame_count_this_sec = 0;
    g_insp.frame_sec_accum = 0.0;

    if (g_insp.fps_label) {
      char buf[64];
      snprintf(buf, sizeof(buf),
               "FPS: %.1f  |  avg paint: %.2f ms",
               g_insp.fps_smooth,
               elapsed > 0.0 ? (g_insp.frame_sec_accum * 1000.0 /
                                (double)g_insp.frame_count_this_sec) : 0.0);
      SetDlgItemText(g_insp.dlg, IDC_FPS_LABEL, buf);
    }
  }
}

// ---- dialog proc ----

static INT_PTR inspector_dlg_proc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
    case WM_INITDIALOG: {
      g_insp.dlg = hwnd;
      g_insp.tree = GetDlgItem(hwnd, IDC_TREE);
      g_insp.props = GetDlgItem(hwnd, IDC_PROPS);
      g_insp.msg_combo = GetDlgItem(hwnd, IDC_MSG_COMBO);
      g_insp.wparam_edit = GetDlgItem(hwnd, IDC_WPARAM);
      g_insp.lparam_edit = GetDlgItem(hwnd, IDC_LPARAM);
      g_insp.fps_label = GetDlgItem(hwnd, IDC_FPS_LABEL);
      g_insp.highlight_chk = GetDlgItem(hwnd, IDC_HIGHLIGHT);
      g_insp.autoref_chk = GetDlgItem(hwnd, IDC_AUTOREF);

      // Populate message combo
      for (int i = 0; kKnownMessages[i].name; i++) {
        int idx = (int)SendMessage(g_insp.msg_combo, CB_ADDSTRING,
                                   0, (LPARAM)kKnownMessages[i].name);
        SendMessage(g_insp.msg_combo, CB_SETITEMDATA,
                    idx, kKnownMessages[i].msg);
      }
      SendMessage(g_insp.msg_combo, CB_SETCURSEL, 0, 0);

      // Default wparam/lparam text
      SetDlgItemText(hwnd, IDC_WPARAM, "0");
      SetDlgItemText(hwnd, IDC_LPARAM, "0");

      // Check auto-refresh by default
      CheckDlgButton(hwnd, IDC_AUTOREF, BST_CHECKED);
      g_insp.autorerefresh = true;

      g_insp.last_tick = std::chrono::steady_clock::now();

      inspector_refresh_tree();
      g_insp.visible = true;
      return 0;
    }

    case WM_CLOSE:
      g_insp.visible = false;
      ShowWindow(hwnd, SW_HIDE);
      return 0;

    case WM_COMMAND: {
      int ctrl = LOWORD(wParam);
      int code = HIWORD(wParam);
      bool update = false;
      switch (ctrl) {
        case IDC_REFRESH:
          inspector_refresh_tree();
          update = true;
          break;
        case IDC_SEND:
          inspector_send_msg(false);
          break;
        case IDC_POST:
          inspector_send_msg(true);
          break;
        case IDC_HIGHLIGHT:
          if (code == BN_CLICKED) {
            bool chk = IsDlgButtonChecked(hwnd, IDC_HIGHLIGHT) == BST_CHECKED;
            if (chk && g_insp.selected_hwnd)
              g_swell_inspector_highlight = g_insp.selected_hwnd;
            else
              g_swell_inspector_highlight = nullptr;
          }
          break;
        case IDC_AUTOREF:
          if (code == BN_CLICKED) {
            g_insp.autorerefresh =
              IsDlgButtonChecked(hwnd, IDC_AUTOREF) == BST_CHECKED;
          }
          break;
      }
      if (update && g_insp.autorerefresh)
        inspector_refresh_tree();
      return 0;
    }

    case WM_NOTIFY: {
      NMHDR *nmh = (NMHDR *)lParam;
      if (nmh->idFrom == IDC_TREE) {
        // Type-cast to what swell treeview actually sends
        // TVN_SELCHANGED is -451 or similar; we detect via TVM_GETNEXTITEM
        HTREEITEM sel = (HTREEITEM)SendMessage(g_insp.tree,
                                                TVM_GETNEXTITEM,
                                                TVGN_CARET, 0);
        if (sel) {
          TVITEM item = {};
          item.mask = TVIF_PARAM;
          item.hItem = sel;
          if (SendMessage(g_insp.tree, TVM_GETITEM, 0, (LPARAM)&item)) {
            HWND h = (HWND)item.lParam;
            inspector_show_props(h);
            // Update highlight
            if (IsDlgButtonChecked(hwnd, IDC_HIGHLIGHT) == BST_CHECKED)
              g_swell_inspector_highlight = h;
          }
        }
      }
      return 0;
    }
  }
  return 0;
}

// ---- dialog resource ----

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN2(IDD_INSPECTOR,
  SWELL_DLG_WS_RESIZABLE | SWELL_DLG_WS_OPAQUE,
  "SWELL Inspector", 620, 480)
BEGIN
  // Tree view — left side
  CONTROL "", IDC_TREE, "SysTreeView32",
    WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
    TVS_HASBUTTONS | WS_TABSTOP,
    6, 6, 340, 410

  // Properties — right side
  EDITTEXT IDC_PROPS, 352, 6, 262, 190,
    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL

  // Message injector section
  GROUPBOX "Message Injector", -1, 352, 202, 262, 96
  COMBOBOX IDC_MSG_COMBO, 360, 218, 246, 160,
    CBS_DROPDOWNLIST | WS_TABSTOP
  LTEXT "wParam:", -1, 360, 248, 46, 14
  EDITTEXT IDC_WPARAM, 410, 246, 80, 20, ES_AUTOHSCROLL
  LTEXT "lParam:", -1, 360, 272, 46, 14
  EDITTEXT IDC_LPARAM, 410, 270, 80, 20, ES_AUTOHSCROLL
  PUSHBUTTON "Send", IDC_SEND, 500, 246, 50, 22
  PUSHBUTTON "Post", IDC_POST, 500, 270, 50, 22

  // Bottom bar
  LTEXT "FPS: --", IDC_FPS_LABEL, 6, 424, 340, 14
  CHECKBOX "Highlight", IDC_HIGHLIGHT, 6, 442, 70, 16
  CHECKBOX "Auto", IDC_AUTOREF, 78, 442, 50, 16
  PUSHBUTTON "Refresh", IDC_REFRESH, 130, 440, 60, 20
  PUSHBUTTON "Close", IDCANCEL, 352, 442, 60, 20
END
SWELL_DEFINE_DIALOG_RESOURCE_END2(IDD_INSPECTOR)

// ---- hotkey check (called from swell-backend-sdl3.cpp event dispatch) ----

bool swell_inspector_check_hotkey(WPARAM vk, bool ctrl, bool shift)
{
  if (!g_insp.visible && ctrl && shift && vk == 'I') {
    inspector_open();
    return true;
  }
  if (g_insp.visible && ctrl && shift && vk == 'I') {
    inspector_close();
    return true;
  }
  return false;
}

// ---- public API ----

static bool g_inspector_init_done = false;

void swell_inspector_init()
{
  if (g_inspector_init_done) return;
  g_inspector_init_done = true;

  const char *env = getenv("SWELL_INSPECTOR");
  if (!env || !env[0] || !strcmp(env, "0")) return;

  inspector_open();
}

void inspector_open()
{
  if (g_insp.dlg) {
    ShowWindow(g_insp.dlg, SW_SHOW);
    g_insp.visible = true;
    inspector_refresh_tree();
    return;
  }

  CreateDialog(nullptr, MAKEINTRESOURCE(IDD_INSPECTOR),
               nullptr, inspector_dlg_proc);
  g_insp.visible = true;
}

void inspector_close()
{
  if (g_insp.dlg) {
    g_insp.visible = false;
    ShowWindow(g_insp.dlg, SW_HIDE);
    g_swell_inspector_highlight = nullptr;
  }
}

void inspector_toggle()
{
  if (g_insp.visible)
    inspector_close();
  else
    inspector_open();
}

void swell_inspector_tick()
{
  // Lazy init on first tick (SDL must be initialized)
  if (!g_inspector_init_done)
    swell_inspector_init();

  if (!g_insp.visible) return;

  using namespace std::chrono;
  auto now = steady_clock::now();

  // Auto-refresh tree every 1 second
  static auto last_refresh = now;
  if (g_insp.autorerefresh &&
      duration<double>(now - last_refresh).count() >= 1.0) {
    inspector_refresh_tree();
    last_refresh = now;
  }
}

void swell_inspector_notify_frame(HWND hwnd, double frame_ms)
{
  if (!g_insp.visible) return;
  inspector_update_fps(frame_ms);
}

// paint hook: red outline on selected HWND
extern void SWELL_internalSkiaPaint(HWND hwnd, SkCanvas *canvas,
                                     int x, int y, bool force);

// We hook by checking g_swell_inspector_highlight in the paint path.
// This is done in swell-wnd.cpp's SWELL_RunMessageLoop and UpdateWindow
// by checking g_swell_inspector_highlight after the normal paint.
