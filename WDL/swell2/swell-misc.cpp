/*
  SWELL2 misc module — clipboard, drag-drop, monitors, MessageBox,
  file dialogs, ShellExecute, threads, events, cursors, GUID, rect utils,
  ImageList, module loading, process helpers.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cerrno>

#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#ifdef SWELL_TARGET_SDL3
#include <SDL3/SDL.h>
#endif

// ============================================================================
// Rect utilities
// ============================================================================

BOOL SWELL_PtInRect(const RECT *r, POINT p)
{
  if (!r) return FALSE;
  return (p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom) ? TRUE : FALSE;
}

BOOL WinOffsetRect(LPRECT lprc, int dx, int dy)
{
  if (!lprc) return FALSE;
  lprc->left += dx; lprc->top  += dy;
  lprc->right+= dx; lprc->bottom += dy;
  return TRUE;
}

BOOL WinSetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom)
{
  if (!lprc) return FALSE;
  lprc->left = xLeft; lprc->top = yTop;
  lprc->right = xRight; lprc->bottom = yBottom;
  return TRUE;
}

void WinUnionRect(RECT *out, const RECT *in1, const RECT *in2)
{
  if (!out || !in1 || !in2) return;
  out->left   = in1->left   < in2->left   ? in1->left   : in2->left;
  out->top    = in1->top    < in2->top    ? in1->top    : in2->top;
  out->right  = in1->right  > in2->right  ? in1->right  : in2->right;
  out->bottom = in1->bottom > in2->bottom ? in1->bottom : in2->bottom;
}

int WinIntersectRect(RECT *out, const RECT *in1, const RECT *in2)
{
  if (!out || !in1 || !in2) return 0;
  if (in1->left >= in2->right || in2->left >= in1->right ||
      in1->top >= in2->bottom || in2->top  >= in1->bottom) {
    memset(out, 0, sizeof(RECT));
    return 0;
  }
  out->left   = in1->left   > in2->left   ? in1->left   : in2->left;
  out->top    = in1->top    > in2->top    ? in1->top    : in2->top;
  out->right  = in1->right  < in2->right  ? in1->right  : in2->right;
  out->bottom = in1->bottom < in2->bottom ? in1->bottom : in2->bottom;
  return 1;
}

// ============================================================================
// GUID
// ============================================================================

bool SWELL_GenerateGUID(void *g)
{
  if (!g) return false;
  uint8_t *b = (uint8_t *)g;
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, b, 16);
    close(fd);
    if (n == 16) {
      b[6] = (b[6] & 0x0F) | 0x40;  // version 4
      b[8] = (b[8] & 0x3F) | 0x80;  // variant RFC 4122
      return true;
    }
  }
  // fallback: time+pid based
  struct timeval tv; gettimeofday(&tv, NULL);
  uint32_t seed = (uint32_t)(tv.tv_sec ^ tv.tv_usec ^ getpid());
  for (int i = 0; i < 16; i++) {
    seed = seed * 1664525 + 1013904223;
    b[i] = (uint8_t)(seed >> 16);
  }
  b[6] = (b[6] & 0x0F) | 0x40;
  b[8] = (b[8] & 0x3F) | 0x80;
  return true;
}

// ============================================================================
// Module / Library
// ============================================================================

DWORD GetModuleFileName(HINSTANCE hInst, char *fn, DWORD nSize)
{
  if (!fn || nSize == 0) return 0;
  if (!hInst) {
    // own executable
    ssize_t r = readlink("/proc/self/exe", fn, nSize - 1);
    if (r < 0) { fn[0] = '\0'; return 0; }
    fn[r] = '\0';
    return (DWORD)r;
  }
  // shared library: use dladdr
  Dl_info info;
  if (dladdr((void *)hInst, &info) && info.dli_fname) {
    lstrcpyn(fn, info.dli_fname, (int)nSize);
    return (DWORD)strlen(fn);
  }
  fn[0] = '\0';
  return 0;
}

HINSTANCE LoadLibrary(const char *fileName)
{
  if (!fileName) return NULL;
  return (HINSTANCE)dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
}

HINSTANCE LoadLibraryGlobals(const char *fileName, bool symGlob)
{
  if (!fileName) return NULL;
  int flags = RTLD_NOW | (symGlob ? RTLD_GLOBAL : RTLD_LOCAL);
  return (HINSTANCE)dlopen(fileName, flags);
}

void *GetProcAddress(HINSTANCE hInst, const char *procName)
{
  if (!hInst || !procName) return NULL;
  return dlsym((void *)hInst, procName);
}

BOOL FreeLibrary(HINSTANCE hInst)
{
  if (!hInst) return FALSE;
  return dlclose((void *)hInst) == 0 ? TRUE : FALSE;
}

void *SWELL_GetBundle(HINSTANCE hInst)
{
  return NULL;
}

// ============================================================================
// Message Box (simple stderr + return)
// ============================================================================

int MessageBox(HWND hwndParent, const char *text, const char *caption, int type)
{
  (void)hwndParent;
  fprintf(stderr, "[MessageBox] %s: %s\n",
          caption ? caption : "", text ? text : "");

  // Without a display, just return the default affirmative
  if ((type & 0xF) == MB_YESNO)  return IDYES;
  if ((type & 0xF) == MB_YESNOCANCEL) return IDYES;
  if ((type & 0xF) == MB_RETRYCANCEL) return IDRETRY;
  if ((type & 0xF) == MB_ABORTRETRYIGNORE) return IDIGNORE;
  if ((type & 0xF) == MB_OKCANCEL) return IDOK;
  return IDOK;
}

// ============================================================================
// File dialogs (no real UI — return NULL/false)
// ============================================================================

char *BrowseForFiles(const char *text, const char *initialdir,
                     const char *initialfile, bool allowmul,
                     const char *extlist)
{
  (void)text; (void)initialdir; (void)initialfile; (void)allowmul; (void)extlist;
  return NULL;
}

bool BrowseForSaveFile(const char *text, const char *initialdir,
                       const char *initialfile, const char *extlist,
                       char *fn, int fnsize)
{
  (void)text; (void)initialdir; (void)initialfile; (void)extlist;
  (void)fn; (void)fnsize;
  return false;
}

bool BrowseForDirectory(const char *text, const char *initialdir,
                        char *fn, int fnsize)
{
  (void)text; (void)initialdir; (void)fn; (void)fnsize;
  return false;
}

void BrowseFile_SetTemplate(const char *dlgid, DLGPROC dlgProc,
                             struct SWELL_DialogResourceIndex *reshead)
{
  (void)dlgid; (void)dlgProc; (void)reshead;
}

// ============================================================================
// Shell / process
// ============================================================================

BOOL ShellExecute(HWND hwndDlg, const char *action,
                  const char *content1, const char *content2,
                  const char *content3, int blah)
{
  (void)hwndDlg; (void)action; (void)content2; (void)content3; (void)blah;
  if (!content1) return FALSE;
  // Try xdg-open for "open" action, direct exec otherwise
  pid_t pid = fork();
  if (pid == 0) {
    const char *argv[] = { "xdg-open", content1, NULL };
    execvp("xdg-open", (char *const *)argv);
    // Fallback: try direct
    execl(content1, content1, (char *)NULL);
    _exit(1);
  }
  return pid > 0 ? TRUE : FALSE;
}

void GetTempPath(int sz, char *buf)
{
  if (!buf || sz <= 0) return;
  const char *tmp = getenv("TMPDIR");
  lstrcpyn(buf, tmp ? tmp : "/tmp", sz);
  // ensure trailing slash
  int len = (int)strlen(buf);
  if (len > 0 && buf[len-1] != '/' && len + 1 < sz) {
    buf[len] = '/'; buf[len+1] = '\0';
  }
}

// ============================================================================
// Clipboard — SDL3 backend
// ============================================================================

static bool g_clipboard_open = false;

bool OpenClipboard(HWND hwndDlg)
{
  (void)hwndDlg;
  g_clipboard_open = true;
  return true;
}

void CloseClipboard()
{
  g_clipboard_open = false;
}

void EmptyClipboard()
{
#ifdef SWELL_TARGET_SDL3
  SDL_SetClipboardText("");
#endif
}

HANDLE GetClipboardData(UINT type)
{
  if (type != CF_TEXT) return NULL;
#ifdef SWELL_TARGET_SDL3
  if (!SDL_HasClipboardText()) return NULL;
  const char *txt = SDL_GetClipboardText();
  if (!txt) return NULL;
  size_t len = strlen(txt);
  char *buf = (char *)malloc(len + 1);
  if (!buf) { SDL_free((void*)txt); return NULL; }
  memcpy(buf, txt, len + 1);
  SDL_free((void*)txt);
  return (HANDLE)buf;
#else
  return NULL;
#endif
}

void SetClipboardData(UINT type, HANDLE h)
{
  if (type != CF_TEXT || !h) return;
#ifdef SWELL_TARGET_SDL3
  SDL_SetClipboardText((const char *)h);
#endif
}

static UINT g_next_clipboard_format = 0xC000;  // custom format base

UINT RegisterClipboardFormat(const char *desc)
{
  (void)desc;
  return g_next_clipboard_format++;
}

UINT EnumClipboardFormats(UINT lastfmt)
{
  if (lastfmt == 0) return CF_TEXT;
  return 0;
}

HANDLE GlobalAlloc(int flags, int sz)
{
  (void)flags;
  return (HANDLE)calloc(1, sz > 0 ? sz : 1);
}

void *GlobalLock(HANDLE h)   { return (void *)h; }
int   GlobalSize(HANDLE h)   { (void)h; return 0; }
void  GlobalUnlock(HANDLE h) { (void)h; }
void  GlobalFree(HANDLE h)   { free((void *)h); }

// ============================================================================
// Drag-drop (stubs — no DnD implementation)
// ============================================================================

BOOL DragQueryPoint(HDROP hDrop, LPPOINT pt)
{
  (void)hDrop; (void)pt; return FALSE;
}

void DragFinish(HDROP hDrop)
{
  (void)hDrop;
}

UINT DragQueryFile(HDROP hDrop, UINT iFile, char *buf, UINT cb)
{
  (void)hDrop; (void)iFile; (void)buf; (void)cb; return 0;
}

void SWELL_InitiateDragDrop(HWND hwnd, RECT *srcrect, const char *srcfn,
                             void (*callback)(const char *droppath))
{
  (void)hwnd; (void)srcrect; (void)srcfn; (void)callback;
}

void SWELL_InitiateDragDropOfFileList(HWND hwnd, RECT *srcrect,
                                       const char **srclist, int srccount,
                                       HICON icon)
{
  (void)hwnd; (void)srcrect; (void)srclist; (void)srccount; (void)icon;
}

void SWELL_FinishDragDrop()
{
}

// ============================================================================
// Monitors / screen metrics
// ============================================================================

BOOL EnumDisplayMonitors(HDC hdc, const LPRECT r,
                          MONITORENUMPROC proc, LPARAM lp)
{
  (void)hdc; (void)r;
  if (!proc) return FALSE;
  RECT rc = { 0, 0, 1920, 1080 };
  SWELL_GetViewPort(&rc, NULL, false);
  HMONITOR hmon = (HMONITOR)(intptr_t)1;
  proc(hmon, NULL, &rc, lp);
  return TRUE;
}

BOOL GetMonitorInfo(HMONITOR hMonitor, void *info)
{
  (void)hMonitor;
  if (!info) return FALSE;
  // MONITORINFO: cbSize, rcMonitor, rcWork, dwFlags
  // cbSize is the first DWORD
  RECT vp = { 0, 0, 1920, 1080 };
  SWELL_GetViewPort(&vp, NULL, false);
  struct { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; } *mi =
    (decltype(mi))info;
  mi->rcMonitor = vp;
  mi->rcWork    = vp;
  mi->dwFlags   = 1; // MONITORINFOF_PRIMARY
  return TRUE;
}

int GetSystemMetrics(int idx)
{
  switch (idx) {
    case SM_CXSCREEN:
    case SM_CYSCREEN: {
      RECT r; SWELL_GetViewPort(&r, NULL, false);
      return idx == SM_CXSCREEN ? r.right - r.left : r.bottom - r.top;
    }
    case SM_CXHSCROLL: case SM_CYHSCROLL:
    case SM_CXVSCROLL: case SM_CYVSCROLL:
      return g_swell_ctheme.smscrollbar_width;
    case SM_CYMENU:      return g_swell_ctheme.menubar_height;
    case SM_CYCAPTION:   return 23;
    case SM_CXBORDER:    return 1;
    case SM_CYBORDER:    return 1;
    case SM_CXDLGFRAME:  return 3;
    case SM_CYDLGFRAME:  return 3;
    case SM_CXICON:      return 32;
    case SM_CYICON:      return 32;
    case SM_CXCURSOR:    return 32;
    case SM_CYCURSOR:    return 32;
    case SM_CXFRAME:     return 4;
    case SM_CYFRAME:     return 4;
    default:             return 0;
  }
}

// ============================================================================
// Threading — pthreads backend
// ============================================================================

struct ThreadRec {
  DWORD (*proc)(LPVOID);
  LPVOID parm;
};

static void *thread_thunk(void *arg)
{
  ThreadRec *tr = (ThreadRec *)arg;
  DWORD (*fn)(LPVOID) = tr->proc;
  LPVOID p = tr->parm;
  free(tr);
  fn(p);
  return NULL;
}

HANDLE CreateThread(void *TA, DWORD stackSize, DWORD (*ThreadProc)(LPVOID),
                    LPVOID parm, DWORD cf, DWORD *tidOut)
{
  (void)TA; (void)stackSize; (void)cf; (void)tidOut;
  if (!ThreadProc) return NULL;
  ThreadRec *tr = (ThreadRec *)malloc(sizeof(ThreadRec));
  tr->proc = ThreadProc;
  tr->parm = parm;
  pthread_t *t = (pthread_t *)malloc(sizeof(pthread_t));
  if (pthread_create(t, NULL, thread_thunk, tr) != 0) {
    free(tr); free(t); return NULL;
  }
  pthread_detach(*t);
  return (HANDLE)t;
}

DWORD GetCurrentThreadId()
{
  return (DWORD)(uintptr_t)pthread_self();
}

BOOL SetThreadPriority(HANDLE evt, int prio)
{
  (void)evt; (void)prio; return TRUE;
}

BOOL CloseHandle(HANDLE hand)
{
  if (!hand) return FALSE;
  // Handles are either pthread_t* (from CreateThread, already detached)
  // or eventfd descriptors wrapped in EventHandle. Check tag byte.
  free(hand);
  return TRUE;
}

// ---- Simple event using pipe ----
struct EventHandle {
  int rd, wr;
  bool manual_reset;
};

HANDLE CreateEvent(void *SA, BOOL manualReset, BOOL initialSig,
                   const char *ignored)
{
  (void)SA; (void)ignored;
  int fd[2];
  if (pipe(fd) != 0) return NULL;
  fcntl(fd[0], F_SETFL, O_NONBLOCK);
  fcntl(fd[1], F_SETFL, O_NONBLOCK);
  EventHandle *ev = (EventHandle *)malloc(sizeof(EventHandle));
  ev->rd = fd[0];
  ev->wr = fd[1];
  ev->manual_reset = manualReset != FALSE;
  if (initialSig) {
    char b = 1;
    write(ev->wr, &b, 1);
  }
  return (HANDLE)ev;
}

HANDLE CreateEventAsSocket(void *SA, BOOL manualReset, BOOL initialSig,
                            const char *ignored)
{
  return CreateEvent(SA, manualReset, initialSig, ignored);
}

BOOL SetEvent(HANDLE evt)
{
  EventHandle *ev = (EventHandle *)evt;
  if (!ev) return FALSE;
  char b = 1;
  write(ev->wr, &b, 1);
  return TRUE;
}

BOOL ResetEvent(HANDLE evt)
{
  EventHandle *ev = (EventHandle *)evt;
  if (!ev) return FALSE;
  char buf[256];
  while (read(ev->rd, buf, sizeof(buf)) > 0) {}
  return TRUE;
}

DWORD WaitForSingleObject(HANDLE hand, DWORD msTO)
{
  EventHandle *ev = (EventHandle *)hand;
  if (!ev) return (DWORD)WAIT_FAILED;

  struct timeval tv;
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(ev->rd, &fds);

  if (msTO == INFINITE) {
    int r = select(ev->rd + 1, &fds, NULL, NULL, NULL);
    if (r <= 0) return (DWORD)WAIT_FAILED;
  } else {
    tv.tv_sec  = msTO / 1000;
    tv.tv_usec = (msTO % 1000) * 1000;
    int r = select(ev->rd + 1, &fds, NULL, NULL, &tv);
    if (r < 0)  return (DWORD)WAIT_FAILED;
    if (r == 0) return (DWORD)WAIT_TIMEOUT;
  }

  char b;
  if (read(ev->rd, &b, 1) <= 0) return (DWORD)WAIT_FAILED;
  if (ev->manual_reset) {
    // drain all
    char buf[256];
    while (read(ev->rd, buf, sizeof(buf)) > 0) {}
    // re-signal for other waiters? For manual-reset: keep drained until ResetEvent
  }
  return WAIT_OBJECT_0;
}

DWORD WaitForAnySocketObject(int numObjs, HANDLE *objs, DWORD msTO)
{
  if (numObjs <= 0 || !objs) return (DWORD)WAIT_FAILED;
  fd_set fds;
  FD_ZERO(&fds);
  int maxfd = 0;
  for (int i = 0; i < numObjs; i++) {
    EventHandle *ev = (EventHandle *)objs[i];
    if (!ev) continue;
    FD_SET(ev->rd, &fds);
    if (ev->rd > maxfd) maxfd = ev->rd;
  }
  struct timeval tv = { (long)(msTO / 1000), (long)((msTO % 1000) * 1000) };
  int r = select(maxfd + 1, &fds, NULL, NULL,
                 msTO == INFINITE ? NULL : &tv);
  if (r <= 0) return (r == 0) ? (DWORD)WAIT_TIMEOUT : (DWORD)WAIT_FAILED;
  for (int i = 0; i < numObjs; i++) {
    EventHandle *ev = (EventHandle *)objs[i];
    if (ev && FD_ISSET(ev->rd, &fds)) {
      char b; read(ev->rd, &b, 1);
      return WAIT_OBJECT_0 + i;
    }
  }
  return (DWORD)WAIT_FAILED;
}

// ============================================================================
// Process
// ============================================================================

HANDLE SWELL_CreateProcess(const char *exe, int nparams, const char **params)
{
  if (!exe) return NULL;
  const char **argv = (const char **)malloc(sizeof(char *) * (nparams + 2));
  argv[0] = exe;
  for (int i = 0; i < nparams; i++) argv[i+1] = params[i];
  argv[nparams+1] = NULL;

  pid_t pid = fork();
  if (pid < 0) { free(argv); return NULL; }
  if (pid == 0) {
    execvp(exe, (char *const *)argv);
    _exit(1);
  }
  free(argv);
  return (HANDLE)(intptr_t)pid;
}

int SWELL_GetProcessExitCode(HANDLE hand)
{
  pid_t pid = (pid_t)(intptr_t)hand;
  if (pid <= 0) return -1;
  int status;
  pid_t r = waitpid(pid, &status, WNOHANG);
  if (r == 0) return -1;  // still running
  if (r < 0)  return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

// ============================================================================
// Mouse / Keyboard / Cursor
// ============================================================================

void GetCursorPos(POINT *pt)
{
  if (!pt) return;
#ifdef SWELL_TARGET_SDL3
  float mx, my;
  SDL_GetGlobalMouseState(&mx, &my);
  pt->x = (int)mx;
  pt->y = (int)my;
#else
  pt->x = 0; pt->y = 0;
#endif
}

DWORD GetMessagePos()
{
#ifdef SWELL_TARGET_SDL3
  float mx, my;
  SDL_GetGlobalMouseState(&mx, &my);
  return MAKELPARAM((int)mx, (int)my);
#else
  return 0;
#endif
}

WORD GetAsyncKeyState(int key)
{
#ifdef SWELL_TARGET_SDL3
  const bool *state = SDL_GetKeyboardState(NULL);
  if (!state) return 0;
  SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
  switch (key) {
    case VK_SHIFT:   sc = SDL_SCANCODE_LSHIFT;   break;
    case VK_CONTROL: sc = SDL_SCANCODE_LCTRL;    break;
    case VK_MENU:    sc = SDL_SCANCODE_LALT;     break;
    case VK_LWIN:    sc = SDL_SCANCODE_LGUI;     break;
    case VK_RETURN:  sc = SDL_SCANCODE_RETURN;   break;
    case VK_ESCAPE:  sc = SDL_SCANCODE_ESCAPE;   break;
    case VK_SPACE:   sc = SDL_SCANCODE_SPACE;    break;
    case VK_TAB:     sc = SDL_SCANCODE_TAB;      break;
    case VK_BACK:    sc = SDL_SCANCODE_BACKSPACE; break;
    case VK_DELETE:  sc = SDL_SCANCODE_DELETE;   break;
    case VK_LEFT:    sc = SDL_SCANCODE_LEFT;     break;
    case VK_RIGHT:   sc = SDL_SCANCODE_RIGHT;    break;
    case VK_UP:      sc = SDL_SCANCODE_UP;       break;
    case VK_DOWN:    sc = SDL_SCANCODE_DOWN;     break;
    default:
      if (key >= 'A' && key <= 'Z')
        sc = (SDL_Scancode)(SDL_SCANCODE_A + (key - 'A'));
      else if (key >= '0' && key <= '9')
        sc = (SDL_Scancode)(SDL_SCANCODE_0 + (key - '0'));
      break;
  }
  if (sc != SDL_SCANCODE_UNKNOWN && state[sc]) return 0x8000;
#endif
  return 0;
}

// ---- Cursor ----

struct CursorEntry {
  char  *id;
  char  *name;
  int    hotspot_x, hotspot_y;
#ifdef SWELL_TARGET_SDL3
  SDL_Cursor *cursor;
#endif
  CursorEntry *next;
};

static CursorEntry *g_cursor_list = NULL;
static HCURSOR g_current_cursor = NULL;

void SWELL_Register_Cursor_Resource(const char *idx, const char *name,
                                     int hotspot_x, int hotspot_y)
{
  CursorEntry *e = (CursorEntry *)calloc(1, sizeof(CursorEntry));
  e->id        = idx  ? strdup(idx)  : NULL;
  e->name      = name ? strdup(name) : NULL;
  e->hotspot_x = hotspot_x;
  e->hotspot_y = hotspot_y;
  e->next = g_cursor_list;
  g_cursor_list = e;
}

static SDL_SystemCursor idcname_to_sdl(const char *idx)
{
  if (!idx) return SDL_SYSTEM_CURSOR_DEFAULT;
  if ((size_t)idx <= 0xFFFF) {
    int id = (int)(size_t)idx;
    switch (id) {
      case 32512: return SDL_SYSTEM_CURSOR_DEFAULT;    // IDC_ARROW
      case 32513: return SDL_SYSTEM_CURSOR_TEXT;       // IDC_IBEAM
      case 32514: return SDL_SYSTEM_CURSOR_WAIT;       // IDC_WAIT
      case 32515: return SDL_SYSTEM_CURSOR_CROSSHAIR;  // IDC_CROSS
      case 32516: return SDL_SYSTEM_CURSOR_PROGRESS;   // IDC_UPARROW
      case 32640: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;// IDC_SIZENWSE
      case 32641: return SDL_SYSTEM_CURSOR_NESW_RESIZE;// IDC_SIZENESW
      case 32642: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;// IDC_SIZENWSE
      case 32643: return SDL_SYSTEM_CURSOR_NESW_RESIZE;// IDC_SIZENESW
      case 32644: return SDL_SYSTEM_CURSOR_EW_RESIZE;  // IDC_SIZEWE
      case 32645: return SDL_SYSTEM_CURSOR_NS_RESIZE;  // IDC_SIZENS
      case 32646: return SDL_SYSTEM_CURSOR_MOVE;       // IDC_SIZEALL
      case 32648: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;// IDC_NO
      case 32649: return SDL_SYSTEM_CURSOR_POINTER;    // IDC_HAND
      case 32650: return SDL_SYSTEM_CURSOR_PROGRESS;   // IDC_APPSTARTING
      default:    return SDL_SYSTEM_CURSOR_DEFAULT;
    }
  }
  return SDL_SYSTEM_CURSOR_DEFAULT;
}

HCURSOR SWELL_LoadCursor(const char *idx)
{
#ifdef SWELL_TARGET_SDL3
  // Check registered cursors
  for (CursorEntry *e = g_cursor_list; e; e = e->next) {
    bool match = false;
    if ((size_t)e->id <= 0xFFFF && (size_t)idx <= 0xFFFF)
      match = (e->id == idx);
    else if (e->id && (size_t)idx > 0xFFFF)
      match = (strcmp(e->id, idx) == 0);
    if (match) {
      if (!e->cursor) {
        SDL_SystemCursor sc = idcname_to_sdl(idx);
        e->cursor = SDL_CreateSystemCursor(sc);
      }
      return (HCURSOR)e;
    }
  }
  // System cursor by integer ID
  SDL_SystemCursor sc = idcname_to_sdl(idx);
  SDL_Cursor *c = SDL_CreateSystemCursor(sc);
  return (HCURSOR)c;
#else
  (void)idx; return NULL;
#endif
}

HCURSOR SWELL_LoadCursorFromFile(const char *fn)
{
  (void)fn; return NULL;
}

void SWELL_SetCursor(HCURSOR curs)
{
  g_current_cursor = curs;
#ifdef SWELL_TARGET_SDL3
  if (!curs) {
    SDL_ShowCursor();
    SDL_SetCursor(SDL_GetDefaultCursor());
    return;
  }
  // Cursor might be a CursorEntry* or SDL_Cursor*; try both
  CursorEntry *ce = (CursorEntry *)curs;
  bool found = false;
  for (CursorEntry *e = g_cursor_list; e; e = e->next) {
    if (e == ce) { found = true; break; }
  }
  if (found && ce->cursor) {
    SDL_SetCursor(ce->cursor);
  } else {
    SDL_SetCursor((SDL_Cursor *)curs);
  }
  SDL_ShowCursor();
#endif
}

HCURSOR SWELL_GetCursor()             { return g_current_cursor; }
HCURSOR SWELL_GetLastSetCursor()      { return g_current_cursor; }
bool    SWELL_IsCursorVisible()       { return true; }
BOOL    SWELL_SetCursorPos(int X, int Y)
{
#ifdef SWELL_TARGET_SDL3
  SDL_WarpMouseGlobal((float)X, (float)Y);
  return TRUE;
#else
  (void)X; (void)Y; return FALSE;
#endif
}

int SWELL_ShowCursor(BOOL bShow)
{
#ifdef SWELL_TARGET_SDL3
  if (bShow) SDL_ShowCursor();
  else       SDL_HideCursor();
#endif
  return bShow ? 1 : 0;
}

// ============================================================================
// ImageList
// ============================================================================

HIMAGELIST ImageList_CreateEx()
{
  return (HIMAGELIST) new HIMAGELIST__();
}

BOOL ImageList_Remove(HIMAGELIST list, int idx)
{
  (void)list; (void)idx; return FALSE;
}

int ImageList_ReplaceIcon(HIMAGELIST list, int offset, HICON image)
{
  (void)list; (void)offset; (void)image; return -1;
}

int ImageList_Add(HIMAGELIST list, HBITMAP image, HBITMAP mask)
{
  (void)list; (void)image; (void)mask; return -1;
}

void ImageList_Destroy(HIMAGELIST list)
{
  delete list;
}

// ============================================================================
// Extended API / Misc
// ============================================================================

void *SWELL_ExtendedAPI(const char *key, void *v)
{
  (void)key; (void)v; return NULL;
}

unsigned int _controlfp(unsigned int flag, unsigned int mask)
{
  (void)flag; (void)mask; return 0;
}

void SWELL_HideApp()
{
}

BOOL SWELL_GetGestureInfo(LPARAM lParam, GESTUREINFO *gi)
{
  (void)lParam; (void)gi; return FALSE;
}

bool SWELL_ChooseColor(HWND hwnd, COLORREF *color, int ncustom,
                       COLORREF *custom)
{
  (void)hwnd; (void)color; (void)ncustom; (void)custom; return false;
}

bool SWELL_ChooseFont(HWND hwnd, LOGFONT *lf)
{
  (void)hwnd; (void)lf; return false;
}

void SetOpaque(HWND h, bool isopaque)           { (void)h; (void)isopaque; }
void SetAllowNoMiddleManRendering(HWND h, bool allow) { (void)h; (void)allow; }
void SWELL_SetViewGL(HWND h, char wantGL)       { (void)h; (void)wantGL; }
bool SWELL_GetViewGL(HWND h)                    { (void)h; return false; }
bool SWELL_SetGLContextToView(HWND h)           { (void)h; return false; }

// ============================================================================
// Non-OSX only
// ============================================================================

#ifndef SWELL_TARGET_OSX
HANDLE SWELL_CreateProcessFromPID(int pid)
{
  return (HANDLE)(intptr_t)pid;
}
#endif
