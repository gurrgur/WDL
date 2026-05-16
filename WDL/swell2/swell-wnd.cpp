/*
  SWELL2 window management module — headless build
  Implements HWND__ lifecycle, SendMessage, PostMessage queue,
  timers, focus, window hierarchy, and all Get/SetWindow* functions.
*/

#include "swell-internal.h"
#include <cstdlib>
#include <cstring>
#include <sys/time.h>

// ===========================================================================
// Global state
// ===========================================================================

HWND g_swell_capture = NULL;
HWND g_swell_focus = NULL;
HWND g_swell_foreground = NULL;
HWND g_swell_focused_oswindow_hwnd = NULL;
HWND g_swell_top_level_list = NULL;
HWND g_swell_top_level_list_end = NULL;
int g_swell_event_dispatch_depth = 0;

// Timer list
TimerInfoRec *g_timer_list = NULL;
WDL_Mutex g_timer_mutex;

// PostMessage queue
PMQ_rec *g_pmq_head = NULL;
PMQ_rec *g_pmq_tail = NULL;
WDL_Mutex g_pmq_mutex;
int g_pmq_count = 0;

static const int MAX_PMQ_SIZE = 1024;

// ===========================================================================
// HWND__ implementation
// ===========================================================================

HWND__::HWND__(HWND__ *parent, int id, const RECT *r, const char *label,
               bool visible, WNDPROC proc)
  : m_parent(parent), m_next(NULL), m_prev(NULL), m_owner(NULL),
    m_id(id), m_wndproc(proc), m_dlgproc(NULL), m_classname(NULL),
    m_font(NULL), m_private_data(0),
    m_invalidated(false), m_child_invalidated(false),
    m_visible(visible), m_enabled(true), m_wantfocus(true),
    m_focused_child(NULL), m_menu(NULL), m_paintctx(NULL),
    m_hashaddestroy(0), m_oswindow(NULL), m_userdata(0),
    m_oswindow_private(0), m_oswindow_fullscreen(0),
    m_style(0), m_exstyle(0),
    refcnt(1)
{
  memset(m_extra, 0, sizeof(m_extra));
  if (r) m_position = *r;
  else memset(&m_position, 0, sizeof(m_position));
  if (label) m_title.Set(label);

  if (parent) {
    parent->m_children.Add(this);
    m_next = NULL;
    m_prev = NULL;
  } else {
    // top-level: add to global list
    if (g_swell_top_level_list_end) {
      g_swell_top_level_list_end->m_next = this;
      m_prev = g_swell_top_level_list_end;
    } else {
      g_swell_top_level_list = this;
      m_prev = NULL;
    }
    g_swell_top_level_list_end = this;
    m_next = NULL;
  }
}

HWND__::~HWND__()
{
  if (m_hashaddestroy < 2 && m_wndproc) {
    m_hashaddestroy = 2;
    m_wndproc((HWND)this, WM_NCDESTROY, 0, 0);
  }

  if (m_font) {
    DeleteObject(m_font);
    m_font = NULL;
  }

  if (m_oswindow) {
    swell_oswindow_destroy((HWND)this);
  }
}

void HWND__::Retain()
{
  refcnt++;
}

void HWND__::Release()
{
  if (--refcnt <= 0) {
    delete this;
  }
}

// ===========================================================================
// SendMessage
// ===========================================================================

LRESULT SendMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (!hwnd) return 0;

  if (msg == WM_DESTROY) {
    if (hwnd->m_hashaddestroy) return 0;
    hwnd->m_hashaddestroy = 1;
    if (GetCapture() == hwnd) ReleaseCapture();
    SWELL_MessageQueue_Clear(hwnd);
  } else if (hwnd->m_hashaddestroy >= 2) {
    return 0;
  } else if (msg == WM_CAPTURECHANGED && hwnd->m_hashaddestroy) {
    return 0;
  }

  WNDPROC proc = hwnd->m_wndproc;
  if (!proc) {
    return 0;
  }

  hwnd->Retain();
  LRESULT ret = proc(hwnd, msg, wParam, lParam);
  hwnd->Release();

  if (msg == WM_DESTROY) {
    // destroy children
    HWND child = hwnd->m_children.GetSize() > 0 ? hwnd->m_children.Get(0) : NULL;
    while (child) {
      HWND next = HWND(NULL);
      int idx = hwnd->m_children.Find(child);
      if (idx >= 0 && idx + 1 < hwnd->m_children.GetSize())
        next = hwnd->m_children.Get(idx + 1);
      SendMessage(child, WM_DESTROY, 0, 0);
      child = next;
    }
    // destroy owned windows (skip modal dialog boxes)
    for (int i = hwnd->m_owned.GetSize() - 1; i >= 0; i--) {
      HWND ow = hwnd->m_owned.Get(i);
      if (ow && !IsModalDialogBox(ow)) SendMessage(ow, WM_DESTROY, 0, 0);
    }
    // clear focus if this window was focused
    if (g_swell_focused_oswindow_hwnd == hwnd) {
      HWND h = (HWND)hwnd->m_owner;
      while (h && !h->m_oswindow) h = h->m_owner ? (HWND)h->m_owner : (HWND)h->m_parent;
      swell_oswindow_focus(h);
    }
    hwnd->m_wndproc = NULL;
    hwnd->m_hashaddestroy = 2;
    KillTimer(hwnd, (UINT_PTR)-1);
  }

  return ret;
}

// ===========================================================================
// DefWindowProc
// ===========================================================================

LRESULT DefWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (!hwnd) return 0;

  switch (msg) {
    case WM_DESTROY:
      return 0;

    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;

    case WM_NCHITTEST:
      if (!hwnd->m_parent && hwnd->m_menu) {
        RECT r;
        GetWindowContentViewRect(hwnd, &r);
        const int mbh = g_swell_theme.menubar_height;
        if (GET_Y_LPARAM(lParam) >= r.top && GET_Y_LPARAM(lParam) < r.top + mbh)
          return HTMENU;
      }
      return HTCLIENT;

    case WM_NCCALCSIZE:
      // When wParam=TRUE: lParam is NCCALCSIZE_PARAMS*
      // When wParam=FALSE: lParam is RECT*
      if (!hwnd->m_parent && hwnd->m_menu && lParam) {
        if (wParam) {
          NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lParam;
          p->rgrc[0].top += g_swell_theme.menubar_height;
        } else {
          RECT *r = (RECT *)lParam;
          r->top += g_swell_theme.menubar_height;
        }
      }
      return 0;

    case WM_NCPAINT:
      if (hwnd->m_menu && !hwnd->m_parent) {
        // Paint menu bar into NC area using the window's backing store canvas.
        // swell_internalSkiaPaint calls WM_NCPAINT before WM_PAINT, so the
        // backing store canvas is available (held in m_paintctx).
        swell_gdpLocalContext *ctx = hwnd->m_paintctx;
        if (ctx) swell_paint_menubar(hwnd, &ctx->ctx);
      }
      return 0;
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONUP:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
      return 0;

    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
      if (wParam == HTMENU && hwnd->m_menu) {
        // lParam is in screen coords per Win32; convert to window-x
        int win_x = (int)(short)LOWORD(lParam) - hwnd->m_position.left;
        RECT item_sr = {};
        int idx = swell_menubar_hittest(hwnd, win_x, &item_sr);
        if (idx >= 0) {
          HMENU sub = GetSubMenu(hwnd->m_menu, idx);
          if (sub)
            TrackPopupMenu(sub, TPM_LEFTALIGN | TPM_TOPALIGN,
                           item_sr.left, item_sr.bottom, 0, hwnd, NULL);
        }
      }
      return 0;

    case WM_RBUTTONUP: {
      POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
      ClientToScreen(hwnd, &pt);
      SendMessage(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd,
                  MAKELPARAM(pt.x, pt.y));
      return 0;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
      if (hwnd->m_parent) {
        return SendMessage((HWND)hwnd->m_parent, msg, wParam, lParam);
      }
      return 0;

    case WM_SETCURSOR:
      if (hwnd->m_parent) {
        return SendMessage((HWND)hwnd->m_parent, msg, wParam, lParam);
      }
      {
        HCURSOR c = SWELL_GetLastSetCursor();
        if (!c) c = SWELL_LoadCursor(IDC_ARROW);
        SetCursor(c);
      }
      return TRUE;

    case WM_CONTEXTMENU:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      if (hwnd->m_parent) {
        return SendMessage((HWND)hwnd->m_parent, msg, wParam, lParam);
      }
      return 0;

    case WM_SETFONT:
      hwnd->m_font = (HFONT)wParam;
      return 0;

    case WM_GETFONT:
      if (hwnd->m_font) return (LRESULT)hwnd->m_font;
      return (LRESULT)SWELL_GetDefaultFont();

    case WM_DROPFILES:
      if (!(hwnd->m_exstyle & WS_EX_ACCEPTFILES) && hwnd->m_parent) {
        return SendMessage((HWND)hwnd->m_parent, msg, wParam, lParam);
      }
      return 0;

    default:
      return 0;
  }
}

