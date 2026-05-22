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
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
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

extern "C" void *SWELLAPI_GetFunc(const char *);

static void swell_call_dll_main(void *h)
{
  typedef int (*swell_dll_main_t)(void *, int, void *(*)(const char *));
  swell_dll_main_t swell_init = (swell_dll_main_t)dlsym(h, "SWELL_dllMain");
  if (swell_init)
    swell_init(h, 1 /* DLL_PROCESS_ATTACH */, SWELLAPI_GetFunc);
}

HINSTANCE LoadLibrary(const char *fileName)
{
  if (!fileName) return NULL;
  void *h = dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
  if (!h) return NULL;
  swell_call_dll_main(h);
  return (HINSTANCE)h;
}

HINSTANCE LoadLibraryGlobals(const char *fileName, bool symGlob)
{
  if (!fileName) return NULL;
  int flags = RTLD_NOW | (symGlob ? RTLD_GLOBAL : RTLD_LOCAL);
  void *h = dlopen(fileName, flags);
  if (!h) return NULL;
  swell_call_dll_main(h);
  return (HINSTANCE)h;
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
// Message Box — swell-based modal dialog
// ============================================================================

static const char *mbidtostr(int idx)
{
  switch (idx) {
    case IDOK:     return "OK";
    case IDCANCEL: return "Cancel";
    case IDYES:    return "Yes";
    case IDNO:     return "No";
    case IDRETRY:  return "Retry";
    case IDABORT:  return "Abort";
    case IDIGNORE: return "Ignore";
  }
  return "";
}

struct MessageBoxParams {
  const char *text;
  const char *caption;
  int type;
  int default_id;
  int buttons[3];
  int nbuttons;
};

enum { IDC_MSGBOX_LABEL = 0x100 };

static INT_PTR swellMessageBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {
    case WM_CREATE: {
      MessageBoxParams *p = (MessageBoxParams *)lParam;
      if (!p) break;

      if (p->caption) SetWindowText(hwnd, p->caption);

      SWELL_MakeSetCurParms(1.0f, 1.0f, 0.0f, 0.0f, hwnd, false, false);

      RECT labsize = {0, 0, SWELL_UI_SCALE(500), SWELL_UI_SCALE(20)};
      HWND lab = SWELL_MakeLabel(-1, p->text ? p->text : "", IDC_MSGBOX_LABEL,
                                 0, 0, 10, 10, SS_CENTER | SS_NOPREFIX);
      HDC dc = NULL;
      if (lab) {
        dc = GetDC(lab);
        if (dc && p->text)
          DrawText(dc, p->text, -1, &labsize, DT_CALCRECT | DT_NOPREFIX);
      }

      const int sc10 = SWELL_UI_SCALE(10);
      const int sc8  = SWELL_UI_SCALE(8);
      const int vpad = sc10 + sc8;
      labsize.top += vpad;
      labsize.bottom += vpad;

      {
        RECT vp;
        SWELL_GetViewPort(&vp, NULL, true);
        const int maxh = (vp.bottom - vp.top) * 7 / 8;
        if (labsize.bottom > maxh) labsize.bottom = maxh;
      }

      const int bspace = SWELL_UI_SCALE(8);
      const int btn_pad_w = g_swell_theme.padding_button_h * 2;
      const int btn_pad_h = g_swell_theme.padding_button_v * 2;
      const int btn_min_w = SWELL_UI_SCALE(60);
      int button_sizes[3];
      int button_height = 0, button_total_w = 0;
      for (int i = 0; i < p->nbuttons; i++) {
        RECT r = {0, 0, 35, 12};
        if (dc)
          DrawText(dc, mbidtostr(p->buttons[i]), -1, &r,
                   DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
        int bw = (r.right - r.left) + btn_pad_w;
        if (bw < btn_min_w) bw = btn_min_w;
        button_sizes[i] = bw;
        button_total_w += button_sizes[i] + (i ? bspace : 0);
        const int bh = (r.bottom - r.top) + btn_pad_h;
        if (bh > button_height) button_height = bh;
      }
      if (button_height < g_swell_theme.button_min_h)
        button_height = g_swell_theme.button_min_h;

      if (dc && lab) ReleaseDC(lab, dc);

      if (labsize.right < SWELL_UI_SCALE(280))
        labsize.right = SWELL_UI_SCALE(280);
      if (labsize.right < button_total_w + sc8 * 2)
        labsize.right = button_total_w + sc8 * 2;

      int xpos = (labsize.right + sc8 * 2) / 2 - button_total_w / 2;
      for (int i = 0; i < p->nbuttons; i++) {
        const int bid = p->buttons[i];
        SWELL_MakeButton(bid == p->default_id, mbidtostr(bid), bid,
                         xpos, vpad + labsize.bottom,
                         button_sizes[i], button_height, 0);
        xpos += button_sizes[i] + bspace;
      }

      SWELL_MakeSetCurParms(1.0f, 1.0f, 0.0f, 0.0f, NULL, false, false);

      SetWindowPos(hwnd, NULL, 0, 0,
                   labsize.right + sc8 * 2,
                   vpad + labsize.bottom + button_height + sc8,
                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);

      if (lab)
        SetWindowPos(lab, NULL, sc8, vpad, labsize.right, labsize.bottom,
                     SWP_NOACTIVATE | SWP_NOZORDER);

      SetFocus(GetDlgItem(hwnd, p->default_id));
      break;
    }

    case WM_COMMAND:
      if (LOWORD(wParam) && HIWORD(wParam) == BN_CLICKED)
        EndDialog(hwnd, LOWORD(wParam));
      break;

    case WM_CLOSE:
      if (GetDlgItem(hwnd, IDCANCEL))
        EndDialog(hwnd, IDCANCEL);
      else if (GetDlgItem(hwnd, IDNO))
        EndDialog(hwnd, IDNO);
      else
        EndDialog(hwnd, IDOK);
      break;
  }
  return 0;
}

int MessageBox(HWND hwndParent, const char *text, const char *caption, int type)
{
#ifdef SWELL_TARGET_SDL3
  int btntype = type & 0xF;

  MessageBoxParams p;
  p.text = text;
  p.caption = caption;
  p.type = type;

  switch (btntype) {
    case MB_OKCANCEL:
      p.buttons[0] = IDOK; p.buttons[1] = IDCANCEL;
      p.nbuttons = 2; p.default_id = IDOK;
      break;
    case MB_YESNO:
      p.buttons[0] = IDYES; p.buttons[1] = IDNO;
      p.nbuttons = 2; p.default_id = IDYES;
      break;
    case MB_YESNOCANCEL:
      p.buttons[0] = IDYES; p.buttons[1] = IDNO; p.buttons[2] = IDCANCEL;
      p.nbuttons = 3; p.default_id = IDYES;
      break;
    case MB_RETRYCANCEL:
      p.buttons[0] = IDRETRY; p.buttons[1] = IDCANCEL;
      p.nbuttons = 2; p.default_id = IDRETRY;
      break;
    case MB_ABORTRETRYIGNORE:
      p.buttons[0] = IDABORT; p.buttons[1] = IDRETRY; p.buttons[2] = IDIGNORE;
      p.nbuttons = 3; p.default_id = IDABORT;
      break;
    default: // MB_OK
      p.buttons[0] = IDOK;
      p.nbuttons = 1; p.default_id = IDOK;
      break;
  }

  if ((type & MB_DEFBUTTON3) && p.nbuttons >= 3)
    p.default_id = p.buttons[2];
  else if ((type & MB_DEFBUTTON2) && p.nbuttons >= 2)
    p.default_id = p.buttons[1];

  HWND owner = NULL;
  if (hwndParent) {
    HWND top = hwndParent;
    while (top->m_parent) top = (HWND)top->m_parent;
    owner = top;
  }

  int sx = GetSystemMetrics(SM_CXSCREEN);
  int sy = GetSystemMetrics(SM_CYSCREEN);
  int dlg_w = SWELL_UI_SCALE(400);
  int dlg_h = SWELL_UI_SCALE(250);
  int dlg_x = (sx - dlg_w) / 2;
  int dlg_y = (sy - dlg_h) / 2;
  if (dlg_x < 10) dlg_x = 10;
  if (dlg_y < 10) dlg_y = 10;

  RECT r = { dlg_x, dlg_y, dlg_x + dlg_w, dlg_y + dlg_h };

  DWORD style = WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  HWND hwndDlg = new HWND__(NULL, 0, &r,
                             caption ? caption : "",
                             false, SwellDialogDefaultWindowProc);
  hwndDlg->m_style = style;
  hwndDlg->m_dlgproc = swellMessageBoxProc;

  if (owner) {
    hwndDlg->m_owner = owner;
    owner->m_owned.Add(hwndDlg);
  }

  // Save and restore g_dlg_parent around WM_CREATE — if the dialog
  // is destroyed during creation, g_dlg_parent would dangle
  HWND saved_dlg_parent = g_dlg_parent;
  swellMessageBoxProc(hwndDlg, WM_CREATE, 0, (LPARAM)&p);
  g_dlg_parent = saved_dlg_parent;
  if (hwndDlg->m_hashaddestroy >= 2)
    return p.default_id;

  hwndDlg->Retain();

  RECT wr;
  GetWindowRect(hwndDlg, &wr);
  dlg_x = (sx - (wr.right - wr.left)) / 2;
  dlg_y = (sy - (wr.bottom - wr.top)) / 2;
  if (dlg_x < 10) dlg_x = 10;
  if (dlg_y < 10) dlg_y = 10;
  SetWindowPos(hwndDlg, NULL, dlg_x, dlg_y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

  ShowWindow(hwndDlg, SW_SHOW);

  void *ctx = SWELL_ModalWindowStart(hwndDlg);

  WDL_PtrList<HWND__> disabled_list;
  for (HWND w = g_swell_top_level_list; w; w = w->m_next) {
    if (w != hwndDlg && w->m_enabled && !w->m_parent) {
      EnableWindow(w, FALSE);
      disabled_list.Add(w);
    }
  }

  int ret = p.default_id;
  while (SWELL_ModalWindowRun(ctx, &ret))
    usleep(10000);

  for (int i = 0; i < disabled_list.GetSize(); i++) {
    HWND w = disabled_list.Get(i);
    if (w && w->m_hashaddestroy < 2)
      EnableWindow(w, TRUE);
  }

  SWELL_ModalWindowEnd(ctx);
  hwndDlg->Release();

  return ret;

#else
  (void)hwndParent;
  fprintf(stderr, "[MessageBox] %s: %s\n",
          caption ? caption : "", text ? text : "");
  int btntype = type & 0xF;
  if (btntype == MB_YESNO || btntype == MB_YESNOCANCEL) return IDYES;
  if (btntype == MB_RETRYCANCEL) return IDRETRY;
  if (btntype == MB_ABORTRETRYIGNORE) return IDIGNORE;
  if (btntype == MB_OKCANCEL) return IDOK;
  return IDOK;
#endif
}

// ============================================================================
// File dialogs (zenity-based)
// ============================================================================

// Append a zenity --file-filter arg from a Win32 null-null extlist
// ("Description\0*.ext\0...") into cmd buffer at offset *pos.
static void append_zenity_filters(char *cmd, int cmdsz, int *pos,
                                   const char *extlist)
{
  if (!extlist || !extlist[0]) return;
  const char *p = extlist;
  while (p && *p) {
    const char *desc = p;
    const char *pat  = p + strlen(p) + 1;
    if (!*pat) break;
    // Escape double quotes in desc and pat to avoid shell injection
    char edesc[512], epat[512];
    const char *s;
    char *d;
    for (s = desc, d = edesc; *s && d < edesc + sizeof(edesc) - 2; s++) {
      if (*s == '"') *d++ = '\\';
      *d++ = *s;
    }
    *d = 0;
    for (s = pat, d = epat; *s && d < epat + sizeof(epat) - 2; s++) {
      if (*s == '"') *d++ = '\\';
      *d++ = *s;
    }
    *d = 0;
    // format: --file-filter="desc | *.ext *.ext2"
    int n = snprintf(cmd + *pos, cmdsz - *pos,
                     " --file-filter=\"%s | %s\"", edesc, epat);
    if (n > 0) { *pos += n; if (*pos >= cmdsz) *pos = cmdsz - 1; }
    p = pat + strlen(pat) + 1;
  }
}

// Run zenity and collect output. Returns newly malloc'd string (strip trailing
// newline) or NULL on cancel/error. Caller frees.
static char *zenity_run(const char *cmd)
{
  FILE *f = popen(cmd, "r");
  if (!f) return NULL;
  char buf[4096] = {};
  size_t tot = 0;
  while (tot < sizeof(buf) - 1) {
    size_t n = fread(buf + tot, 1, sizeof(buf) - 1 - tot, f);
    if (n == 0) break;
    tot += n;
  }
  int ret = pclose(f);
  if (ret != 0 || tot == 0) return NULL;
  // Strip trailing newline
  while (tot > 0 && (buf[tot-1] == '\n' || buf[tot-1] == '\r')) buf[--tot] = 0;
  return tot > 0 ? strdup(buf) : NULL;
}

// Escape a user-provided string for safe use inside a double-quoted shell argument.
// Replaces " $ ` \ with their backslash-escaped forms.
static void shell_escape_arg(WDL_FastString &out, const char *s)
{
  if (!s) return;
  while (*s) {
    if (*s == '"' || *s == '$' || *s == '`' || *s == '\\')
      out.Append("\\", 1);
    out.Append(s, 1);
    s++;
  }
}

char *BrowseForFiles(const char *text, const char *initialdir,
                     const char *initialfile, bool allowmul,
                     const char *extlist)
{
  WDL_FastString esc_text;
  shell_escape_arg(esc_text, text);

  char cmd[4096];
  int pos = snprintf(cmd, sizeof(cmd), "zenity --file-selection");
  if (pos < 0 || pos >= (int)sizeof(cmd)) pos = (int)sizeof(cmd) - 1;
  if (esc_text.GetLength()) {
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --title=\"%s\"", esc_text.Get());
    if (pos >= (int)sizeof(cmd)) pos = (int)sizeof(cmd) - 1;
  }

  {
    // Build --filename: combine initialdir + initialfile when both provided.
    // Skip initialfile starting with '.' (SWELL default-extension convention).
    const bool have_file = (initialfile && initialfile[0] && initialfile[0] != '.');
    const bool have_dir  = (initialdir  && initialdir[0]);
    WDL_FastString combo, escaped_fn;

    if (have_file) {
      const char *slash = strrchr(initialfile, '/');
      if (!slash && have_dir) {
        combo.Set(initialdir);
        if (combo.Get()[combo.GetLength()-1] != '/') combo.Append("/", 1);
        combo.Append(initialfile);
      } else {
        combo.Set(initialfile);
      }
    } else if (have_dir) {
      combo.Set(initialdir);
      combo.Append("/", 1);
    }

    if (combo.GetLength()) {
      shell_escape_arg(escaped_fn, combo.Get());
      pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --filename=\"%s\"", escaped_fn.Get());
      if (pos >= (int)sizeof(cmd)) pos = (int)sizeof(cmd) - 1;
    }
  }

  if (allowmul) {
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --multiple --separator=|");
    if (pos >= (int)sizeof(cmd)) pos = (int)sizeof(cmd) - 1;
  }
  append_zenity_filters(cmd, sizeof(cmd), &pos, extlist);
  pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>/dev/null");
  if (pos >= (int)sizeof(cmd)) pos = (int)sizeof(cmd) - 1;

  char *raw = zenity_run(cmd);
  if (!raw) return NULL;

  if (!allowmul || !strchr(raw, '|')) {
    // Single file (or single file selected in multi mode): return full
    // path with double-null termination. Matches original SWELL behavior
    // where single selection in OPENMULTI mode takes the else-branch
    // and returns calloc(l+2) for the complete path.
    size_t len = strlen(raw);
    char *out = (char *)malloc(len + 2);
    if (!out) { free(raw); return NULL; }
    memcpy(out, raw, len + 1);
    out[len + 1] = '\0';
    free(raw);
    return out;
  }

  // Multi-file: convert '|'-separated full paths to Win32 allowmul=1 format:
  // "dir/file1|dir/file2" -> "dir/\0file1\0file2\0\0"
  size_t len = strlen(raw);

  // Find directory prefix of first entry (up to and including last '/')
  char *last_slash = NULL;
  for (size_t i = 0; i < len && raw[i] != '|'; i++)
    if (raw[i] == '/') last_slash = raw + i;
  size_t dir_len = last_slash ? (size_t)(last_slash - raw + 1) : 0;

  char *out = (char *)malloc(len + 2);
  if (!out) { free(raw); return NULL; }

  // Copy directory prefix
  if (dir_len > 0) memcpy(out, raw, dir_len);
  size_t wpos = dir_len;

  // Process each pipe-separated entry, stripping common dir prefix
  const char *p = raw;
  while (*p) {
    const char *end = strchr(p, '|');
    size_t seg_len = end ? (size_t)(end - p) : strlen(p);

    if (dir_len > 0 && seg_len > dir_len && !memcmp(p, raw, dir_len))
      memcpy(out + wpos, p + dir_len, seg_len - dir_len);
    else
      memcpy(out + wpos, p, seg_len);
    wpos += (dir_len > 0 && seg_len > dir_len && !memcmp(p, raw, dir_len))
              ? (seg_len - dir_len) : seg_len;
    out[wpos++] = '\0';

    if (!end) break;
    p = end + 1;
  }
  out[wpos] = '\0';  // double-null terminator
  free(raw);
  return out;
}

