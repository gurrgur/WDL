#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "swell.h"
#include "swell-dlggen.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

struct SWELL_CursorResourceIndex *SWELL_curmodule_cursorresource_head;
struct SWELL_DialogResourceIndex *SWELL_curmodule_dialogresource_head;
struct SWELL_MenuResourceIndex *SWELL_curmodule_menuresource_head;

#ifndef TVM_FIRST
#define TVM_FIRST               0x1100
#define TVM_INSERTITEM          (TVM_FIRST+50)
#define TVM_DELETEITEM          (TVM_FIRST+1)
#define TVM_EXPAND              (TVM_FIRST+2)
#define TVE_EXPAND              0x0002
#endif

#ifndef TVI_ROOT
#define TVI_ROOT                ((HTREEITEM)0xFFFF0000)
#define TVI_FIRST               ((HTREEITEM)0xFFFF0001)
#define TVI_LAST                ((HTREEITEM)0xFFFF0002)
#define TVI_SORT                ((HTREEITEM)0xFFFF0003)
#endif

#ifndef LBS_NOTIFY
#define LBS_NOTIFY              0x0001
#endif

enum
{
  IDD_WINDOW_BENCH = 3000,
  IDD_WINDOW_BENCH_TEMP = 3001,

  IDC_EDIT = 3100,
  IDC_COMBO = 3101,
  IDC_LISTBOX = 3102,
  IDC_CHECK = 3103,
  IDC_SLIDER = 3104,
  IDC_PROGRESS = 3105,
  IDC_LISTVIEW = 3106,
  IDC_TREEVIEW = 3107,
  IDC_TAB = 3108,
  IDC_PAINT = 3109,
  IDC_LABEL = 3110,
  IDC_LONG_EDIT = 3111,

  IDC_TEMP_EDIT = 3200,
  IDC_TEMP_BUTTON = 3201,
  IDC_TEMP_LISTBOX = 3202,
};

struct Options
{
  int width = 760;
  int height = 520;
  int duration_ms = 1000;
  bool pretty = false;
  std::vector<std::string> selected;
};

struct BenchResult
{
  std::string name;
  std::string category;
  int target_ms = 0;
  double elapsed_ms = 0.0;
  uint64_t iterations = 0;
  uint64_t operations = 0;
  uint64_t paint_count = 0;
  uint64_t size_count = 0;
  uint64_t command_count = 0;
  uint64_t notify_count = 0;
  double ops_per_second = 0.0;
  double ns_per_op = 0.0;
};

struct WindowBenchState
{
  HWND main = NULL;
  HWND edit = NULL;
  HWND combo = NULL;
  HWND listbox = NULL;
  HWND check = NULL;
  HWND slider = NULL;
  HWND progress = NULL;
  HWND listview = NULL;
  HWND treeview = NULL;
  HWND tab = NULL;
  HWND paint = NULL;
  HWND long_edit = NULL;
  int long_text_len = 0;
  uint64_t paint_count = 0;
  uint64_t size_count = 0;
  uint64_t command_count = 0;
  uint64_t notify_count = 0;
};

struct Bench
{
  const char *name;
  const char *category;
  uint64_t ops_per_iteration;
  void (*run)(WindowBenchState &, int, int, uint64_t);
};

static WindowBenchState g_state;
static std::string g_long_text;

static uint32_t lcg_next(uint32_t &state)
{
  state = state * 1664525u + 1013904223u;
  return state;
}

static int rnd(uint32_t &state, int maxv)
{
  return maxv > 0 ? (int)(lcg_next(state) % (uint32_t)maxv) : 0;
}

static void pump_messages(int n = 1)
{
  for (int i = 0; i < n; ++i)
    SWELL_RunMessageLoop();
}

static const std::string &long_edit_text()
{
  if (!g_long_text.empty()) return g_long_text;

  g_long_text.reserve(64000);
  for (int i = 0; i < 512; ++i)
  {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "Line %03d: SWELL edit benchmark text with selection ranges, wrapping-ish data, numbers %06d.\n",
             i, i * 7919);
    g_long_text += buf;
  }
  return g_long_text;
}