// ===========================================================================
// SwellDialogDefaultWindowProc
// ===========================================================================

LRESULT SwellDialogDefaultWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  DLGPROC dlgproc = hwnd->m_dlgproc;

  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (hdc) {
        RECT r = ps.rcPaint;

        // Query dlgproc for custom background brush (matching original)
        HBRUSH hbrush = NULL;
        if (dlgproc)
          hbrush = (HBRUSH)dlgproc(hwnd, WM_CTLCOLORDLG, (WPARAM)hdc, (LPARAM)hwnd);

        if (hbrush && hbrush != (HBRUSH)1) {
          FillRect(hdc, &r, hbrush);
        } else {
          SWELL_FillDialogBackground(hdc, &r, 0);
        }
        EndPaint(hwnd, &ps);
      }
      break;
    }
    default:
      break;
  }

  // call dlgproc
  INT_PTR dlgret = 0;
  if (dlgproc) {
    dlgret = dlgproc(hwnd, msg, wParam, lParam);
  }

  if (dlgret != 0) return dlgret;

  // default dialog handling when dlgproc returns 0
  switch (msg) {
    case WM_PAINT:
      break; // already handled above

    case WM_CLOSE:
      EndDialog(hwnd, IDCANCEL);
      return 0;

    case WM_KEYDOWN: {
      if (!hwnd->m_parent) {
        if (wParam == VK_ESCAPE && IsWindowEnabled(hwnd)) {
          if (SendMessage(hwnd, WM_CLOSE, 0, 0) == 0 &&
              hwnd->m_hashaddestroy < 2) {
            SendMessage(hwnd, WM_COMMAND, IDCANCEL, 0);
          }
          return 1;
        }
        if (wParam == VK_RETURN) {
          int defid = SWELL_GetDefaultButtonID(hwnd, true);
          if (defid) {
            SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(defid, BN_CLICKED), 0);
          } else {
            SendMessage(hwnd, WM_COMMAND, IDOK, 0);
          }
          return 1;
        }
        // Tab navigation: find next/prev focusable child
        if (wParam == VK_TAB && (lParam & ~FSHIFT) == FVIRTKEY) {
          bool back = (lParam & FSHIFT) != 0;
          int n = hwnd->m_children.GetSize();
          int startIdx = 0;
          HWND curFocus = GetFocus();
          if (curFocus) {
            // find current focused child in z-order
            for (int i = 0; i < n; i++) {
              if (hwnd->m_children.Get(i) == curFocus ||
                  (curFocus->m_parent && hwnd->m_children.Get(i) == (HWND)curFocus->m_parent)) {
                startIdx = i;
                break;
              }
            }
          }
          if (back) startIdx = (startIdx > 0) ? startIdx - 1 : n - 1;
          else startIdx = (startIdx + 1) % n;
          for (int i = 0; i < n; i++) {
            int idx = (startIdx + i) % n;
            HWND ch = hwnd->m_children.Get(idx);
            if (ch && ch->m_visible && ch->m_wantfocus && ch->m_enabled) {
              SetFocus(ch);
              break;
            }
          }
          return 1;
        }
      }
      break;
    }

    case WM_CTLCOLORSTATIC:
      SetTextColor((HDC)wParam, g_swell_theme.fg_text);
      {
        static HBRUSH br;
        if (!br) br = CreateSolidBrush(g_swell_theme.bg_window);
        return (LRESULT)br;
      }

    default:
      break;
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ===========================================================================
// PostMessage queue
// ===========================================================================

#ifndef SWELL_PROVIDED_BY_APP
void SWELL_Internal_PostMessage_Init()
{
}

BOOL SWELL_Internal_PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  return PostMessage(hwnd, msg, wParam, lParam);
}

void SWELL_Internal_PMQ_ClearAllMessages(HWND hwnd)
{
  SWELL_MessageQueue_Clear(hwnd);
}
#endif

BOOL PostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (!hwnd || hwnd->m_hashaddestroy) return FALSE;

  WDL_MutexLock lock(&g_pmq_mutex);

  if (g_pmq_count >= MAX_PMQ_SIZE) {
    // drop oldest
    PMQ_rec *old = g_pmq_head;
    if (old) {
      g_pmq_head = old->_next;
      if (!g_pmq_head) g_pmq_tail = NULL;
      delete old;
      g_pmq_count--;
    }
  }

  PMQ_rec *rec = new PMQ_rec();
  rec->hwnd = hwnd;
  rec->msg = msg;
  rec->wParam = wParam;
  rec->lParam = lParam;
  rec->_next = NULL;

  if (g_pmq_tail) {
    g_pmq_tail->_next = rec;
  } else {
    g_pmq_head = rec;
  }
  g_pmq_tail = rec;
  g_pmq_count++;

  return TRUE;
}

void SWELL_MessageQueue_Flush()
{
  int max_amt = 0;
  g_pmq_mutex.Enter();
  max_amt = g_pmq_count;
  g_pmq_mutex.Leave();

  for (int i = 0; i < max_amt; i++) {
    g_pmq_mutex.Enter();
    PMQ_rec *rec = g_pmq_head;
    if (rec) {
      g_pmq_head = rec->_next;
      if (!g_pmq_head) g_pmq_tail = NULL;
      g_pmq_count--;
    }
    g_pmq_mutex.Leave();

    if (rec) {
      HWND h = rec->hwnd;
      if (h && h->m_hashaddestroy < 2) {
        SendMessage(h, rec->msg, rec->wParam, rec->lParam);
      }
      delete rec;
    }
  }
}

void SWELL_MessageQueue_Clear(HWND h)
{
  WDL_MutexLock lock(&g_pmq_mutex);

  PMQ_rec *prev = NULL;
  PMQ_rec *rec = g_pmq_head;
  while (rec) {
    PMQ_rec *next = rec->_next;
    if (!h || rec->hwnd == h) {
      if (prev) prev->_next = next;
      else g_pmq_head = next;
      if (g_pmq_tail == rec) g_pmq_tail = prev;
      delete rec;
      g_pmq_count--;
      rec = next;
    } else {
      prev = rec;
      rec = next;
    }
  }
}

// ===========================================================================
// Timer management
// ===========================================================================