bool BrowseForSaveFile(const char *text, const char *initialdir,
                       const char *initialfile, const char *extlist,
                       char *fn, int fnsize)
{
  WDL_FastString esc_text;
  shell_escape_arg(esc_text, text);

  char cmd[4096];
  int pos = snprintf(cmd, sizeof(cmd), "zenity --file-selection --save --confirm-overwrite");
  if (esc_text.GetLength())
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --title=\"%s\"", esc_text.Get());

  {
    // Build --filename: combine initialdir + initialfile when both provided.
    // Keep initialfile starting with '.' (SWELL default-extension hint for save).
    const bool have_file = (initialfile && initialfile[0]);
    const bool have_dir  = (initialdir  && initialdir[0]);
    WDL_FastString combo, escaped_fn;

    if (have_file) {
      const char *slash = strrchr(initialfile, '/');
      if (!slash && have_dir) {
        combo.Set(initialdir);
        if (combo.Get()[combo.GetLength()-1] != '/') combo.Append("/", 1);
        combo.Append(initialfile);
      } else {
        combo.Set(initialfile);
      }
    } else if (have_dir) {
      combo.Set(initialdir);
      combo.Append("/", 1);
    }

    if (combo.GetLength()) {
      shell_escape_arg(escaped_fn, combo.Get());
      pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --filename=\"%s\"", escaped_fn.Get());
    }
  }
  append_zenity_filters(cmd, sizeof(cmd), &pos, extlist);
  pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>/dev/null");

  char *raw = zenity_run(cmd);
  if (!raw) return false;
  snprintf(fn, fnsize, "%s", raw);
  free(raw);
  return true;
}

