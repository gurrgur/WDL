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
    m_font = NULL;
  }

  if (m_oswindow) {
    swell_oswindow_destroy((HWND)this);
  }
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
  if (hwnd->m_hashaddestroy >= 2) return 0;

  WNDPROC proc = hwnd->m_wndproc;
  if (!proc) return 0;

  return proc(hwnd, msg, wParam, lParam);
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

    case WM_NCHITTEST:
      return HTCLIENT;

    case WM_NCCALCSIZE:
      // for top-level with menu: adjust r->top += menubar_height
      if (!hwnd->m_parent && hwnd->m_menu) {
        NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lParam;
        if (p) {
          p->rgrc[0].top += g_swell_ctheme.menubar_height;
        }
      }
      return 0;

    case WM_NCPAINT:
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
      return 0;

    case WM_RBUTTONUP:
      SendMessage(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd,
                  MAKELPARAM(0xFFFF, 0xFFFF));
      return 0;

    case WM_KEYDOWN:
    case WM_KEYUP:
      if (hwnd->m_parent) {
        return SendMessage((HWND)hwnd->m_parent, msg, wParam, lParam);
      }
      return 0;

    case WM_CONTEXTMENU:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_SETCURSOR:
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

    case WM_KEYDOWN: {
      if (!hwnd->m_parent) {
        if (wParam == VK_ESCAPE) {
          if (SendMessage(hwnd, WM_CLOSE, 0, 0) == 0) {
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
        // Tab navigation
        if (wParam == VK_TAB && (lParam & ~FSHIFT) == FVIRTKEY) {
          bool back = (lParam & FSHIFT) != 0;
          // find next/prev focusable child
          HWND focus = GetFocus();
          HWND start = back ? NULL : (hwnd->m_focused_child ? (HWND)hwnd->m_focused_child : NULL);
          if (!start) start = hwnd;
          // simplified: just move to next sibling
          int n = hwnd->m_children.GetSize();
          for (int i = 0; i < n; i++) {
            HWND ch = hwnd->m_children.Get(i);
            if (ch && ch->m_visible && ch->m_wantfocus) {
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
      // default: set text color and return default brush
      SetTextColor((HDC)wParam, g_swell_ctheme.label_text);
      return (LRESULT)GetStockObject(NULL_BRUSH);

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
  g_pmq_mutex.Enter();
  int n = g_pmq_count;
  g_pmq_mutex.Leave();

  for (int i = 0; i < n; i++) {
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
  if (rate < 1) rate = 1;

  WDL_MutexLock lock(&g_timer_mutex);

  // check for existing timer with same (hwnd, timerid)
  TimerInfoRec *rec = g_timer_list;
  while (rec) {
    if (rec->hwnd == hwnd && rec->timerid == timerid) {
      rec->interval = rate;
      rec->tProc = tProc;
      rec->lastFire = GetTickCount();
      return timerid;
    }
    rec = rec->_next;
  }

  // create new timer
  TimerInfoRec *newrec = new TimerInfoRec();
  newrec->hwnd = hwnd;
  newrec->timerid = timerid;
  newrec->interval = rate;
  newrec->lastFire = GetTickCount();
  newrec->tProc = tProc;
  newrec->refcnt = 0;
  newrec->_next = g_timer_list;
  g_timer_list = newrec;

  return timerid;
}

BOOL KillTimer(HWND hwnd, UINT_PTR timerid)
{
  WDL_MutexLock lock(&g_timer_mutex);

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
      if (rec->refcnt > 0) {
        rec->refcnt = -1; // marked for deletion after callback
        prev = rec;
        rec = next;
      } else {
        if (prev) prev->_next = next;
        else g_timer_list = next;
        delete rec;
        rec = next;
      }
    } else {
      prev = rec;
      rec = rec->_next;
    }
  }

  return TRUE;
}

// Fire timers (called from SWELL_RunMessageLoop)
static void fireTimers()
{
  g_timer_mutex.Enter();
  DWORD now = GetTickCount();

  TimerInfoRec *rec = g_timer_list;
  while (rec) {
    TimerInfoRec *next = rec->_next;
    if (rec->refcnt >= 0 &&
        ((int)(now - rec->lastFire) >= (int)rec->interval)) {
      rec->lastFire = now;
      rec->refcnt++;
      g_timer_mutex.Leave();

      if (rec->tProc) {
        rec->tProc(rec->hwnd, WM_TIMER, rec->timerid, now);
      } else if (rec->hwnd && rec->hwnd->m_hashaddestroy < 2) {
        SendMessage(rec->hwnd, WM_TIMER, rec->timerid, 0);
      }

      g_timer_mutex.Enter();
      rec->refcnt--;
      if (rec->refcnt < 0) {
        TimerInfoRec *pr = NULL, *r = g_timer_list;
        while (r) {
          if (r == rec) {
            if (pr) pr->_next = r->_next;
            else g_timer_list = r->_next;
            delete r;
            break;
          }
          pr = r;
          r = r->_next;
        }
      }
    }
    rec = next;
  }
  g_timer_mutex.Leave();
}

// ===========================================================================
// DestroyWindow
// ===========================================================================

void DestroyWindow(HWND hwnd)
{
  if (!hwnd) return;
  if (hwnd->m_hashaddestroy) return;

  // step 1: WM_DESTROY
  hwnd->m_hashaddestroy = 1;

  if (hwnd->m_wndproc) {
    hwnd->m_wndproc(hwnd, WM_DESTROY, 0, 0);
  }

  // destroy children
  int n = hwnd->m_children.GetSize();
  for (int i = 0; i < n; i++) {
    HWND ch = hwnd->m_children.Get(i);
    if (ch) DestroyWindow(ch);
  }

  // destroy owned windows
  n = hwnd->m_owned.GetSize();
  for (int i = 0; i < n; i++) {
    HWND ow = hwnd->m_owned.Get(i);
    if (ow) DestroyWindow(ow);
  }

  // clear message queue for this window
  SWELL_MessageQueue_Clear(hwnd);

  // kill all timers
  KillTimer(hwnd, (UINT_PTR)-1);

  hwnd->m_wndproc = NULL;
  hwnd->m_hashaddestroy = 2;

  // step 2: RecurseDestroyWindow
  // destroy OS window
  if (hwnd->m_oswindow) {
    swell_oswindow_destroy(hwnd);
  }

  // destroy children again (RecurseDestroyWindow behavior)
  n = hwnd->m_children.GetSize();
  for (int i = n - 1; i >= 0; i--) {
    HWND ch = hwnd->m_children.Get(i);
    if (ch) {
      hwnd->m_children.Delete(i, false);
      DestroyWindow(ch);
    }
  }

  // destroy owned
  n = hwnd->m_owned.GetSize();
  for (int i = n - 1; i >= 0; i--) {
    HWND ow = hwnd->m_owned.Get(i);
    if (ow) {
      hwnd->m_owned.Delete(i, false);
      DestroyWindow(ow);
    }
  }

  // destroy menu
  if (hwnd->m_menu) {
    DestroyMenu(hwnd->m_menu);
    hwnd->m_menu = NULL;
  }

  // remove from parent/global lists
  if (hwnd->m_parent) {
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

  // clear message queue and timers again (safety)
  SWELL_MessageQueue_Clear(hwnd);
  KillTimer(hwnd, (UINT_PTR)-1);

  // Release (decrements refcnt, calls delete when 0)
  hwnd->Release();
}

// ===========================================================================
// ShowWindow / EnableWindow
// ===========================================================================

void ShowWindow(HWND hwnd, int cmd)
{
  if (!hwnd) return;

  bool wasVisible = hwnd->m_visible;

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

  if (hwnd->m_visible && !wasVisible && !hwnd->m_parent && !hwnd->m_oswindow) {
    swell_oswindow_manage(hwnd, cmd != SW_SHOWNA);
  }

  SendMessage(hwnd, WM_SHOWWINDOW, hwnd->m_visible ? TRUE : FALSE, cmd);
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
  if (!hwnd) return false;
  return hwnd->m_visible;
}

bool IsWindow(HWND hwnd)
{
  // non-null pointer is enough for the headless backend
  return hwnd != NULL;
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
  while (w->m_parent) {
    w->m_parent->m_focused_child = w;
    w = (HWND)w->m_parent;
  }

  g_swell_focused_oswindow_hwnd = w;
  g_swell_focus = hwnd;

  swell_oswindow_focus(hwnd);

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
  if (w && w->m_visible) g_swell_focus = w;
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
  return g_swell_foreground;
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
  return (HWND)hwnd->m_parent;
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
  }

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
    if (ch && !proc(ch, lParam)) return FALSE;
  }
  return TRUE;
}

HWND FindWindowEx(HWND par, HWND lastw, const char *classname, const char *title)
{
  HWND list = NULL;
  if (par) {
    if (par->m_children.GetSize() > 0) {
      list = par->m_children.Get(0);
    }
  } else {
    list = g_swell_top_level_list;
  }

  bool pastStart = (lastw == NULL);
  while (list) {
    if (pastStart) {
      bool match = true;
      if (classname && list->m_classname) {
        match = (strcmp(classname, list->m_classname) == 0);
      }
      if (match && title && title[0]) {
        match = (strcmp(title, list->m_title.Get()) == 0);
      }
      if (match) return list;
    }
    if (par) {
      for (int i = 0; i < par->m_children.GetSize(); i++) {
        if (par->m_children.Get(i) == lastw) {
          pastStart = true;
          break;
        }
      }
      pastStart = true; // simplified: just iterate all children after lastw
    }
    lastw = list;
    list = par ? list->m_next : list->m_next;
    pastStart = true;
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
    case GWL_STYLE:       return hwnd->m_style;
    case GWL_EXSTYLE:     return hwnd->m_exstyle;
    case GWL_HWNDPARENT:  return (LONG_PTR)(hwnd->m_parent);
    default:
      if (idx >= 0 && idx < 64) return hwnd->m_extra[idx];
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
      hwnd->m_style = (DWORD)val;
      SendMessage(hwnd, WM_STYLECHANGED, GWL_STYLE,
                  (LPARAM)(new STYLESTRUCT{oldStyle, (DWORD)val}));
      swell_oswindow_update_style(hwnd, oldStyle);
      return (LONG_PTR)oldStyle;
    }
    case GWL_EXSTYLE:
      old = hwnd->m_exstyle;
      hwnd->m_exstyle = (DWORD)val;
      SendMessage(hwnd, WM_STYLECHANGED, GWL_EXSTYLE,
                  (LPARAM)(new STYLESTRUCT{(DWORD)old, (DWORD)val}));
      return (LONG_PTR)old;
    default:
      if (idx >= 0 && idx < 64) {
        old = hwnd->m_extra[idx];
        hwnd->m_extra[idx] = (INT_PTR)val;
        return old;
      }
      return 0;
  }
}

// ===========================================================================
// Window properties (Prop list)
// ===========================================================================

struct PropEntry {
  const char *name;   // for string props
  UINT_PTR id;        // for integer props (name < 65536)
  HANDLE data;
  PropEntry *_next;
  bool isById() const { return (UINT_PTR)name < 65536; }
};

static PropEntry *propList = NULL;

HANDLE GetProp(HWND hwnd, const char *name)
{
  if (!hwnd || !name) return NULL;

  for (PropEntry *e = propList; e; e = e->_next) {
    if (e->isById()) {
      if (e->id == (UINT_PTR)name) return e->data;
    } else {
      if (e->name && strcmp(e->name, name) == 0) return e->data;
    }
  }
  return NULL;
}

BOOL SetProp(HWND hwnd, const char *name, HANDLE data)
{
  if (!hwnd || !name) return FALSE;

  // remove existing
  RemoveProp(hwnd, name);

  PropEntry *e = new PropEntry();
  if ((UINT_PTR)name < 65536) {
    e->name = NULL;
    e->id = (UINT_PTR)name;
  } else {
    e->name = name;
    e->id = 0;
  }
  e->data = data;
  e->_next = propList;
  propList = e;
  return TRUE;
}

HANDLE RemoveProp(HWND hwnd, const char *name)
{
  if (!hwnd || !name) return NULL;

  PropEntry *prev = NULL;
  for (PropEntry *e = propList; e; prev = e, e = e->_next) {
    bool match = false;
    if ((UINT_PTR)name < 65536) {
      match = e->isById() && e->id == (UINT_PTR)name;
    } else {
      match = !e->isById() && e->name && strcmp(e->name, name) == 0;
    }
    if (match) {
      HANDLE data = e->data;
      if (prev) prev->_next = e->_next;
      else propList = e->_next;
      delete e;
      return data;
    }
  }
  return NULL;
}

int EnumPropsEx(HWND hwnd, PROPENUMPROCEX proc, LPARAM lParam)
{
  if (!hwnd || !proc) return -1;

  for (PropEntry *e = propList; e; e = e->_next) {
    const char *name = e->isById() ? (const char *)(UINT_PTR)e->id : e->name;
    if (!proc(hwnd, name, e->data, lParam)) return 0;
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
  GetDlgItemText(hwnd, idx, buf, sizeof(buf));
  if (translated) *translated = TRUE;

  if (issigned) return atoi(buf);
  else return (int)strtoul(buf, NULL, 10);
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

// ===========================================================================
// Coordinate conversion
// ===========================================================================

void ClientToScreen(HWND hwnd, POINT *p)
{
  if (!hwnd || !p) return;

  // walk up parent chain accumulating offsets
  HWND w = hwnd;
  while (w) {
    // account for NCCALCSIZE
    RECT r = w->m_position;
    NCCALCSIZE_PARAMS ncp = {{{r.left, r.top, r.right, r.bottom}}};
    SendMessage(w, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
    p->x += r.left;
    p->y += r.top;

    w = (HWND)w->m_parent;
  }
}

void ScreenToClient(HWND hwnd, POINT *p)
{
  if (!hwnd || !p) return;

  HWND w = hwnd;
  while (w) {
    RECT r = w->m_position;
    p->x -= r.left;
    p->y -= r.top;
    w = (HWND)w->m_parent;
  }
}

void GetClientRect(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return;

  RECT wr = hwnd->m_position;
  NCCALCSIZE_PARAMS ncp = {{{0, 0, wr.right - wr.left, wr.bottom - wr.top}}};
  SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&ncp);
  *r = ncp.rgrc[0];
}

bool GetWindowRect(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return false;
  *r = hwnd->m_position;
  return true;
}

void GetWindowContentViewRect(HWND hwnd, RECT *r)
{
  if (!hwnd || !r) return;
  r->left = 0;
  r->top = 0;
  r->right = hwnd->m_position.right - hwnd->m_position.left;
  r->bottom = hwnd->m_position.bottom - hwnd->m_position.top;
}

void SetWindowPos(HWND hwnd, HWND unused, int x, int y, int cx, int cy, int flags)
{
  if (!hwnd) return;

  bool moved = false, sized = false;

  if (!(flags & SWP_NOMOVE)) {
    if (hwnd->m_position.left != x || hwnd->m_position.top != y) {
      hwnd->m_position.left = x;
      hwnd->m_position.top = y;
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

  int reposflag = (moved ? 1 : 0) | (sized ? 2 : 0);
  if (reposflag) {
    swell_oswindow_resize(hwnd, reposflag, &hwnd->m_position);
  }

  if (sized) {
    SendMessage(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(cx, cy));
  }
  if (moved) {
    SendMessage(hwnd, WM_MOVE, 0, MAKELPARAM(x, y));
  }

  if (flags & SWP_SHOWWINDOW) {
    ShowWindow(hwnd, SW_SHOW);
  }
}

HWND WindowFromPoint(POINT p)
{
  // search top-level windows for containment
  HWND w = g_swell_top_level_list;
  while (w) {
    RECT r = w->m_position;
    if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) {
      // search children for deeper match
      for (int i = w->m_children.GetSize() - 1; i >= 0; i--) {
        HWND ch = w->m_children.Get(i);
        if (ch && ch->m_visible) {
          RECT cr = ch->m_position;
          if (p.x >= cr.left && p.x < cr.right &&
              p.y >= cr.top && p.y < cr.bottom) {
            return ch;
          }
        }
      }
      return w;
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
  HWND top = hwnd;
  while (top->m_parent) top = (HWND)top->m_parent;

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

    if (h == top && h->m_oswindow) break;

    h = (HWND)h->m_parent;
    if (!h) return FALSE;
  }

  hwnd->m_invalidated = true;

  // WS_CLIPSIBLINGS: invalidate later siblings that intersect us
  {
    HWND t = (HWND)hwnd->m_parent;
    if (t && (t->m_style & WS_CLIPSIBLINGS)) {
      HWND nw = hwnd->m_next;
      while (nw) {
        RECT tmp;
        if (nw->m_visible && !nw->m_invalidated &&
            WinIntersectRect(&tmp, &hwnd->m_position, &nw->m_position))
          nw->m_invalidated = true;
        nw = nw->m_next;
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

  SkCanvas *canvas = top->m_backingstore->getCanvas();
  if (canvas) {
    SWELL_internalSkiaPaint(top, canvas, 0, 0, false);
    swell_oswindow_updatetoscreen(top, NULL);
  }
}

// ===========================================================================
// ScrollWindow
// ===========================================================================

BOOL ScrollWindow(HWND hwnd, int xamt, int yamt,
                  const RECT *lpRect, const RECT *lpClipRect)
{
  if (!hwnd) return FALSE;
  // for headless: invalidate and return
  InvalidateRect(hwnd, NULL, FALSE);
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
  *r = hwnd->m_position;
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

  // convert time_t to 64-bit Windows FILETIME (100ns intervals since 1601)
  unsigned long long t = (unsigned long long)st.st_mtime * 10000000ULL + 116444736000000000ULL;

  if (lpCreationTime) {
    lpCreationTime->dwLowDateTime = (DWORD)t;
    lpCreationTime->dwHighDateTime = (DWORD)(t >> 32);
  }
  if (lpLastAccessTime) {
    lpLastAccessTime->dwLowDateTime = (DWORD)t;
    lpLastAccessTime->dwHighDateTime = (DWORD)(t >> 32);
  }
  if (lpLastWriteTime) {
    lpLastWriteTime->dwLowDateTime = (DWORD)t;
    lpLastWriteTime->dwHighDateTime = (DWORD)(t >> 32);
  }
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
  SWELL_MessageQueue_Flush();

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

  SWELL_RunEvents();
  fireTimers();
}