UINT_PTR SetTimer(HWND hwnd, UINT_PTR timerid, UINT rate, TIMERPROC tProc)
{
  if (!hwnd && !tProc) return 0;
  if (hwnd && !timerid) return 0;
  if (hwnd && hwnd->m_hashaddestroy) return 0;
  if (rate < 1) rate = 1;

  WDL_MutexLock lock(&g_timer_mutex);

  // check for existing timer with same (hwnd, timerid)
  TimerInfoRec *rec = NULL;
  if (hwnd || timerid) {
    rec = g_timer_list;
    while (rec) {
      if (rec->hwnd == hwnd && rec->timerid == timerid) {
        break;
      }
      rec = rec->_next;
    }
  }

  bool recAdd = false;
  if (!rec) {
    rec = new TimerInfoRec();
    recAdd = true;
  }

  if (!hwnd) timerid = (UINT_PTR)rec;

  rec->hwnd = hwnd;
  rec->timerid = timerid;
  rec->interval = rate;
  rec->lastFire = GetTickCount();
  rec->tProc = tProc;

  if (recAdd) {
    rec->_next = g_timer_list;
    g_timer_list = rec;
  }

  return timerid;
}

BOOL KillTimer(HWND hwnd, UINT_PTR timerid)
{
  if (!hwnd && !timerid) return FALSE;

  WDL_MutexLock lock(&g_timer_mutex);
  BOOL rv = FALSE;

  // Do not allow removing all global timers.
  if (timerid != (UINT_PTR)-1 || hwnd) {
    TimerInfoRec *prev = NULL;
    TimerInfoRec *rec = g_timer_list;
    while (rec) {
      bool match = false;
      if (timerid == (UINT_PTR)-1) {
        match = (rec->hwnd == hwnd);
      } else {
        match = (rec->hwnd == hwnd && rec->timerid == timerid);
      }

      if (match) {
        TimerInfoRec *next = rec->_next;
        if (prev) prev->_next = next;
        else g_timer_list = next;

        if (--rec->refcnt < 0) {
          delete rec;
        }
        rv = TRUE;
        if (timerid != (UINT_PTR)-1) break;
        rec = next;
      } else {
        prev = rec;
        rec = rec->_next;
      }
    }
  }

  return rv;
}

// Fire timers (called from SWELL_RunMessageLoop)
static void fireTimers()
{
  g_timer_mutex.Enter();
  DWORD now = GetTickCount();

  TimerInfoRec *rec = g_timer_list;
  while (rec) {
    const DWORD nextFire = rec->lastFire + rec->interval;
    if ((int)(now - nextFire) >= 0 && (int)(now - nextFire) < 100000) {
      rec->lastFire = nextFire;
      rec->refcnt++;
      HWND hwnd = rec->hwnd;
      UINT_PTR timerid = rec->timerid;
      TIMERPROC tProc = rec->tProc;
      if (hwnd) hwnd->Retain();
      g_timer_mutex.Leave();

      if (tProc) {
        tProc(hwnd, WM_TIMER, timerid, now);
      } else if (hwnd && hwnd->m_hashaddestroy < 2) {
        SendMessage(hwnd, WM_TIMER, timerid, 0);
      }

      g_timer_mutex.Enter();
      if (hwnd) hwnd->Release();
      rec->refcnt--;
      if (rec->refcnt < 0) {
        delete rec;
        rec = g_timer_list;
        continue;
      }
    }
    rec = rec->_next;
  }
  g_timer_mutex.Leave();
}

// ===========================================================================
// DestroyWindow
// ===========================================================================

// Physical cleanup of an HWND and all descendants — no hasaddestroy guard.
// Called from DestroyWindow(root) after WM_DESTROY cascade.
static void RecurseDestroyWindow(HWND hwnd)
{
  if (!hwnd) return;

  if (hwnd->m_oswindow) {
    swell_oswindow_destroy(hwnd);
  }

  // recurse into children first (like reference: nullify list, iterate saved)
  {
    WDL_PtrList<HWND__> tmpChildren;
    for (int i = hwnd->m_children.GetSize() - 1; i >= 0; i--) {
      HWND ch = hwnd->m_children.Get(i);
      if (ch) {
        hwnd->m_children.Delete(i, false);
        tmpChildren.Add(ch);
      }
    }
    for (int i = 0; i < tmpChildren.GetSize(); i++)
      RecurseDestroyWindow(tmpChildren.Get(i));
  }

  // owned windows: nullify list, then recurse
  {
    WDL_PtrList<HWND__> tmpOwned;
    for (int i = hwnd->m_owned.GetSize() - 1; i >= 0; i--) {
      HWND ow = hwnd->m_owned.Get(i);
      if (ow && ow->m_hashaddestroy < 2) {
        hwnd->m_owned.Delete(i, false);
        tmpOwned.Add(ow);
      }
    }
    for (int i = 0; i < tmpOwned.GetSize(); i++)
      RecurseDestroyWindow(tmpOwned.Get(i));
  }

  if (hwnd->m_menu) {
    DestroyMenu(hwnd->m_menu);
    hwnd->m_menu = NULL;
  }

  // remove from parent/global lists
  if (hwnd->m_parent) {
    // If this was the parent's focused child, clear the reference
    // so GetFocus() doesn't follow a dangling pointer later.
    if (hwnd->m_parent->m_focused_child == hwnd)
      hwnd->m_parent->m_focused_child = NULL;

    for (int i = 0; i < hwnd->m_parent->m_children.GetSize(); i++) {
      if (hwnd->m_parent->m_children.Get(i) == hwnd) {
        hwnd->m_parent->m_children.Delete(i, false);
        break;
      }
    }
    hwnd->m_parent = NULL;
  } else {
    // top-level: remove from global list
    if (hwnd->m_prev) hwnd->m_prev->m_next = hwnd->m_next;
    else g_swell_top_level_list = hwnd->m_next;
    if (hwnd->m_next) hwnd->m_next->m_prev = hwnd->m_prev;
    else g_swell_top_level_list_end = hwnd->m_prev;
  }

  // Remove from owner's m_owned list.  Owned windows that are destroyed
  // independently (not via the owner's destruction) must be cleaned out
  // so code that iterates m_owned (e.g. WindowFromPoint) does not hit
  // dangling pointers.
  if (hwnd->m_owner) {
    int oi = hwnd->m_owner->m_owned.Find(hwnd);
    if (oi >= 0) hwnd->m_owner->m_owned.Delete(oi, false);
    hwnd->m_owner = NULL;
  }

  SWELL_MessageQueue_Clear(hwnd);
  KillTimer(hwnd, (UINT_PTR)-1);

  if (g_swell_focus == hwnd) g_swell_focus = NULL;

  hwnd->Release();
}

void DestroyWindow(HWND hwnd)
{
  if (!hwnd) return;
  if (hwnd->m_hashaddestroy) {
    return;
  }

  SendMessage(hwnd, WM_DESTROY, 0, 0);

  RecurseDestroyWindow(hwnd);
}

// ===========================================================================
// ShowWindow / EnableWindow
// ===========================================================================

void ShowWindow(HWND hwnd, int cmd)
{
  if (!hwnd) return;

  bool wasVisible = hwnd->m_visible;

  // Matching original SWELL: if already visible, don't steal focus
  if ((cmd == SW_SHOW || cmd == SW_SHOWNA) && hwnd->m_visible)
    cmd = SW_SHOWNA;

  switch (cmd) {
    case SW_HIDE:
      hwnd->m_visible = false;
      break;
    case SW_SHOW:
    case SW_SHOWNA:
    case SW_SHOWMINIMIZED:
    case SW_SHOWMAXIMIZED:
    case SW_RESTORE:
      hwnd->m_visible = true;
      break;
  }

  if (cmd == SW_SHOWMAXIMIZED)
    swell_oswindow_maximize(hwnd);

  if (cmd == SW_HIDE && wasVisible) {
    if (hwnd->m_parent)
      InvalidateRect((HWND)hwnd->m_parent, &hwnd->m_position, FALSE);
  }

  // swell_oswindow_manage BEFORE SetForegroundWindow (matching original)
  // so the OS window exists before focus-switch tries to raise it.
  if (hwnd->m_visible && !wasVisible && !hwnd->m_parent && !hwnd->m_oswindow) {
    swell_oswindow_manage(hwnd, cmd != SW_SHOWNA);
  }

  if (cmd == SW_SHOW)
    SetForegroundWindow(hwnd);

  if (hwnd->m_visible) {
    InvalidateRect(hwnd, NULL, FALSE);
  }
}