bool BrowseForDirectory(const char *text, const char *initialdir,
                        char *fn, int fnsize)
{
  WDL_FastString esc_text, esc_dir;
  shell_escape_arg(esc_text, text);
  shell_escape_arg(esc_dir, initialdir);

  char cmd[4096];
  int pos = snprintf(cmd, sizeof(cmd), "zenity --file-selection --directory");
  if (esc_text.GetLength())
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --title=\"%s\"", esc_text.Get());
  if (esc_dir.GetLength())
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " --filename=\"%s/\"", esc_dir.Get());
  pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 2>/dev/null");

  char *raw = zenity_run(cmd);
  if (!raw) return false;
  snprintf(fn, fnsize, "%s", raw);
  free(raw);
  return true;
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
  (void)hwndDlg; (void)action; (void)blah;
  if (!content1) return FALSE;

  // Auto-reap zombies: only install handler once (not on every call)
  {
    static bool s_sigchld_installed = false;
    if (!s_sigchld_installed) {
      s_sigchld_installed = true;
      signal(SIGCHLD, SIG_IGN);
    }
  }

  // Detect URL scheme (http://, https://, ftp://, mailto:, etc.)
  if (strstr(content1, "://") || !strncmp(content1, "mailto:", 7)) {
    const char *argv[] = { "xdg-open", content1, content2 && content2[0] ? content2 : NULL, NULL };
    pid_t pid = fork();
    if (pid == 0) { execvp("xdg-open", (char *const *)argv); _exit(1); }
    return pid > 0;
  }

  // explorer.exe → open containing directory (or xdg-open the path)
  if (strstr(content1, "explorer") || strstr(content1, "Explorer")) {
    const char *target = content2 && content2[0] ? content2 : content3;
    if (target && target[0]) {
      const char *argv[] = { "xdg-open", target, NULL };
      pid_t pid = fork();
      if (pid == 0) { execvp("xdg-open", (char *const *)argv); _exit(1); }
      return pid > 0;
    }
    return FALSE;
  }

  // notepad.exe → open text file
  if (strstr(content1, "notepad") || strstr(content1, "Notepad")) {
    const char *target = content2;
    const char *argv[] = { "xdg-open", target && target[0] ? target : NULL, NULL };
    pid_t pid = fork();
    if (pid == 0) { execvp("xdg-open", (char *const *)argv); _exit(1); }
    return pid > 0;
  }

  pid_t pid = fork();
  if (pid == 0) {
    if (content3 && content3[0]) chdir(content3);

    const char *args[16];
    int argc = 0;
    args[argc++] = content1;

    // Tokenize content2 (parameters) by spaces
    if (content2 && content2[0]) {
      WDL_FastString tok;
      for (const char *p = content2; *p && argc < 15; ) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *s = p;
        int in_quote = 0;
        while (*p && (in_quote || (*p != ' ' && *p != '\t'))) {
          if (*p == '"') in_quote = !in_quote;
          p++;
        }
        tok.Set(s, (int)(p - s));
        // Strip surrounding quotes
        const char *ts = tok.Get();
        if (ts[0] == '"' && ts[tok.GetLength()-1] == '"') {
          WDL_FastString unq;
          unq.Set(ts + 1, tok.GetLength() - 2);
          args[argc] = strdup(unq.Get());
        } else {
          args[argc] = strdup(ts);
        }
        argc++;
      }
    }
    args[argc] = NULL;

    execvp(content1, (char *const *)args);

    // execvp failed — try xdg-open as fallback for files/dirs
    if (content2 && content2[0]) {
      const char *fallback[] = { "xdg-open", content2, NULL };
      execvp("xdg-open", (char *const *)fallback);
    }
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