static LRESULT CALLBACK PaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
    {
      ++g_state.paint_count;
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (hdc)
      {
        RECT cr = {0, 0, 0, 0};
        GetClientRect(hwnd, &cr);

        HBRUSH bg = CreateSolidBrush(RGB(244, 246, 248));
        FillRect(hdc, &cr, bg);
        DeleteObject((HGDIOBJ)bg);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 90, 170));
        HGDIOBJ old_pen = SelectObject(hdc, (HGDIOBJ)pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);

        for (int i = 0; i < 18; ++i)
        {
          MoveToEx(hdc, 8, 10 + i * 9, NULL);
          LineTo(hdc, cr.right - 8, cr.bottom - 10 - i * 7);
        }

        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject((HGDIOBJ)pen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(20, 30, 45));
        RECT tr = {10, 10, cr.right - 10, 34};
        DrawText(hdc, "paint_update benchmark", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void populate_static_widgets(WindowBenchState &s)
{
  if (s.combo)
  {
    SendMessage(s.combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 24; ++i)
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "Combo row %02d", i);
      SendMessage(s.combo, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(s.combo, CB_SETCURSEL, 0, 0);
  }

  if (s.listbox)
  {
    SendMessage(s.listbox, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 64; ++i)
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "List row %02d", i);
      SendMessage(s.listbox, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(s.listbox, LB_SETCURSEL, 0, 0);
  }

  if (s.slider)
  {
    SendMessage(s.slider, TBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(s.slider, TBM_SETPOS, 1, 50);
  }

  if (s.progress)
  {
    SendMessage(s.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(s.progress, PBM_SETPOS, 50, 0);
  }

  if (s.tab)
  {
    TCITEM item = {0};
    item.mask = TCIF_TEXT;
    for (int i = 0; i < 6; ++i)
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "Tab %d", i + 1);
      item.pszText = buf;
      TabCtrl_InsertItem(s.tab, i, &item);
    }
  }

  if (s.listview)
  {
    LVCOLUMN col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 120;
    col.pszText = (char *)"Name";
    ListView_InsertColumn(s.listview, 0, &col);
    col.pszText = (char *)"Value";
    ListView_InsertColumn(s.listview, 1, &col);
  }

  if (s.long_edit)
  {
    const std::string &txt = long_edit_text();
    s.long_text_len = (int)txt.size();
    SetDlgItemText(s.main, IDC_LONG_EDIT, txt.c_str());
    SendMessage(s.long_edit, EM_SETSEL, 0, 256);
  }
}

static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_INITDIALOG:
      g_state.main = hwndDlg;
      g_state.edit = GetDlgItem(hwndDlg, IDC_EDIT);
      g_state.combo = GetDlgItem(hwndDlg, IDC_COMBO);
      g_state.listbox = GetDlgItem(hwndDlg, IDC_LISTBOX);
      g_state.check = GetDlgItem(hwndDlg, IDC_CHECK);
      g_state.slider = GetDlgItem(hwndDlg, IDC_SLIDER);
      g_state.progress = GetDlgItem(hwndDlg, IDC_PROGRESS);
      g_state.listview = GetDlgItem(hwndDlg, IDC_LISTVIEW);
      g_state.treeview = GetDlgItem(hwndDlg, IDC_TREEVIEW);
      g_state.tab = GetDlgItem(hwndDlg, IDC_TAB);
      g_state.paint = GetDlgItem(hwndDlg, IDC_PAINT);
      g_state.long_edit = GetDlgItem(hwndDlg, IDC_LONG_EDIT);
      if (g_state.paint)
      {
        SetWindowLong(g_state.paint, GWL_WNDPROC, (LONG_PTR)PaintProc);
        SetOpaque(g_state.paint, true);
      }
      populate_static_widgets(g_state);
      return 1;

    case WM_SIZE:
      ++g_state.size_count;
      return 0;

    case WM_COMMAND:
      ++g_state.command_count;
      return 0;

    case WM_NOTIFY:
      ++g_state.notify_count;
      return 0;

    case WM_CLOSE:
      DestroyWindow(hwndDlg);
      return 1;
  }
  return 0;
}