void EnableWindow(HWND hwnd, int enable)
{
  if (!hwnd) return;
  hwnd->m_enabled = (enable != 0);

  // if disabling the focused child, clear parent's focused_child
  if (!enable && hwnd->m_parent && hwnd->m_parent->m_focused_child == hwnd) {
    hwnd->m_parent->m_focused_child = NULL;
  }

  InvalidateRect(hwnd, NULL, FALSE);
  swell_oswindow_update_enable(hwnd);
}

bool IsWindowEnabled(HWND hwnd)
{
  if (!hwnd) return false;
  HWND w = hwnd;
  while (w) {
    if (!w->m_enabled) return false;
    w = (HWND)w->m_parent;
  }
  return true;
}

bool IsWindowVisible(HWND hwnd)
{
  // Win32: window is visible only if itself AND all ancestors have WS_VISIBLE
  for (HWND w = hwnd; w; w = w->m_parent)
    if (!w->m_visible) return false;
  return true;
}

bool IsWindow(HWND hwnd)
{
  if (!hwnd) return false;
  HWND w = hwnd;
  while (w->m_parent) w = w->m_parent;
  for (HWND t = g_swell_top_level_list; t; t = t->m_next)
    if (t == w) return true;
  return false;
}

// ===========================================================================
// Focus management
// ===========================================================================

void SetFocus(HWND hwnd)
{
  if (!hwnd) return;

  HWND oldFoc = GetFocus();
  if (oldFoc == hwnd) return;

  if (oldFoc) {
    SendMessage(oldFoc, WM_KILLFOCUS, (WPARAM)hwnd, 0);
  }

  // set focus chain
  hwnd->m_focused_child = NULL;

  HWND w = hwnd;
  while (w->m_parent && !w->m_oswindow) {
    w->m_parent->m_focused_child = w;
    w = (HWND)w->m_parent;
  }

  g_swell_focused_oswindow_hwnd = w;
  g_swell_focus = hwnd;

  swell_oswindow_focus(w);

  if (hwnd != oldFoc) {
    SendMessage(hwnd, WM_SETFOCUS, (WPARAM)oldFoc, 0);
  }
}

HWND GetFocus()
{
  HWND w = g_swell_focused_oswindow_hwnd;
  while (w && w->m_focused_child) {
    w = (HWND)w->m_focused_child;
  }
  while (w && !w->m_visible)
    w = (HWND)w->m_parent;
  g_swell_focus = w;
  return g_swell_focus;
}

void SetForegroundWindow(HWND hwnd)
{
  g_swell_foreground = hwnd;
  if (hwnd) {
    SetFocus(hwnd);
  }
}

HWND GetForegroundWindow()
{
  return GetFocus();
}

// ===========================================================================
// Mouse capture
// ===========================================================================

HWND SetCapture(HWND hwnd)
{
  HWND prev = g_swell_capture;
  if (prev && prev != hwnd) {
    SendMessage(prev, WM_CAPTURECHANGED, 0, (LPARAM)hwnd);
  }
  g_swell_capture = hwnd;
  return prev;
}

HWND GetCapture()
{
  return g_swell_capture;
}

void ReleaseCapture()
{
  if (g_swell_capture) {
    SendMessage(g_swell_capture, WM_CAPTURECHANGED, 0, 0);
    g_swell_capture = NULL;
  }
}

// ===========================================================================
// Window hierarchy
// ===========================================================================

HWND GetParent(HWND hwnd)
{
  if (!hwnd) return NULL;
  // Win32: GetParent returns the owner for top-level owned windows
  if (hwnd->m_parent) return (HWND)hwnd->m_parent;
  if (hwnd->m_owner) return (HWND)hwnd->m_owner;
  return NULL;
}

HWND SetParent(HWND hwnd, HWND newPar)
{
  if (!hwnd) return NULL;

  HWND oldPar = (HWND)hwnd->m_parent;
  if (oldPar == newPar) return oldPar;

  // remove from old parent
  if (oldPar) {
    for (int i = 0; i < oldPar->m_children.GetSize(); i++) {
      if (oldPar->m_children.Get(i) == hwnd) {
        oldPar->m_children.Delete(i, false);
        break;
      }
    }
    if (oldPar->m_focused_child == hwnd)
      oldPar->m_focused_child = NULL;
  } else {
    // remove from global top-level list
    if (hwnd->m_prev) hwnd->m_prev->m_next = hwnd->m_next;
    else g_swell_top_level_list = hwnd->m_next;
    if (hwnd->m_next) hwnd->m_next->m_prev = hwnd->m_prev;
    else g_swell_top_level_list_end = hwnd->m_prev;
    hwnd->m_prev = NULL;
    hwnd->m_next = NULL;
  }

  // add to new parent
  hwnd->m_parent = newPar;
  if (newPar) {
    newPar->m_children.Add(hwnd);
    hwnd->m_style |= WS_CHILD;
  } else {
    // add to global top-level list
    if (g_swell_top_level_list_end) {
      g_swell_top_level_list_end->m_next = hwnd;
      hwnd->m_prev = g_swell_top_level_list_end;
    } else {
      g_swell_top_level_list = hwnd;
      hwnd->m_prev = NULL;
    }
    g_swell_top_level_list_end = hwnd;
    hwnd->m_next = NULL;
    hwnd->m_style &= ~WS_CHILD;
  }

  // Ensure OS window is created/destroyed based on new parent status
  swell_oswindow_manage(hwnd, false);

  return oldPar;
}

HWND GetWindow(HWND hwnd, int what)
{
  if (!hwnd) return NULL;

  switch (what) {
    case GW_HWNDFIRST:
      if (hwnd->m_parent) {
        return hwnd->m_parent->m_children.GetSize() > 0 ?
               hwnd->m_parent->m_children.Get(0) : NULL;
      }
      return g_swell_top_level_list;

    case GW_HWNDLAST:
      if (hwnd->m_parent) {
        int n = hwnd->m_parent->m_children.GetSize();
        return n > 0 ? hwnd->m_parent->m_children.Get(n-1) : NULL;
      }
      return g_swell_top_level_list_end;

    case GW_HWNDNEXT:
      if (hwnd->m_parent) {
        for (int i = 0; i < hwnd->m_parent->m_children.GetSize() - 1; i++) {
          if (hwnd->m_parent->m_children.Get(i) == hwnd) {
            return hwnd->m_parent->m_children.Get(i+1);
          }
        }
        return NULL;
      }
      return hwnd->m_next ? (HWND)hwnd->m_next : NULL;

    case GW_HWNDPREV:
      if (hwnd->m_parent) {
        for (int i = 1; i < hwnd->m_parent->m_children.GetSize(); i++) {
          if (hwnd->m_parent->m_children.Get(i) == hwnd) {
            return hwnd->m_parent->m_children.Get(i-1);
          }
        }
        return NULL;
      }
      return hwnd->m_prev ? (HWND)hwnd->m_prev : NULL;

    case GW_OWNER:
      return hwnd->m_owner ? (HWND)hwnd->m_owner : NULL;

    case GW_CHILD:
      return hwnd->m_children.GetSize() > 0 ?
             hwnd->m_children.Get(0) : NULL;

    default:
      return NULL;
  }
}