#define MAX_CLIPBOARD_ENTRIES 32

struct sw_clipboard_format_rec {
  char name[256];
  UINT id;
};

struct sw_clipboard_data_rec {
  UINT format;
  HANDLE data;
};

static sw_clipboard_format_rec g_clipboard_formats[MAX_CLIPBOARD_ENTRIES];
static int g_clipboard_format_count = 0;

static sw_clipboard_data_rec g_clipboard_data[MAX_CLIPBOARD_ENTRIES];
static int g_clipboard_data_count = 0;

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
  for (int i = 0; i < g_clipboard_data_count; i++) {
    GlobalFree(g_clipboard_data[i].data);
  }
  g_clipboard_data_count = 0;
#ifdef SWELL_TARGET_SDL3
  SDL_SetClipboardText("");
#endif
}

HANDLE GetClipboardData(UINT type)
{
  // Look up in our own stored clipboard data first
  for (int i = 0; i < g_clipboard_data_count; i++) {
    if (g_clipboard_data[i].format == type)
      return g_clipboard_data[i].data;
  }
#ifdef SWELL_TARGET_SDL3
  // CF_TEXT fallback: fetch from OS clipboard if SDL has text
  if (type == CF_TEXT) {
    if (!SDL_HasClipboardText()) return NULL;
    const char *txt = SDL_GetClipboardText();
    if (!txt) return NULL;
    int len = (int)strlen(txt) + 1;
    char *buf = (char *)malloc(len);
    if (buf) {
      memcpy(buf, txt, len);
      // Cache it so it's freed on EmptyClipboard
      if (g_clipboard_data_count < MAX_CLIPBOARD_ENTRIES) {
        g_clipboard_data[g_clipboard_data_count].format = CF_TEXT;
        g_clipboard_data[g_clipboard_data_count].data = buf;
        g_clipboard_data_count++;
      }
    }
    SDL_free((void*)txt);
    return buf;
  }
#endif
  return NULL;
}

void SetClipboardData(UINT type, HANDLE h)
{
  if (!h) return;
#ifdef SWELL_TARGET_SDL3
  if (type == CF_TEXT) {
    SDL_SetClipboardText((const char *)h);
  }
#endif
  // Store / replace in our list
  for (int i = 0; i < g_clipboard_data_count; i++) {
    if (g_clipboard_data[i].format == type) {
      GlobalFree(g_clipboard_data[i].data);
      g_clipboard_data[i].data = h;
      return;
    }
  }
  if (g_clipboard_data_count < MAX_CLIPBOARD_ENTRIES) {
    g_clipboard_data[g_clipboard_data_count].format = type;
    g_clipboard_data[g_clipboard_data_count].data = h;
    g_clipboard_data_count++;
  }
}

static UINT g_next_clipboard_format = 0xC000;  // custom format base

UINT RegisterClipboardFormat(const char *desc)
{
  if (!desc || !desc[0]) return 0;
  // Look up existing
  for (int i = 0; i < g_clipboard_format_count; i++) {
    if (!strcmp(g_clipboard_formats[i].name, desc))
      return g_clipboard_formats[i].id;
  }
  // Register new
  if (g_clipboard_format_count < MAX_CLIPBOARD_ENTRIES) {
    const char *src = desc;
    char *dst = g_clipboard_formats[g_clipboard_format_count].name;
    int i = 0;
    while (i < (int)sizeof(g_clipboard_formats[0].name) - 1 && *src)
      dst[i++] = *src++;
    dst[i] = 0;
    g_clipboard_formats[g_clipboard_format_count].id = g_next_clipboard_format++;
    g_clipboard_format_count++;
    return g_clipboard_formats[g_clipboard_format_count - 1].id;
  }
  return 0;
}