static INT_PTR CALLBACK TempDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_INITDIALOG:
      SendDlgItemMessage(hwndDlg, IDC_TEMP_LISTBOX, LB_ADDSTRING, 0, (LPARAM)"row");
      SetDlgItemText(hwndDlg, IDC_TEMP_EDIT, "temp");
      return 1;
  }
  return 0;
}

static void bench_resize_window(WindowBenchState &s, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 131u + 17u;
  for (int i = 0; i < 48; ++i)
  {
    int ww = w - 80 + rnd(state, 120);
    int hh = h - 60 + rnd(state, 100);
    SetWindowPos(s.main, NULL, 40 + rnd(state, 12), 40 + rnd(state, 12), ww, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    pump_messages();
  }
}

static void bench_control_layout(WindowBenchState &s, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 313u + 29u;
  HWND controls[] = {s.edit, s.combo, s.listbox, s.check, s.slider, s.progress, s.listview, s.treeview, s.tab, s.paint, s.long_edit};
  for (int i = 0; i < 11; ++i)
  {
    HWND hwnd = controls[i];
    if (!hwnd) continue;
    int x = 8 + rnd(state, std::max(1, w / 2));
    int y = 8 + rnd(state, std::max(1, h / 2));
    int cw = 80 + rnd(state, std::max(1, w / 3));
    int ch = 22 + rnd(state, std::max(1, h / 4));
    SetWindowPos(hwnd, NULL, x, y, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  pump_messages();
}

static void bench_paint_update(WindowBenchState &s, int, int, uint64_t iter)
{
  if (!s.paint) return;
  RECT r;
  GetClientRect(s.paint, &r);
  uint32_t state = (uint32_t)iter * 911u + 7u;
  for (int i = 0; i < 64; ++i)
  {
    RECT dirty = {
      rnd(state, std::max(1, r.right - 24)),
      rnd(state, std::max(1, r.bottom - 24)),
      0,
      0
    };
    dirty.right = std::min(r.right, dirty.left + 24 + rnd(state, 160));
    dirty.bottom = std::min(r.bottom, dirty.top + 24 + rnd(state, 96));
    const uint64_t before = s.paint_count;
    InvalidateRect(s.paint, &dirty, 0);
    UpdateWindow(s.paint);
    if (s.paint_count == before)
      SendMessage(s.paint, WM_PAINT, 0, 0);
  }
}

static void bench_widget_messages(WindowBenchState &s, int, int, uint64_t iter)
{
  for (int i = 0; i < 96; ++i)
  {
    char buf[96];
    snprintf(buf, sizeof(buf), "edit value %llu.%d", (unsigned long long)iter, i);
    SetDlgItemText(s.main, IDC_EDIT, buf);
    SendMessage(s.combo, CB_SETCURSEL, i % 24, 0);
    SendMessage(s.listbox, LB_SETCURSEL, i % 64, 0);
    CheckDlgButton(s.main, IDC_CHECK, i & 1);
    SendMessage(s.slider, TBM_SETPOS, 1, i % 101);
    SendMessage(s.progress, PBM_SETPOS, i % 101, 0);
  }
  pump_messages();
}

static void bench_textedit_selection(WindowBenchState &s, int, int, uint64_t iter)
{
  if (!s.long_edit || s.long_text_len <= 0) return;

  uint32_t state = (uint32_t)iter * 3571u + 43u;
  for (int i = 0; i < 96; ++i)
  {
    int start = rnd(state, std::max(1, s.long_text_len - 1024));
    int end = std::min(s.long_text_len, start + 32 + rnd(state, 768));
    SendMessage(s.long_edit, EM_SETSEL, start, end);

    int got_start = 0;
    int got_end = 0;
    SendMessage(s.long_edit, EM_GETSEL, (WPARAM)&got_start, (LPARAM)&got_end);
    if ((i & 7) == 0)
    {
      InvalidateRect(s.long_edit, NULL, 0);
      UpdateWindow(s.long_edit);
      SendMessage(s.long_edit, WM_PAINT, 0, 0);
    }
  }
  pump_messages();
}

static void bench_listview_churn(WindowBenchState &s, int, int, uint64_t iter)
{
  if (!s.listview) return;
  ListView_DeleteAllItems(s.listview);
  for (int i = 0; i < 96; ++i)
  {
    char name[64];
    char value[64];
    snprintf(name, sizeof(name), "item %llu.%02d", (unsigned long long)iter, i);
    snprintf(value, sizeof(value), "value %d", i * 3);
    LVITEM item = {0};
    item.mask = LVIF_TEXT;
    item.iItem = i;
    item.pszText = name;
    ListView_InsertItem(s.listview, &item);
    ListView_SetItemText(s.listview, i, 1, value);
  }
  ListView_RedrawItems(s.listview, 0, 95);
  pump_messages();
}

static void bench_treeview_churn(WindowBenchState &s, int, int, uint64_t iter)
{
  if (!s.treeview) return;
  TreeView_DeleteAllItems(s.treeview);
  for (int i = 0; i < 24; ++i)
  {
    char root_text[64];
    snprintf(root_text, sizeof(root_text), "root %llu.%02d", (unsigned long long)iter, i);
    TVINSERTSTRUCT root = {0};
    root.hParent = TVI_ROOT;
    root.hInsertAfter = TVI_LAST;
    root.item.mask = TVIF_TEXT | TVIF_CHILDREN;
    root.item.pszText = root_text;
    root.item.cChildren = 1;
    HTREEITEM hroot = TreeView_InsertItem(s.treeview, &root);
    for (int c = 0; c < 3; ++c)
    {
      char child_text[64];
      snprintf(child_text, sizeof(child_text), "child %02d", c);
      TVINSERTSTRUCT child = {0};
      child.hParent = hroot;
      child.hInsertAfter = TVI_LAST;
      child.item.mask = TVIF_TEXT;
      child.item.pszText = child_text;
      TreeView_InsertItem(s.treeview, &child);
    }
    TreeView_Expand(s.treeview, hroot, TVE_EXPAND);
  }
  pump_messages();
}

static void bench_tab_menu(WindowBenchState &s, int, int, uint64_t iter)
{
  for (int i = 0; i < 96; ++i)
  {
    TabCtrl_SetCurSel(s.tab, i % 6);
    HMENU bar = CreatePopupMenu();
    if (bar)
    {
      AddMenuItem(bar, -1, "File", 100);
      AddMenuItem(bar, -1, "Edit", 101);
      AddMenuItem(bar, -1, "View", 102);
      SetMenu(s.main, bar);
      DrawMenuBar(s.main);
      SetMenu(s.main, NULL);
    }
    if (bar) DestroyMenu(bar);
  }
  (void)iter;
  pump_messages();
}

static void bench_dialog_create_destroy(WindowBenchState &, int, int, uint64_t)
{
  for (int i = 0; i < 16; ++i)
  {
    HWND h = CreateDialog(NULL, MAKEINTRESOURCE(IDD_WINDOW_BENCH_TEMP), NULL, TempDlgProc);
    if (h)
    {
      ShowWindow(h, SW_HIDE);
      pump_messages();
      DestroyWindow(h);
    }
  }
  pump_messages();
}

static void bench_mixed_window(WindowBenchState &s, int w, int h, uint64_t iter)
{
  bench_resize_window(s, w, h, iter);
  bench_control_layout(s, w, h, iter);
  bench_paint_update(s, w, h, iter);
  bench_widget_messages(s, w, h, iter);
  bench_textedit_selection(s, w, h, iter);
  bench_listview_churn(s, w, h, iter);
}

static const Bench kBenches[] = {
  {"resize_window", "windowing", 48, bench_resize_window},
  {"control_layout", "windowing", 10, bench_control_layout},
  {"paint_update", "painting", 64, bench_paint_update},
  {"widget_messages", "widgets", 576, bench_widget_messages},
  {"textedit_selection", "widgets", 204, bench_textedit_selection},
  {"listview_churn", "widgets", 288, bench_listview_churn},
  {"treeview_churn", "widgets", 120, bench_treeview_churn},
  {"tab_menu", "widgets", 480, bench_tab_menu},
  {"dialog_create_destroy", "windowing", 64, bench_dialog_create_destroy},
  {"mixed_window", "mixed", 1190, bench_mixed_window},
};

static bool has_bench(const Options &opt, const char *name)
{
  if (opt.selected.empty()) return true;
  return std::find(opt.selected.begin(), opt.selected.end(), name) != opt.selected.end();
}

static void usage(const char *argv0)
{
  fprintf(stderr,
          "usage: %s [--duration-ms N] [--width N] [--height N] [--bench NAME] [--pretty]\n",
          argv0);
  fprintf(stderr, "benches:");
  for (const Bench &b : kBenches) fprintf(stderr, " %s", b.name);
  fprintf(stderr, "\n");
}

static bool parse_int_arg(const char *s, int *out)
{
  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (!s[0] || (end && *end) || v < 1 || v > std::numeric_limits<int>::max()) return false;
  *out = (int)v;
  return true;
}

static bool parse_options(int argc, const char **argv, Options *opt)
{
  for (int i = 1; i < argc; ++i)
  {
    if (!strcmp(argv[i], "--duration-ms") && i + 1 < argc)
    {
      if (!parse_int_arg(argv[++i], &opt->duration_ms)) return false;
    }
    else if (!strcmp(argv[i], "--width") && i + 1 < argc)
    {
      if (!parse_int_arg(argv[++i], &opt->width)) return false;
    }
    else if (!strcmp(argv[i], "--height") && i + 1 < argc)
    {
      if (!parse_int_arg(argv[++i], &opt->height)) return false;
    }
    else if (!strcmp(argv[i], "--bench") && i + 1 < argc)
    {
      opt->selected.push_back(argv[++i]);
    }
    else if (!strcmp(argv[i], "--pretty"))
    {
      opt->pretty = true;
    }
    else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
    {
      usage(argv[0]);
      exit(0);
    }
    else
    {
      return false;
    }
  }
  return true;
}

static bool init_window(const Options &opt)
{
  memset(&g_state, 0, sizeof(g_state));
  g_state.main = CreateDialog(NULL, MAKEINTRESOURCE(IDD_WINDOW_BENCH), NULL, MainDlgProc);
  if (!g_state.main) return false;
  SetWindowPos(g_state.main, NULL, 30, 30, opt.width, opt.height, SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(g_state.main, SW_SHOW);
  pump_messages(4);
  return true;
}

static void destroy_window()
{
  if (g_state.main)
  {
    DestroyWindow(g_state.main);
    g_state.main = NULL;
    pump_messages(4);
  }
}

static BenchResult run_bench(const Bench &bench, const Options &opt)
{
  using clock = std::chrono::steady_clock;

  BenchResult result;
  result.name = bench.name;
  result.category = bench.category;
  result.target_ms = opt.duration_ms;

  const auto warm_until = clock::now() + std::chrono::milliseconds(50);
  uint64_t warm_iter = 0;
  while (clock::now() < warm_until)
    bench.run(g_state, opt.width, opt.height, warm_iter++);

  const uint64_t paint0 = g_state.paint_count;
  const uint64_t size0 = g_state.size_count;
  const uint64_t command0 = g_state.command_count;
  const uint64_t notify0 = g_state.notify_count;

  const auto start = clock::now();
  const auto deadline = start + std::chrono::milliseconds(opt.duration_ms);
  uint64_t iterations = 0;
  while (clock::now() < deadline)
  {
    bench.run(g_state, opt.width, opt.height, iterations);
    ++iterations;
  }
  const auto end = clock::now();
  pump_messages(2);

  result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
  result.iterations = iterations;
  result.operations = iterations * bench.ops_per_iteration;
  result.paint_count = g_state.paint_count - paint0;
  result.size_count = g_state.size_count - size0;
  result.command_count = g_state.command_count - command0;
  result.notify_count = g_state.notify_count - notify0;
  if (result.elapsed_ms > 0.0 && result.operations > 0)
  {
    result.ops_per_second = (double)result.operations * 1000.0 / result.elapsed_ms;
    result.ns_per_op = result.elapsed_ms * 1000000.0 / (double)result.operations;
  }
  return result;
}

static void json_indent(bool pretty, int n)
{
  if (pretty)
  {
    putchar('\n');
    for (int i = 0; i < n; ++i) putchar(' ');
  }
}

static void json_string(const char *s)
{
  putchar('"');
  for (; *s; ++s)
  {
    switch (*s)
    {
      case '\\': fputs("\\\\", stdout); break;
      case '"': fputs("\\\"", stdout); break;
      case '\n': fputs("\\n", stdout); break;
      case '\r': fputs("\\r", stdout); break;
      case '\t': fputs("\\t", stdout); break;
      default: putchar(*s); break;
    }
  }
  putchar('"');
}

static void print_json(const Options &opt, const std::vector<BenchResult> &results)
{
  const bool p = opt.pretty;
  const char *label = getenv("SWELL_BENCH_LABEL");
  if (!label) label = "";

  putchar('{');
  json_indent(p, 2); fputs("\"app\":", stdout); if (p) putchar(' '); json_string("bench_swell_window_app"); putchar(',');
  json_indent(p, 2); fputs("\"label\":", stdout); if (p) putchar(' '); json_string(label); putchar(',');
  json_indent(p, 2); fputs("\"width\":", stdout); if (p) putchar(' '); printf("%d,", opt.width);
  json_indent(p, 2); fputs("\"height\":", stdout); if (p) putchar(' '); printf("%d,", opt.height);
  json_indent(p, 2); fputs("\"duration_ms\":", stdout); if (p) putchar(' '); printf("%d,", opt.duration_ms);
  json_indent(p, 2); fputs("\"scaling256\":", stdout); if (p) putchar(' '); printf("%d,", SWELL_GetScaling256());
  json_indent(p, 2); fputs("\"benchmarks\":", stdout); if (p) putchar(' '); putchar('[');

  for (size_t i = 0; i < results.size(); ++i)
  {
    const BenchResult &r = results[i];
    if (i) putchar(',');
    json_indent(p, 4); putchar('{');
    json_indent(p, 6); fputs("\"name\":", stdout); if (p) putchar(' '); json_string(r.name.c_str()); putchar(',');
    json_indent(p, 6); fputs("\"category\":", stdout); if (p) putchar(' '); json_string(r.category.c_str()); putchar(',');
    json_indent(p, 6); fputs("\"target_ms\":", stdout); if (p) putchar(' '); printf("%d,", r.target_ms);
    json_indent(p, 6); fputs("\"elapsed_ms\":", stdout); if (p) putchar(' '); printf("%.3f,", r.elapsed_ms);
    json_indent(p, 6); fputs("\"iterations\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.iterations);
    json_indent(p, 6); fputs("\"operations\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.operations);
    json_indent(p, 6); fputs("\"paint_count\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.paint_count);
    json_indent(p, 6); fputs("\"size_count\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.size_count);
    json_indent(p, 6); fputs("\"command_count\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.command_count);
    json_indent(p, 6); fputs("\"notify_count\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.notify_count);
    json_indent(p, 6); fputs("\"ops_per_second\":", stdout); if (p) putchar(' '); printf("%.3f,", r.ops_per_second);
    json_indent(p, 6); fputs("\"ns_per_op\":", stdout); if (p) putchar(' '); printf("%.3f", r.ns_per_op);
    json_indent(p, 4); putchar('}');
  }

  json_indent(p, 2); putchar(']');
  json_indent(p, 0); putchar('}');
  putchar('\n');
}

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN2(IDD_WINDOW_BENCH, SWELL_DLG_WS_RESIZABLE | SWELL_DLG_WS_OPAQUE, "SWELL Window Benchmark", 760, 520)
BEGIN
LTEXT "Window benchmark", IDC_LABEL, 10, 8, 180, 18
EDITTEXT IDC_EDIT, 10, 30, 220, 24, ES_AUTOHSCROLL | WS_TABSTOP
COMBOBOX IDC_COMBO, 240, 30, 180, 160, CBS_DROPDOWNLIST | WS_TABSTOP
LISTBOX IDC_LISTBOX, 10, 64, 220, 160, LBS_NOTIFY | WS_TABSTOP
CHECKBOX "Check", IDC_CHECK, 240, 64, 120, 24, BS_AUTOCHECKBOX | WS_TABSTOP
CONTROL "", IDC_SLIDER, "msctls_trackbar32", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 240, 96, 180, 28
CONTROL "", IDC_PROGRESS, "msctls_progress32", WS_CHILD | WS_VISIBLE, 240, 136, 180, 20
CONTROL "", IDC_TAB, "SysTabControl32", WS_CHILD | WS_VISIBLE, 440, 30, 300, 80
EDITTEXT IDC_LONG_EDIT, 440, 116, 300, 76, ES_MULTILINE | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP
CONTROL "", IDC_LISTVIEW, "SysListView32", WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_TABSTOP, 10, 240, 350, 250
CONTROL "", IDC_TREEVIEW, "SysTreeView32", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | WS_TABSTOP, 380, 204, 350, 116
CONTROL "", IDC_PAINT, "Static", WS_CHILD | WS_VISIBLE, 380, 330, 350, 160
END
SWELL_DEFINE_DIALOG_RESOURCE_END2(IDD_WINDOW_BENCH)

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN2(IDD_WINDOW_BENCH_TEMP, SWELL_DLG_WS_OPAQUE, "Temp", 220, 120)
BEGIN
EDITTEXT IDC_TEMP_EDIT, 10, 10, 180, 22, ES_AUTOHSCROLL | WS_TABSTOP
PUSHBUTTON "OK", IDC_TEMP_BUTTON, 10, 40, 80, 22
LISTBOX IDC_TEMP_LISTBOX, 10, 70, 180, 40, LBS_NOTIFY | WS_TABSTOP
END
SWELL_DEFINE_DIALOG_RESOURCE_END2(IDD_WINDOW_BENCH_TEMP)

INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  (void)msg;
  (void)parm1;
  (void)parm2;
  return 0;
}

#ifndef __APPLE__
int main(int argc, const char **argv)
{
  Options opt;
  if (!parse_options(argc, argv, &opt))
  {
    usage(argv[0]);
    return 64;
  }

  for (const std::string &name : opt.selected)
  {
    bool found = false;
    for (const Bench &b : kBenches)
      if (name == b.name) found = true;
    if (!found)
    {
      fprintf(stderr, "unknown benchmark: %s\n", name.c_str());
      usage(argv[0]);
      return 64;
    }
  }

  SWELL_initargs(&argc, (char ***)&argv);
  SWELL_Internal_PostMessage_Init();
  SWELL_ExtendedAPI("APPNAME", (void *)"BenchSwellWindowApp");

  if (!init_window(opt))
  {
    fprintf(stderr, "failed to create benchmark dialog\n");
    return 2;
  }

  std::vector<BenchResult> results;
  for (const Bench &b : kBenches)
  {
    if (has_bench(opt, b.name))
      results.push_back(run_bench(b, opt));
  }

  destroy_window();
  print_json(opt, results);
  return 0;
}
#endif