int IsChild(HWND hwndParent, HWND hwndChild)
{
  if (!hwndParent || !hwndChild) return FALSE;
  HWND w = (HWND)hwndChild->m_parent;
  while (w) {
    if (w == hwndParent) return TRUE;
    w = (HWND)w->m_parent;
  }
  return FALSE;
}

BOOL EnumWindows(BOOL (*proc)(HWND, LPARAM), LPARAM lp)
{
  if (!proc) return FALSE;
  HWND w = g_swell_top_level_list;
  while (w) {
    if (!proc(w, lp)) return FALSE;
    w = w->m_next;
  }
  return TRUE;
}

BOOL EnumChildWindows(HWND hwnd, BOOL (*proc)(HWND, LPARAM), LPARAM lParam)
{
  if (!hwnd || !proc) return FALSE;
  int n = hwnd->m_children.GetSize();
  for (int i = 0; i < n; i++) {
    HWND ch = hwnd->m_children.Get(i);
    if (ch) {
      if (!proc(ch, lParam)) return FALSE;
      if (!EnumChildWindows(ch, proc, lParam)) return FALSE;
    }
  }
  return TRUE;
}

HWND FindWindowEx(HWND par, HWND lastw, const char *classname, const char *title)
{
  HWND h = lastw ? GetWindow(lastw, GW_HWNDNEXT) :
           par ? GetWindow(par, GW_CHILD) :
           g_swell_top_level_list;
  while (h) {
    bool isOk = true;
    if (title && strcmp(title, h->m_title.Get())) isOk = false;
    else if (classname) {
      if (!h->m_classname || strcmp(classname, h->m_classname)) isOk = false;
    }
    if (isOk) return h;
    h = GetWindow(h, GW_HWNDNEXT);
  }
  return NULL;
}

HWND GetDlgItem(HWND hwnd, int idx)
{
  if (!hwnd) return NULL;
  if (idx == 0) return hwnd;

  int n = hwnd->m_children.GetSize();
  for (int i = 0; i < n; i++) {
    HWND ch = hwnd->m_children.Get(i);
    if (ch && ch->m_id == idx) return ch;
  }
  return NULL;
}

// ===========================================================================
// GetWindowLong / SetWindowLong
// ===========================================================================

LONG_PTR GetWindowLong(HWND hwnd, int idx)
{
  if (!hwnd) return 0;

  switch (idx) {
    case GWL_ID:          return hwnd->m_id;
    case GWL_USERDATA:    return hwnd->m_userdata;
    case GWL_WNDPROC:     return (LONG_PTR)hwnd->m_wndproc;
    case DWL_DLGPROC:     return (LONG_PTR)hwnd->m_dlgproc;
    case GWL_STYLE: {
      LONG_PTR ret = hwnd->m_style;
      if (hwnd->m_visible) ret |= WS_VISIBLE;
      else ret &= ~WS_VISIBLE;
      return ret;
    }
    case GWL_EXSTYLE:     return hwnd->m_exstyle;
    case GWL_HWNDPARENT:  return (LONG_PTR)(hwnd->m_owner);
    default:
      // Win32 nIndex is a byte offset into extra window memory
      if (idx >= 0 && idx < 64 * (int)sizeof(INT_PTR))
        return (LONG_PTR)hwnd->m_extra[idx / (int)sizeof(INT_PTR)];
      return 0;
  }
}

LONG_PTR SetWindowLong(HWND hwnd, int idx, LONG_PTR val)
{
  if (!hwnd) return 0;

  LONG_PTR old = 0;

  switch (idx) {
    case GWL_ID:
      old = hwnd->m_id;
      hwnd->m_id = (int)val;
      return old;
    case GWL_USERDATA:
      old = hwnd->m_userdata;
      hwnd->m_userdata = val;
      return old;
    case GWL_WNDPROC:
      old = (LONG_PTR)hwnd->m_wndproc;
      hwnd->m_wndproc = (WNDPROC)val;
      return old;
    case DWL_DLGPROC:
      old = (LONG_PTR)hwnd->m_dlgproc;
      hwnd->m_dlgproc = (DLGPROC)val;
      return old;
    case GWL_STYLE: {
      DWORD oldStyle = hwnd->m_style;
      DWORD newStyle = (DWORD)val & ~WS_VISIBLE;
      bool wantVis = ((DWORD)val & WS_VISIBLE) != 0;
      if (wantVis != hwnd->m_visible)
        ShowWindow(hwnd, wantVis ? SW_SHOWNA : SW_HIDE);
      hwnd->m_style = newStyle;
      swell_oswindow_update_style(hwnd, oldStyle);
      return (LONG_PTR)oldStyle;
    }
    case GWL_EXSTYLE:
      old = hwnd->m_exstyle;
      hwnd->m_exstyle = (DWORD)val;
      return (LONG_PTR)old;
    case GWL_HWNDPARENT:
      old = (LONG_PTR)hwnd->m_owner;
      if (val != old) {
        if (hwnd->m_owner && hwnd->m_owner->m_owned.Find(hwnd) >= 0)
          hwnd->m_owner->m_owned.Delete(hwnd->m_owner->m_owned.Find(hwnd), false);
        hwnd->m_owner = (HWND__*)val;
        if (hwnd->m_owner)
          hwnd->m_owner->m_owned.Add(hwnd);
      }
      return old;
    default:
      if (idx >= 0 && idx < 64 * (int)sizeof(INT_PTR)) {
        old = (LONG_PTR)hwnd->m_extra[idx / (int)sizeof(INT_PTR)];
        hwnd->m_extra[idx / (int)sizeof(INT_PTR)] = (INT_PTR)val;
        return old;
      }
      return 0;
  }
}

// ===========================================================================
// Window properties (stored per-HWND, matches original SWELL)
// ===========================================================================

HANDLE GetProp(HWND hwnd, const char *name)
{
  if (!hwnd || !name) return NULL;
  return hwnd->m_props.Get(name);
}

BOOL SetProp(HWND hwnd, const char *name, HANDLE data)
{
  if (!hwnd || !name) return FALSE;
  hwnd->m_props.Insert(name, (void *)data);
  return TRUE;
}

HANDLE RemoveProp(HWND hwnd, const char *name)
{
  if (!hwnd || !name) return NULL;
  HANDLE h = GetProp(hwnd, name);
  hwnd->m_props.Delete(name);
  return h;
}

int EnumPropsEx(HWND hwnd, PROPENUMPROCEX proc, LPARAM lParam)
{
  if (!hwnd || !proc) return -1;
  for (int x = 0; x < hwnd->m_props.GetSize(); x++) {
    const char *k = "";
    void *p = hwnd->m_props.Enumerate(x, &k);
    if (!proc(hwnd, k, p, lParam)) return 0;
  }
  return 1;
}

// ===========================================================================
// Dialog item text/value
// ===========================================================================

BOOL SetDlgItemText(HWND hwnd, int idx, const char *text)
{
  HWND w = GetDlgItem(hwnd, idx);
  if (!w) return FALSE;

  w->m_title.Set(text);
  SendMessage(w, WM_SETTEXT, 0, (LPARAM)text);
  InvalidateRect(w, NULL, FALSE);
  return TRUE;
}

BOOL SetDlgItemInt(HWND hwnd, int idx, int val, int issigned)
{
  char buf[64];
  if (issigned) snprintf(buf, sizeof(buf), "%d", val);
  else snprintf(buf, sizeof(buf), "%u", (unsigned int)val);
  return SetDlgItemText(hwnd, idx, buf);
}