UINT EnumClipboardFormats(UINT lastfmt)
{
  if (lastfmt == 0) {
    if (g_clipboard_data_count > 0) return g_clipboard_data[0].format;
    return 0;
  }
  for (int i = 0; i < g_clipboard_data_count; i++) {
    if (g_clipboard_data[i].format == lastfmt && i + 1 < g_clipboard_data_count)
      return g_clipboard_data[i + 1].format;
  }
  return 0;
}

HANDLE GlobalAlloc(int flags, int sz)
{
  (void)flags;
  size_t alloc_sz = sz > 0 ? (size_t)sz : 1;
  // Prefix allocation with size header so GlobalSize can return it
  char *raw = (char *)calloc(1, sizeof(size_t) + alloc_sz);
  if (!raw) return NULL;
  *(size_t *)raw = alloc_sz;
  return (HANDLE)(raw + sizeof(size_t));
}

void *GlobalLock(HANDLE h)   { return (void *)h; }
int   GlobalSize(HANDLE h)
{
  if (!h) return 0;
  size_t *hdr = (size_t *)h;
  return (int)hdr[-1];
}
void  GlobalUnlock(HANDLE h) { (void)h; }
void  GlobalFree(HANDLE h)
{
  if (!h) return;
  char *raw = (char *)h;
  free(raw - sizeof(size_t));
}

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
      return g_swell_theme.scrollbar_width;
    case SM_CYMENU:      return g_swell_theme.menubar_height;
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

// Magic values stored as first field in heap-allocated handle structs.
// CloseHandle checks these to safely distinguish swell-owned allocations
// from plain integer values (fds, sockets) that REAPER passes as HANDLE.
static const uint32_t SWELL_HANDLE_MAGIC_EVENT   = 0x53574556u; // 'SWEV'
static const uint32_t SWELL_HANDLE_MAGIC_THREAD  = 0x53575448u; // 'SWTH'
static const uint32_t SWELL_HANDLE_MAGIC_PROCESS = 0x53575043u; // 'SWPC'

// ---- Simple event using pipe ----
struct EventHandle {
  uint32_t magic; // must be first; = SWELL_HANDLE_MAGIC_EVENT
  int rd, wr;
  bool manual_reset;
};

struct ThreadHandle {
  uint32_t magic; // = SWELL_HANDLE_MAGIC_THREAD
  pthread_t tid;
  volatile int done;
  DWORD retv;
  DWORD (*proc)(LPVOID);
  LPVOID parm;
};

struct ProcessHandle {
  uint32_t magic; // = SWELL_HANDLE_MAGIC_PROCESS
  pid_t pid;
};

static void *thread_thunk(void *arg)
{
  ThreadHandle *th = (ThreadHandle *)arg;
  DWORD ret = th->proc(th->parm);
  th->retv = ret;
  th->done = 1;
  return NULL;
}

HANDLE CreateThread(void *TA, DWORD stackSize, DWORD (*ThreadProc)(LPVOID),
                    LPVOID parm, DWORD cf, DWORD *tidOut)
{
  (void)TA; (void)cf;
  ThreadHandle *th = (ThreadHandle *)malloc(sizeof(ThreadHandle));
  if (!th) return NULL;
  th->magic = SWELL_HANDLE_MAGIC_THREAD;
  th->done = 0;
  th->retv = 0;
  th->proc = ThreadProc;
  th->parm = parm;

  pthread_attr_t attr;
  pthread_attr_t *pattr = NULL;
  if (stackSize > 0) {
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stackSize);
    pattr = &attr;
  }

  if (pthread_create(&th->tid, pattr, thread_thunk, th) != 0) {
    if (pattr) pthread_attr_destroy(&attr);
    free(th);
    return NULL;
  }
  if (pattr) pthread_attr_destroy(&attr);
  if (tidOut) *tidOut = (DWORD)(uintptr_t)th->tid;
  return (HANDLE)th;
}

DWORD GetCurrentThreadId()
{
  return (DWORD)(uintptr_t)pthread_self();
}

BOOL SetThreadPriority(HANDLE evt, int prio)
{
  if (!evt || (uintptr_t)evt < 4096u) return FALSE;
  uint32_t magic = *(const uint32_t *)evt;
  if (magic != SWELL_HANDLE_MAGIC_THREAD) return FALSE;

  ThreadHandle *th = (ThreadHandle *)evt;

  // Map Win32 THREAD_PRIORITY_* to Linux nice value (-20..+19)
  // Win32 range: IDLE(-15) to TIME_CRITICAL(15)
  // Linux range: nice -20 (highest) to +19 (lowest)
  int nice_val;
  if      (prio >= 15) nice_val = -20;
  else if (prio >= 2)  nice_val = -10;
  else if (prio >= 1)  nice_val = -5;
  else if (prio >= 0)  nice_val =  0;
  else if (prio >= -1) nice_val =  5;
  else if (prio >= -2) nice_val = 10;
  else                 nice_val = 19;

  // pthread_t on glibc Linux is the kernel TID
  setpriority(PRIO_PROCESS, (pid_t)(uintptr_t)th->tid, nice_val);
  return TRUE;
}

BOOL CloseHandle(HANDLE hand)
{
  if (!hand || (uintptr_t)hand < 4096u) return FALSE;
  uint32_t magic = *(const uint32_t *)hand;
  if (magic == SWELL_HANDLE_MAGIC_EVENT) {
    EventHandle *ev = (EventHandle *)hand;
    close(ev->rd);
    close(ev->wr);
    ev->magic = 0;
    free(ev);
    return TRUE;
  }
  if (magic == SWELL_HANDLE_MAGIC_THREAD) {
    ThreadHandle *th = (ThreadHandle *)hand;
    th->magic = 0;
    pthread_detach(th->tid);
    free(th);
    return TRUE;
  }
  if (magic == SWELL_HANDLE_MAGIC_PROCESS) {
    ProcessHandle *ph = (ProcessHandle *)hand;
    ph->magic = 0;
    // Don't kill the process — just free the handle
    free(ph);
    return TRUE;
  }
  // Unknown handle type (fd, socket, etc.) — do not free
  return FALSE;
}