int GetDlgItemInt(HWND hwnd, int idx, BOOL *translated, int issigned)
{
  char buf[256] = "";
  BOOL ok = GetDlgItemText(hwnd, idx, buf, sizeof(buf));
  if (!ok) {
    if (translated) *translated = FALSE;
    return 0;
  }
  // Skip leading whitespace (matching original)
  const char *p = buf;
  while (*p == ' ' || *p == '\t') p++;

  if (issigned) {
    char *end = nullptr;
    long v = strtol(p, &end, 10);
    if (translated) *translated = (end != p && *end == '\0') ? TRUE : FALSE;
    return (int)v;
  } else {
    // Unsigned: reject negative values per Win32 spec
    if (*p == '-') {
      if (translated) *translated = FALSE;
      return 0;
    }
    char *end = nullptr;
    unsigned long v = strtoul(p, &end, 10);
    if (translated) *translated = (end != p && *end == '\0') ? TRUE : FALSE;
    return (int)v;
  }
}

BOOL GetDlgItemText(HWND hwnd, int idx, char *text, int textlen)
{
  HWND w = GetDlgItem(hwnd, idx);
  if (!w) {
    if (textlen > 0) text[0] = 0;
    return FALSE;
  }

  const char *src = w->m_title.Get();
  if (!src) src = "";
  lstrcpyn(text, src, textlen);
  return TRUE;
}

int GetWindowTextLength(HWND hwnd)
{
  if (!hwnd) return 0;
  return hwnd->m_title.GetLength();
}

BOOL CheckDlgButton(HWND hwnd, int idx, int check)
{
  HWND w = GetDlgItem(hwnd, idx);
  if (!w) return FALSE;
  return (BOOL)SendMessage(w, BM_SETCHECK, check, 0);
}

int IsDlgButtonChecked(HWND hwnd, int idx)
{
  HWND w = GetDlgItem(hwnd, idx);
  if (!w) return 0;
  return (int)SendMessage(w, BM_GETCHECK, 0, 0);
}

void ClientToScreen(HWND hwnd, POINT *p)
{
  if (!hwnd || !p) return;

  HWND w = hwnd;
  while (w) {
    // Separate position offset from NC (non-client) area offset.
    // m_position is parent-relative for children, screen-absolute for
    // top-level.  NCCALCSIZE receives {0,0,w,h} — a proper window-size
    // rect — and returns NC insets in .left / .top.
    int ww = w->m_position.right - w->m_position.left;
    int wh = w->m_position.bottom - w->m_position.top;
    NCCALCSIZE_PARAMS ncp = {{{0, 0, ww, wh}}};
    SendMessage(w, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
    p->x += w->m_position.left + ncp.rgrc[0].left;
    p->y += w->m_position.top  + ncp.rgrc[0].top;

    w = (HWND)w->m_parent;
  }
}

void ScreenToClient(HWND hwnd, POINT *p)
{
  if (!hwnd || !p) return;

  HWND w = hwnd;
  while (w) {
    int ww = w->m_position.right - w->m_position.left;
    int wh = w->m_position.bottom - w->m_position.top;
    NCCALCSIZE_PARAMS ncp = {{{0, 0, ww, wh}}};
    SendMessage(w, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
    p->x -= w->m_position.left + ncp.rgrc[0].left;
    p->y -= w->m_position.top  + ncp.rgrc[0].top;
    w = (HWND)w->m_parent;
  }
}

void GetClientRect(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return;

  RECT wr = hwnd->m_position;
  NCCALCSIZE_PARAMS ncp = {{{0, 0, wr.right - wr.left, wr.bottom - wr.top}}};
  SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
  // Normalize to client-local coordinates: Win32 contract requires top-left = (0,0).
  int l = ncp.rgrc[0].left, t = ncp.rgrc[0].top;
  r->left   = 0;
  r->top    = 0;
  r->right  = ncp.rgrc[0].right  - l;
  r->bottom = ncp.rgrc[0].bottom - t;
}

bool GetWindowRect(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return false;
  if (hwnd->m_oswindow) {
    // Top-level window: m_position is screen-absolute.
    *r = hwnd->m_position;
    return true;
  }
  // Child window: convert parent-relative m_position to screen coordinates.
  r->left = r->top = 0;
  ClientToScreen(hwnd, (LPPOINT)r);
  r->right = r->left + hwnd->m_position.right - hwnd->m_position.left;
  r->bottom = r->top + hwnd->m_position.bottom - hwnd->m_position.top;
  return true;
}

void GetWindowContentViewRect(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return;
  if (hwnd->m_oswindow) {
    // top-level window: return screen-absolute m_position (matching original)
    *r = hwnd->m_position;
    return;
  }
  GetWindowRect(hwnd, r);
}

void SetWindowPos(HWND hwnd, HWND zorder, int x, int y, int cx, int cy, int flags)
{
  if (!hwnd) return;

  bool moved = false, sized = false;

  if (!(flags & SWP_NOMOVE)) {
    if (hwnd->m_position.left != x || hwnd->m_position.top != y) {
      // Preserve dimensions: update right/bottom to keep width/height constant.
      // Without this, leaving right/bottom at old values corrupts the size when
      // SWP_NOSIZE is also set.
      int w = hwnd->m_position.right  - hwnd->m_position.left;
      int h = hwnd->m_position.bottom - hwnd->m_position.top;
      hwnd->m_position.left   = x;
      hwnd->m_position.top    = y;
      hwnd->m_position.right  = x + w;
      hwnd->m_position.bottom = y + h;
      moved = true;
    }
  }

  if (!(flags & SWP_NOSIZE)) {
    int w = cx, h = cy;
    if (hwnd->m_position.right - hwnd->m_position.left != w ||
        hwnd->m_position.bottom - hwnd->m_position.top != h) {
      hwnd->m_position.right = hwnd->m_position.left + w;
      hwnd->m_position.bottom = hwnd->m_position.top + h;
      sized = true;
    }
  }

  // Z-order reordering (matching original SWELL semantics):
  // Children list is bottom-to-top. HWND_BOTTOM → index 0.
  // HWND_TOP → end of list. Specific HWND → insert after it.
  if (!(flags & SWP_NOZORDER) && hwnd->m_parent && zorder != hwnd) {
    HWND par = (HWND)hwnd->m_parent;
    int myIdx = par->m_children.Find(hwnd);
    if (myIdx >= 0) {
      par->m_children.Delete(myIdx, false);
      if (zorder == HWND_BOTTOM) {
        par->m_children.Insert(0, hwnd);
      } else if (zorder == HWND_TOP) {
        par->m_children.Add(hwnd);
      } else {
        int zIdx = par->m_children.Find(zorder);
        if (zIdx >= 0) {
          par->m_children.Insert(zIdx + 1, hwnd);
        } else {
          par->m_children.Add(hwnd);
        }
      }
    }
  }

  int reposflag = (moved ? 1 : 0) | (sized ? 2 : 0);
  // Do not resize the OS window while in fullscreen mode (matching original SWELL).
  if (reposflag && !hwnd->m_oswindow_fullscreen) {
    swell_oswindow_resize(hwnd, reposflag, &hwnd->m_position);
  }

  if (sized) {
    RECT ncr = {0, 0, cx, cy};
    SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncr);
    SendMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(ncr.right - ncr.left, ncr.bottom - ncr.top));
  }
  if (moved) {
    SendMessage(hwnd, WM_MOVE, 0, MAKELPARAM(x, y));
  }

  if (flags & SWP_SHOWWINDOW) {
    ShowWindow(hwnd, SW_SHOW);
  }
}

// Recursive helper: search `parent`'s children in parent-relative coordinates.
// `p` must already be in parent-client coordinates (subtract NC offsets first).
static HWND windowfrompoint_recurse(HWND parent, POINT p)
{
  if (!parent) return NULL;
  for (int i = parent->m_children.GetSize() - 1; i >= 0; i--) {
    HWND ch = parent->m_children.Get(i);
    if (!ch || !ch->m_visible) continue;
    RECT cr = ch->m_position; // parent-relative
    if (p.x >= cr.left && p.x < cr.right &&
        p.y >= cr.top && p.y < cr.bottom) {
      // Descend: convert p to child-client coords (subtract position + NC)
      POINT cp = { p.x - cr.left, p.y - cr.top };
      int cw = cr.right - cr.left;
      int chh = cr.bottom - cr.top;
      NCCALCSIZE_PARAMS ncp = {{{0, 0, cw, chh}}};
      SendMessage(ch, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
      cp.x -= ncp.rgrc[0].left;
      cp.y -= ncp.rgrc[0].top;
      HWND deeper = windowfrompoint_recurse(ch, cp);
      return deeper ? deeper : ch;
    }
  }
  return NULL;
}

// Hit-test owned windows of `owner` in screen coords. Owned windows float
// above their owner and must be checked before the owner's own children.
// Returns the deepest matching HWND, or NULL.
static HWND windowfrompoint_check_owned(HWND owner, POINT p)
{
  for (int i = owner->m_owned.GetSize() - 1; i >= 0; i--) {
    HWND ow = owner->m_owned.Get(i);
    if (!ow || !ow->m_visible) continue;
    RECT orr = ow->m_position;
    if (p.x >= orr.left && p.x < orr.right &&
        p.y >= orr.top  && p.y < orr.bottom) {
      POINT op = { p.x - orr.left, p.y - orr.top };
      int oww = orr.right - orr.left;
      int owh = orr.bottom - orr.top;
      NCCALCSIZE_PARAMS oncp = {{{0, 0, oww, owh}}};
      SendMessage(ow, WM_NCCALCSIZE, FALSE, (LPARAM)&oncp);
      op.x -= oncp.rgrc[0].left;
      op.y -= oncp.rgrc[0].top;
      HWND ch = windowfrompoint_recurse(ow, op);
      return ch ? ch : ow;
    }
  }
  return NULL;
}

HWND WindowFromPoint(POINT p)
{
  HWND w = g_swell_top_level_list;
  while (w) {
    RECT r = w->m_position;
    if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) {
      // Check owned windows first — they float above the owner.
      HWND oh = windowfrompoint_check_owned(w, p);
      if (oh) return oh;

      // Convert screen point to top-level client coords
      POINT cp = { p.x - r.left, p.y - r.top };
      int ww = r.right - r.left;
      int wh = r.bottom - r.top;
      NCCALCSIZE_PARAMS ncp = {{{0, 0, ww, wh}}};
      SendMessage(w, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
      cp.x -= ncp.rgrc[0].left;
      cp.y -= ncp.rgrc[0].top;
      HWND ch = windowfrompoint_recurse(w, cp);
      return ch ? ch : w;
    }
    w = w->m_next;
  }
  return NULL;
}

// ===========================================================================
// Invalidation
// ===========================================================================

BOOL InvalidateRect(HWND hwnd, const RECT *r, int eraseBk)
{
  if (!hwnd || !hwnd->m_visible) return FALSE;

  // Convert r to top-level window's client coordinates (matching original SWELL)
  RECT rect;
  if (r) {
    rect = *r;
  } else {
    rect = hwnd->m_position;
    WinOffsetRect(&rect, -rect.left, -rect.top);
  }

  // rect is in client coords of hwnd. Walk up ancestor chain
  // applying position + NCCALCSIZE offsets to reach top-level coords.
  HWND h = hwnd;

  for (;;) {
    if (!h->m_visible || h->m_hashaddestroy) return FALSE;

    RECT ncrect = h->m_position;
    if (h->m_oswindow) WinOffsetRect(&ncrect, -ncrect.left, -ncrect.top);

    NCCALCSIZE_PARAMS tr;
    memset(&tr, 0, sizeof(tr));
    tr.rgrc[0] = ncrect;
    if (h->m_wndproc)
      h->m_wndproc(h, WM_NCCALCSIZE, FALSE, (LPARAM)&tr);

    WinOffsetRect(&rect, tr.rgrc[0].left, tr.rgrc[0].top);

    if (!IntersectRect(&rect, &rect, &ncrect)) return FALSE;

    if (h->m_oswindow) break;

    h = (HWND)h->m_parent;
    if (!h) return FALSE;
  }

  hwnd->m_invalidated = true;

  // WS_CLIPSIBLINGS: invalidate later siblings that intersect us.
  // Children list is bottom-to-top; siblings at higher indices are
  // above in z-order and must be repainted to cover our area.
  {
    HWND t = (HWND)hwnd->m_parent;
    if (t && ((t->m_style | hwnd->m_style) & WS_CLIPSIBLINGS)) {
      int myIdx = t->m_children.Find(hwnd);
      if (myIdx >= 0) {
        for (int i = myIdx + 1; i < t->m_children.GetSize(); i++) {
           HWND nw = t->m_children.Get(i);
           if (nw && nw->m_visible && !nw->m_invalidated && !nw->m_hashaddestroy) {
            RECT tmp;
            if (WinIntersectRect(&tmp, &hwnd->m_position, &nw->m_position))
              nw->m_invalidated = true;
          }
        }
      }
    }
  }

  // walk up ancestor chain marking child_invalidated
  // if eraseBk > 0, also invalidate parent backgrounds up to eraseBk levels
  {
    HWND w = (HWND)hwnd->m_parent;
    while (w) {
      if (eraseBk) {
        w->m_invalidated = true;
        eraseBk--;
      }
      w->m_child_invalidated = true;
      w = (HWND)w->m_parent;
    }
  }

  // h is the OS window found by the ancestor walk loop
  swell_oswindow_invalidate(h, (hwnd != h || r) ? &rect : NULL);
  return TRUE;
}

void UpdateWindow(HWND hwnd)
{
  if (!hwnd) return;
  if (!hwnd->m_invalidated && !hwnd->m_child_invalidated) return;

  HWND top = hwnd;
  while (top->m_parent) top = (HWND)top->m_parent;
  if (!top->m_backingstore) return;
  if (g_swell_event_dispatch_depth > 0) return;

  // guard against re-entrant paint cycles when UpdateWindow is called
  // from within a WM_PAINT handler (matching swell-experimental's
  // s_sdl_paint_depth guard in swell_sdl_paint).
  static int s_updatewindow_depth = 0;
  if (s_updatewindow_depth > 0) return;
  s_updatewindow_depth++;

  SkCanvas *canvas = top->m_backingstore->getCanvas();
  if (canvas) {
    SWELL_internalSkiaPaint(top, canvas, 0, 0, false);
    swell_oswindow_updatetoscreen(top, NULL);
  }

  s_updatewindow_depth--;
}

// ===========================================================================
// ScrollWindow
// ===========================================================================

BOOL ScrollWindow(HWND hwnd, int xamt, int yamt,
                  const RECT *lpRect, const RECT *lpClipRect)
{
  if (!hwnd || (!xamt && !yamt)) return FALSE;
  (void)lpRect; (void)lpClipRect; // not yet supported per-rect scrolling
  InvalidateRect(hwnd, NULL, FALSE);
  for (int i = 0; i < hwnd->m_children.GetSize(); i++) {
    HWND c = hwnd->m_children.Get(i);
    if (c) {
      c->m_position.left   += xamt;
      c->m_position.right  += xamt;
      c->m_position.top    += yamt;
      c->m_position.bottom += yamt;
    }
  }
  return TRUE;
}

// ===========================================================================
// GetClassName / SWELL_SetClassName
// ===========================================================================

int GetClassName(HWND hwnd, char *buf, int bufsz)
{
  if (!hwnd || !buf || bufsz <= 0) return 0;
  if (hwnd->m_classname) {
    lstrcpyn(buf, hwnd->m_classname, bufsz);
    return strlen(buf);
  }
  buf[0] = 0;
  return 0;
}

void SWELL_SetClassName(HWND hwnd, const char *name)
{
  if (!hwnd) return;
  hwnd->m_classname = name; // caller must pass static string
}

// ===========================================================================
// SWELL_BroadcastMessage
// ===========================================================================

void SWELL_BroadcastMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
  HWND w = g_swell_top_level_list;
  while (w) {
    SendMessage(w, msg, wParam, lParam);
    w = w->m_next;
  }
}