HANDLE CreateEvent(void *SA, BOOL manualReset, BOOL initialSig,
                   const char *ignored)
{
  (void)SA; (void)ignored;
  int fd[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) return NULL;
  fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK);
  fcntl(fd[1], F_SETFL, fcntl(fd[1], F_GETFL) | O_NONBLOCK);
  EventHandle *ev = (EventHandle *)malloc(sizeof(EventHandle));
  ev->magic = SWELL_HANDLE_MAGIC_EVENT;
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
  if (!evt || (uintptr_t)evt < 4096u) return FALSE;
  EventHandle *ev = (EventHandle *)evt;
  if (ev->magic != SWELL_HANDLE_MAGIC_EVENT)
    return FALSE;
  if (ev->manual_reset) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(ev->rd, &fds);
    struct timeval tv = {0, 0};
    if (select(ev->rd + 1, &fds, NULL, NULL, &tv) > 0)
      return TRUE;  // already signaled
  }
  char b = 1;
  write(ev->wr, &b, 1);
  return TRUE;
}

BOOL ResetEvent(HANDLE evt)
{
  if (!evt || (uintptr_t)evt < 4096u) return FALSE;
  EventHandle *ev = (EventHandle *)evt;
  if (ev->magic != SWELL_HANDLE_MAGIC_EVENT) return FALSE;
  char buf[256];
  while (read(ev->rd, buf, sizeof(buf)) > 0) {}
  return TRUE;
}

DWORD WaitForSingleObject(HANDLE hand, DWORD msTO)
{
  if (!hand || (uintptr_t)hand < 4096u) return (DWORD)WAIT_FAILED;

  // Check known handle types by magic number first.
  const uint32_t magic = *(const uint32_t *)hand;

  if (magic == SWELL_HANDLE_MAGIC_EVENT) {
    EventHandle *ev = (EventHandle *)hand;

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
    if (ev->manual_reset) {
      return WAIT_OBJECT_0;  // keep signal byte in pipe for other waiters
    }
    if (read(ev->rd, &b, 1) <= 0) return (DWORD)WAIT_FAILED;
    return WAIT_OBJECT_0;
  }

  if (magic == SWELL_HANDLE_MAGIC_THREAD) {
    ThreadHandle *th = (ThreadHandle *)hand;

    const DWORD start = GetTickCount();
    for (;;) {
      if (th->done)
        return WAIT_OBJECT_0;
      if (msTO != INFINITE && (GetTickCount() - start) >= msTO)
        return WAIT_TIMEOUT;
      if (msTO == 0)
        return WAIT_TIMEOUT;
      usleep(1000);
    }
  }

  if (magic == SWELL_HANDLE_MAGIC_PROCESS) {
    ProcessHandle *ph = (ProcessHandle *)hand;
    pid_t pid = ph->pid;
    int status;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r > 0) return WAIT_OBJECT_0;
    if (msTO == 0) return WAIT_TIMEOUT;
    if (msTO == INFINITE) {
      while (true) {
        usleep(10000);
        r = waitpid(pid, &status, WNOHANG);
        if (r > 0) return WAIT_OBJECT_0;
        if (r < 0) return (DWORD)WAIT_FAILED;
      }
    }
    DWORD start = GetTickCount();
    while (GetTickCount() - start < msTO) {
      usleep(10000);
      r = waitpid(pid, &status, WNOHANG);
      if (r > 0) return WAIT_OBJECT_0;
      if (r < 0) return (DWORD)WAIT_FAILED;
    }
    return WAIT_TIMEOUT;
  }

  // PID handle fallback: use kill(pid,0) to validate it's a real process.
  // Linux pid_max defaults to 4,194,304; this avoids the old value-range
  // heuristic that broke PID handles above 0x100000.
  {
    pid_t pid = (pid_t)(uintptr_t)hand;
    if (pid > 0 && kill(pid, 0) == 0) {
      int status;
      pid_t r = waitpid(pid, &status, WNOHANG);
      if (r > 0) return WAIT_OBJECT_0;
      if (msTO == 0) return WAIT_TIMEOUT;
      if (msTO == INFINITE) {
        // Poll with WNOHANG — never call blocking waitpid so that
        // SWELL_GetProcessExitCode can later reap the zombie.
        while (true) {
          usleep(10000);
          r = waitpid(pid, &status, WNOHANG);
          if (r > 0) return WAIT_OBJECT_0;
          if (r < 0) return (DWORD)WAIT_FAILED;
        }
      }
      DWORD start = GetTickCount();
      while (GetTickCount() - start < msTO) {
        usleep(10000);
        r = waitpid(pid, &status, WNOHANG);
        if (r > 0) return WAIT_OBJECT_0;
        if (r < 0) return (DWORD)WAIT_FAILED;
      }
      return WAIT_TIMEOUT;
    }
  }

  return (DWORD)WAIT_FAILED;
}