// ===========================================================================
// Custom control registration
// ===========================================================================

struct ControlCreatorNode {
  SWELL_ControlCreatorProc proc;
  ControlCreatorNode *_next;
};

static ControlCreatorNode *g_control_creators = NULL;

void SWELL_RegisterCustomControlCreator(SWELL_ControlCreatorProc proc)
{
  if (!proc) return;
  ControlCreatorNode *n = new ControlCreatorNode();
  n->proc = proc;
  n->_next = g_control_creators;
  g_control_creators = n;
}

void SWELL_UnregisterCustomControlCreator(SWELL_ControlCreatorProc proc)
{
  ControlCreatorNode *prev = NULL;
  ControlCreatorNode *n = g_control_creators;
  while (n) {
    if (n->proc == proc) {
      if (prev) prev->_next = n->_next;
      else g_control_creators = n->_next;
      delete n;
      return;
    }
    prev = n;
    n = n->_next;
  }
}

// Invoked by swell-dlg.cpp for unknown classnames
HWND swell_invoke_control_creators(HWND parent, const char *cname, int idx,
                                   const char *classname, int style,
                                   int x, int y, int w, int h)
{
  for (ControlCreatorNode *n = g_control_creators; n; n = n->_next) {
    HWND res = n->proc(parent, cname, idx, classname, style, x, y, w, h);
    if (res) return res;
  }
  return NULL;
}

// ===========================================================================
// SWELL_GetDefaultButtonID
// ===========================================================================

int SWELL_GetDefaultButtonID(HWND hwndDlg, bool onlyIfEnabled)
{
  if (!hwndDlg) return 0;
  int n = hwndDlg->m_children.GetSize();
  for (int i = 0; i < n; i++) {
    HWND ch = hwndDlg->m_children.Get(i);
    if (!ch) continue;
    if (ch->m_style & BS_DEFPUSHBUTTON) {
      if (onlyIfEnabled && !IsWindowEnabled(ch)) continue;
      return ch->m_id;
    }
  }
  return 0;
}

// ===========================================================================
// SWELL helper functions
// ===========================================================================

void SWELL_DrawFocusRect(HWND hwndPar, RECT *rct, void **handle)
{
  // stub: headless mode, no drawing
}

BOOL SWELL_IsGroupBox(HWND hwnd)
{
  if (!hwnd) return FALSE;
  return (hwnd->m_style & BS_GROUPBOX) != 0;
}

BOOL SWELL_IsButton(HWND hwnd)
{
  if (!hwnd) return FALSE;
  return (hwnd->m_style & (BS_PUSHBUTTON | BS_DEFPUSHBUTTON | BS_AUTOCHECKBOX |
         BS_AUTO3STATE | BS_AUTORADIOBUTTON | BS_OWNERDRAW | BS_GROUPBOX)) != 0;
}

BOOL SWELL_IsStaticText(HWND hwnd)
{
  if (!hwnd) return FALSE;
  if (!hwnd->m_classname) return FALSE;
  return strcmp(hwnd->m_classname, "Static") == 0;
}

void SWELL_GetDesiredControlSize(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return;
  if (!hwnd->m_classname) return;  // unknown class → leave r untouched

  bool isbutton = !strcmp(hwnd->m_classname, "Button") &&
                  !(hwnd->m_style & BS_GROUPBOX);
  bool isstatic = !isbutton && !strcmp(hwnd->m_classname, "Static");

  if (!isbutton && !isstatic) return;  // leave r alone

  const int sf = hwnd->m_style & 0xf;
  const bool ischk = isbutton && (sf == BS_AUTO3STATE ||
                                  sf == BS_AUTOCHECKBOX ||
                                  sf == BS_AUTORADIOBUTTON);
  const int chksz = SWELL_UI_SCALE(12 + 6);

  RECT r2 = {0, 0, 0, 0};
  HDC hdc = GetDC(hwnd);
  DrawText(hdc, hwnd->m_title.Get(), -1, &r2, DT_CALCRECT);
  ReleaseDC(hwnd, hdc);

  if (isbutton)
    r->right = r->left + r2.right + SWELL_UI_SCALE(6) + (ischk ? chksz : 0);
  else
    r->right = r->left + r2.right + SWELL_UI_SCALE(4);
  r->bottom = r->top + r2.bottom;
}

void SWELL_DisableContextMenu(HWND hwnd, bool disable)
{
  // stub: headless mode
}

int SWELL_SetWindowLevel(HWND hwnd, int newlevel)
{
  return newlevel;
}

int SWELL_GetWindowWantRaiseAmt(HWND h)
{
  return 0;
}

void SWELL_SetWindowWantRaiseAmt(HWND h, int amt)
{
}

// ===========================================================================
// Utility: MulDiv, lstrcpyn
// ===========================================================================

int MulDiv(int a, int b, int c)
{
  if (c == 0) return -1;
  return (int)(((long long)a * (long long)b) / c);
}

char *lstrcpyn(char *dest, const char *src, int l)
{
  if (!dest || !src || l <= 0) return dest;
  char *d = dest;
  while (--l > 0 && *src) {
    *d++ = *src++;
  }
  *d = 0;
  return dest;
}

// ===========================================================================
// Sleep, GetTickCount, GetFileTime
// ===========================================================================

void Sleep(int ms)
{
  if (ms <= 0) {
    usleep(100);
  } else {
    usleep(ms * 1000);
  }
}

DWORD GetTickCount()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (DWORD)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

BOOL GetFileTime(int filedes, FILETIME *lpCreationTime,
                 FILETIME *lpLastAccessTime, FILETIME *lpLastWriteTime)
{
  if (!lpCreationTime && !lpLastAccessTime && !lpLastWriteTime) return FALSE;

  struct stat st;
  if (fstat(filedes, &st) < 0) return FALSE;

  auto toFileTime = [](time_t tm, FILETIME *ft) {
    if (!ft) return;
    unsigned long long t = (unsigned long long)tm * 10000000ULL + 116444736000000000ULL;
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
  };

  toFileTime(st.st_ctime, lpCreationTime);
  toFileTime(st.st_atime, lpLastAccessTime);
  toFileTime(st.st_mtime, lpLastWriteTime);
  return TRUE;
}

// ===========================================================================
// SWELL_RunMessageLoop (overrides headless backend stub)
// ===========================================================================

#ifndef SWELL_TARGET_OSX
#undef SWELL_RunMessageLoop
#endif

void SWELL_RunMessageLoop()
{
  // Flush posted messages first (matching original order):
  // posted messages may generate more messages and need fresh OS events.
  SWELL_MessageQueue_Flush();

  // Process OS events — they may resize/invalidate windows
  SWELL_RunEvents();

  // Paint all dirty top-level windows (deferred from InvalidateRect calls)
  HWND w = g_swell_top_level_list;
  while (w) {
    if ((w->m_invalidated || w->m_child_invalidated) && w->m_backingstore) {
      SkCanvas *canvas = w->m_backingstore->getCanvas();
      if (canvas) {
        SWELL_internalSkiaPaint(w, canvas, 0, 0, false);
        swell_oswindow_updatetoscreen(w, NULL);
      }
    }
    w = w->m_next;
  }

  fireTimers();
}