DWORD WaitForAnySocketObject(int numObjs, HANDLE *objs, DWORD msTO)
{
  if (numObjs <= 0 || !objs) return (DWORD)WAIT_FAILED;
  fd_set fds;
  FD_ZERO(&fds);
  int maxfd = 0;
  for (int i = 0; i < numObjs; i++) {
    HANDLE h = objs[i];
    if (!h || (uintptr_t)h < 4096u) continue;
    uint32_t magic = *(const uint32_t *)h;
    if (magic != SWELL_HANDLE_MAGIC_EVENT) continue;
    EventHandle *ev = (EventHandle *)h;
    FD_SET(ev->rd, &fds);
    if (ev->rd > maxfd) maxfd = ev->rd;
  }
  struct timeval tv = { (long)(msTO / 1000), (long)((msTO % 1000) * 1000) };
  int r = select(maxfd + 1, &fds, NULL, NULL,
                 msTO == INFINITE ? NULL : &tv);
  if (r <= 0) return (r == 0) ? (DWORD)WAIT_TIMEOUT : (DWORD)WAIT_FAILED;
  for (int i = 0; i < numObjs; i++) {
    HANDLE h = objs[i];
    if (!h || (uintptr_t)h < 4096u) continue;
    uint32_t magic = *(const uint32_t *)h;
    if (magic != SWELL_HANDLE_MAGIC_EVENT) continue;
    EventHandle *ev = (EventHandle *)h;
    if (FD_ISSET(ev->rd, &fds)) {
      if (!ev->manual_reset) {
        char b; read(ev->rd, &b, 1);
      }
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
  ProcessHandle *ph = (ProcessHandle *)malloc(sizeof(ProcessHandle));
  if (!ph) return NULL;
  ph->magic = SWELL_HANDLE_MAGIC_PROCESS;
  ph->pid = pid;
  return (HANDLE)ph;
}

int SWELL_GetProcessExitCode(HANDLE hand)
{
  if (!hand || (uintptr_t)hand < 4096u) return -1;
  uint32_t magic = *(const uint32_t *)hand;
  pid_t pid;
  if (magic == SWELL_HANDLE_MAGIC_PROCESS) {
    pid = ((ProcessHandle *)hand)->pid;
  } else {
    // Backward compat: raw pid values (from older code)
    pid = (pid_t)(intptr_t)hand;
    if (pid <= 0) return -1;
  }
  int status;
  pid_t r = waitpid(pid, &status, WNOHANG);
  if (r == 0) return -1;  // still running
  if (r < 0) return -1;
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
  pt->x = swell_log_to_phys(mx);
  pt->y = swell_log_to_phys(my);
#else
  pt->x = 0; pt->y = 0;
#endif
}

DWORD GetMessagePos()
{
#ifdef SWELL_TARGET_SDL3
  float mx, my;
  SDL_GetGlobalMouseState(&mx, &my);
  return MAKELPARAM(swell_log_to_phys(mx), swell_log_to_phys(my));
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
static int g_cursor_vis_cnt = 0;

void SWELL_Register_Cursor_Resource(const char *idx, const char *name,
                                     int hotspot_x, int hotspot_y)
{
  CursorEntry *e = (CursorEntry *)calloc(1, sizeof(CursorEntry));
  // idx may be MAKEINTRESOURCE (integer cast to pointer) — guard before strdup
  e->id        = (idx && (size_t)idx > 0xFFFF) ? strdup(idx) : (char *)idx;
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
        if (e->name) {
          e->cursor = (SDL_Cursor *)SWELL_LoadCursorFromFile(e->name);
        }
        if (!e->cursor) {
          SDL_SystemCursor sc = idcname_to_sdl(idx);
          e->cursor = SDL_CreateSystemCursor(sc);
        }
      }
      return (HCURSOR)e;
    }
  }
  // Integer ID (MAKEINTRESOURCE): auto-register to cache and avoid leak
  if ((size_t)idx <= 0xFFFF) {
    CursorEntry *e = (CursorEntry *)calloc(1, sizeof(CursorEntry));
    e->id = (char *)idx;
    e->cursor = SDL_CreateSystemCursor(idcname_to_sdl(idx));
    e->next = g_cursor_list;
    g_cursor_list = e;
    return (HCURSOR)e;
  }
  // String ID not found in registered list
  return NULL;
#else
  (void)idx; return NULL;
#endif
}

HCURSOR SWELL_LoadCursorFromFile(const char *fn)
{
#ifdef SWELL_TARGET_SDL3
  if (!fn || !fn[0]) return NULL;

  // Load image via GDI loader (supports PNG, JPEG, BMP, WebP, GIF)
  HICON icon = LoadNamedImage(fn, false);
  if (!icon) return NULL;

  HGDIOBJ__ *gdi = (HGDIOBJ__*)icon;
  if (gdi->type != TYPE_BITMAP || !gdi->typedata) {
    DeleteObject((HGDIOBJ)icon);
    return NULL;
  }

  SkBitmap *bm = (SkBitmap*)gdi->typedata;
  if (!bm->getPixels()) {
    DeleteObject((HGDIOBJ)icon);
    return NULL;
  }

  int w = bm->width();
  int h = bm->height();
  if (w <= 0 || h <= 0 || w > 128 || h > 128) {
    DeleteObject((HGDIOBJ)icon);
    return NULL;
  }

  // Create SDL surface from SkBitmap BGRA pixels
  SDL_Surface *surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_BGRA32);
  if (!surf) {
    DeleteObject((HGDIOBJ)icon);
    return NULL;
  }

  bm->readPixels(bm->info(), surf->pixels, surf->pitch, 0, 0);
  SDL_Cursor *cur = SDL_CreateColorCursor(surf, 0, 0);
  SDL_DestroySurface(surf);
  DeleteObject((HGDIOBJ)icon);

  return (HCURSOR)cur;
#else
  (void)fn; return NULL;
#endif
}

void SWELL_SetCursor(HCURSOR curs)
{
  g_current_cursor = curs;
#ifdef SWELL_TARGET_SDL3
  if (!curs) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    if (g_cursor_vis_cnt >= 0) SDL_ShowCursor();
    else SDL_HideCursor();
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
  if (g_cursor_vis_cnt >= 0) SDL_ShowCursor();
  else SDL_HideCursor();
#endif
}

HCURSOR SWELL_GetCursor()             { return g_current_cursor; }
HCURSOR SWELL_GetLastSetCursor()      { return g_current_cursor; }
bool    SWELL_IsCursorVisible()       { return g_cursor_vis_cnt >= 0; }
BOOL    SWELL_SetCursorPos(int X, int Y)
{
#ifdef SWELL_TARGET_SDL3
  SDL_WarpMouseGlobal((float)swell_phys_to_log(X), (float)swell_phys_to_log(Y));
  return TRUE;
#else
  (void)X; (void)Y; return FALSE;
#endif
}

int SWELL_ShowCursor(BOOL bShow)
{
  g_cursor_vis_cnt += bShow ? 1 : -1;
#ifdef SWELL_TARGET_SDL3
  if (g_cursor_vis_cnt == -1 && !bShow) SDL_HideCursor();
  if (g_cursor_vis_cnt == 0 && bShow) SDL_ShowCursor();
#endif
  return g_cursor_vis_cnt;
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
  if (!list || idx < 0 || idx >= list->m_entries.GetSize()) return FALSE;
  HIMAGELIST__::Entry *e = list->m_entries.Get(idx);
  if (e->image) DeleteObject((HGDIOBJ)e->image);
  if (e->mask)  DeleteObject((HGDIOBJ)e->mask);
  list->m_entries.Delete(idx, true);
  return TRUE;
}

int ImageList_ReplaceIcon(HIMAGELIST list, int offset, HICON image)
{
  if (!list || offset < 0 || offset >= list->m_entries.GetSize() || !image)
    return -1;
  HIMAGELIST__::Entry *e = list->m_entries.Get(offset);
  if (e->image) DeleteObject((HGDIOBJ)e->image);
  e->image = (HGDIOBJ__*)SWELL_CloneGDIObject((HGDIOBJ)image);
  e->mask  = NULL;
  return offset;
}

int ImageList_Add(HIMAGELIST list, HBITMAP image, HBITMAP mask)
{
  if (!list || !image) return -1;
  HIMAGELIST__::Entry *e = new HIMAGELIST__::Entry();
  e->image = (HGDIOBJ__*)SWELL_CloneGDIObject((HGDIOBJ)image);
  e->mask  = mask ? (HGDIOBJ__*)SWELL_CloneGDIObject((HGDIOBJ)mask) : NULL;
  list->m_entries.Add(e);
  return list->m_entries.GetSize() - 1;
}

void ImageList_Destroy(HIMAGELIST list)
{
  if (!list) return;
  for (int i = list->m_entries.GetSize() - 1; i >= 0; i--) {
    HIMAGELIST__::Entry *e = list->m_entries.Get(i);
    if (e) {
      if (e->image) { DeleteObject((HGDIOBJ)e->image); e->image = NULL; }
      if (e->mask)  { DeleteObject((HGDIOBJ)e->mask);  e->mask  = NULL; }
    }
  }
  delete list;
}

// ============================================================================
// Extended API / Misc
// ============================================================================

const char *g_swell_appname    = NULL;
       char       *g_swell_defini     = NULL;
static const char *g_swell_fontpangram = NULL;

// Drag-drop callbacks (set by app via SWELL_ExtendedAPI)
void (*SWELL_DDrop_onDragLeave)(void)                = NULL;
void (*SWELL_DDrop_onDragOver)(HWND, int, int)       = NULL;
void (*SWELL_DDrop_onDragEnter)(void *, HWND, int, int) = NULL;
const char *(*SWELL_DDrop_getDroppedFileTargetPath)(const char *) = NULL;

void *SWELL_ExtendedAPI(const char *key, void *v)
{
  if (!key) return NULL;

  if (!strcmp(key, "APPNAME")) {
    g_swell_appname = (const char *)v;
    return NULL;
  }
  if (!strcmp(key, "FONTPANGRAM")) {
    g_swell_fontpangram = (const char *)v;
    return NULL;
  }
  if (!strcmp(key, "INIFILE")) {
    free(g_swell_defini);
    g_swell_defini = v ? strdup((const char *)v) : NULL;

    // Raise file descriptor limit for audio workloads
    struct rlimit rl = {};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
      rlim_t want = 16384;
      if (want > rl.rlim_max) want = rl.rlim_max;
      if (want > rl.rlim_cur) {
        rl.rlim_cur = want;
        setrlimit(RLIMIT_NOFILE, &rl);
      }
    }
    return NULL;
  }
  if (!strcmp(key, "activate_app")) {
    // Raise the frontmost SDL window
#ifdef SWELL_TARGET_SDL3
    extern HWND g_swell_focused_oswindow_hwnd;
    if (g_swell_focused_oswindow_hwnd && g_swell_focused_oswindow_hwnd->m_oswindow)
      SDL_RaiseWindow((SDL_Window*)g_swell_focused_oswindow_hwnd->m_oswindow);
#endif
    return NULL;
  }
#ifdef SWELL_TARGET_SDL3
  if (!strcmp(key, "FULLSCREEN") || !strcmp(key, "-FULLSCREEN") || !strcmp(key, "oFULLSCREEN")) {
    HWND hwnd = (HWND)v;
    if (!hwnd || !hwnd->m_oswindow) return NULL;
    SDL_Window *w = (SDL_Window*)hwnd->m_oswindow;
    if (key[0] == '-')
      SDL_SetWindowFullscreen(w, false);
    else
      SDL_SetWindowFullscreen(w, true);
    return v;
  }
  if (!strcmp(key, "PREVENT_SCREENSAVER")) {
    SDL_DisableScreenSaver();
    return NULL;
  }
  if (!strcmp(key, "-PREVENT_SCREENSAVER")) {
    SDL_EnableScreenSaver();
    return NULL;
  }
#endif
  if (!strcmp(key, "SWELL_DDrop_onDragLeave"))   { *(void**)&SWELL_DDrop_onDragLeave = v; return v; }
  if (!strcmp(key, "SWELL_DDrop_onDragOver"))    { *(void**)&SWELL_DDrop_onDragOver = v; return v; }
  if (!strcmp(key, "SWELL_DDrop_onDragEnter"))   { *(void**)&SWELL_DDrop_onDragEnter = v; return v; }
  if (!strcmp(key, "SWELL_DDrop_getDroppedFileTargetPath")) {
    *(void**)&SWELL_DDrop_getDroppedFileTargetPath = v; return v;
  }
  return NULL;
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
  (void)hwnd; (void)ncustom; (void)custom;
  if (!color) return false;

  // Build zenity command: show color picker, pre-select current color
  int r = GetRValue(*color), g = GetGValue(*color), b = GetBValue(*color);
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "zenity --color-selection --color='#%02X%02X%02X' 2>/dev/null",
           r, g, b);
  char *raw = zenity_run(cmd);
  if (!raw) return false;

  // Parse output: zenity outputs rgb(R,G,B) or #RRGGBB
  int rr = 0, gg = 0, bb = 0;
  if (sscanf(raw, "rgb(%d,%d,%d)", &rr, &gg, &bb) == 3 ||
      sscanf(raw, "#%02x%02x%02x", &rr, &gg, &bb) == 3) {
    *color = RGB(rr, gg, bb);
    free(raw);
    return true;
  }
  free(raw);
  return false;
}

bool SWELL_ChooseFont(HWND hwnd, LOGFONT *lf)
{
  (void)hwnd;
  if (!lf) return false;

  // List available font families via fc-list, let user pick with zenity
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "fc-list : family | sort -u | zenity --list --column=Font "
           "--title='Select Font' --width=400 --height=400 2>/dev/null");
  char *raw = zenity_run(cmd);
  if (!raw) return false;

  // Fill LOGFONT with selected family + sensible defaults
  memset(lf, 0, sizeof(*lf));
  lf->lfHeight = -12; // 12pt
  lf->lfWeight = FW_NORMAL; // 400
  lf->lfCharSet = DEFAULT_CHARSET;
  lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf->lfQuality = DEFAULT_QUALITY;
  lf->lfPitchAndFamily = DEFAULT_PITCH;
  strncpy(lf->lfFaceName, raw, sizeof(lf->lfFaceName) - 1);
  lf->lfFaceName[sizeof(lf->lfFaceName) - 1] = '\0';
  free(raw);
  return true;
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
  if (pid <= 0) return NULL;
  ProcessHandle *ph = (ProcessHandle *)malloc(sizeof(ProcessHandle));
  if (!ph) return NULL;
  ph->magic = SWELL_HANDLE_MAGIC_PROCESS;
  ph->pid = (pid_t)pid;
  return (HANDLE)ph;
}
#endif
