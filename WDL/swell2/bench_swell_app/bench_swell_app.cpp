#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "swell.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

struct Options
{
  int width = 800;
  int height = 600;
  int duration_ms = 1000;
  bool pretty = false;
  std::vector<std::string> selected;
};

struct BenchResult
{
  std::string name;
  std::string category;
  int width = 0;
  int height = 0;
  int target_ms = 0;
  double elapsed_ms = 0.0;
  uint64_t iterations = 0;
  uint64_t operations = 0;
  double ops_per_second = 0.0;
  double ns_per_op = 0.0;
};

struct Bench
{
  const char *name;
  const char *category;
  uint64_t ops_per_iteration;
  void (*setup)(HDC, HDC, int, int);
  void (*run)(HDC, HDC, int, int, uint64_t);
};

static uint32_t lcg_next(uint32_t &state)
{
  state = state * 1664525u + 1013904223u;
  return state;
}

static int rnd(uint32_t &state, int maxv)
{
  return maxv > 0 ? (int)(lcg_next(state) % (uint32_t)maxv) : 0;
}

static void fill_background(HDC hdc, int w, int h, int color)
{
  HBRUSH br = CreateSolidBrush(color);
  RECT r = {0, 0, w, h};
  FillRect(hdc, &r, br);
  DeleteObject((HGDIOBJ)br);
}

static void setup_target(HDC target, HDC source, int w, int h)
{
  fill_background(target, w, h, RGB(248, 249, 250));
  fill_background(source, w, h, RGB(30, 33, 38));

  for (int y = 0; y < h; y += 24)
  {
    HBRUSH br = CreateSolidBrush(RGB((y * 3) & 255, (80 + y) & 255, (180 - y) & 255));
    RECT r = {0, y, w, std::min(y + 24, h)};
    FillRect(source, &r, br);
    DeleteObject((HGDIOBJ)br);
  }
}

static void bench_fill_rect(HDC hdc, HDC, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 977u + 13u;
  HBRUSH brushes[8];
  for (int i = 0; i < 8; ++i)
    brushes[i] = CreateSolidBrush(RGB(30 + i * 23, 180 - i * 13, 70 + i * 17));

  for (int i = 0; i < 128; ++i)
  {
    int rw = 8 + rnd(state, 96);
    int rh = 6 + rnd(state, 72);
    int x = rnd(state, std::max(1, w - rw));
    int y = rnd(state, std::max(1, h - rh));
    RECT r = {x, y, x + rw, y + rh};
    FillRect(hdc, &r, brushes[i & 7]);
  }

  for (int i = 0; i < 8; ++i) DeleteObject((HGDIOBJ)brushes[i]);
}

static void bench_lines(HDC hdc, HDC, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 131u + 29u;
  HPEN pens[4];
  for (int i = 0; i < 4; ++i)
    pens[i] = CreatePen(PS_SOLID, 1 + (i & 1), RGB(50 + i * 40, 40 + i * 35, 210 - i * 30));

  POINT oldpt;
  for (int i = 0; i < 256; ++i)
  {
    HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)pens[i & 3]);
    MoveToEx(hdc, rnd(state, w), rnd(state, h), &oldpt);
    LineTo(hdc, rnd(state, w), rnd(state, h));
    SelectObject(hdc, old);
  }

  for (int i = 0; i < 4; ++i) DeleteObject((HGDIOBJ)pens[i]);
}

static void bench_shapes(HDC hdc, HDC, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 313u + 41u;
  HPEN pen = CreatePen(PS_SOLID, 2, RGB(40, 90, 170));
  HBRUSH brush = CreateSolidBrush(RGB(180, 210, 120));
  HGDIOBJ old_pen = SelectObject(hdc, (HGDIOBJ)pen);
  HGDIOBJ old_brush = SelectObject(hdc, (HGDIOBJ)brush);

  for (int i = 0; i < 48; ++i)
  {
    int x = rnd(state, std::max(1, w - 80));
    int y = rnd(state, std::max(1, h - 70));
    int rw = 20 + rnd(state, 80);
    int rh = 18 + rnd(state, 70);
    switch (i % 4)
    {
      case 0: Rectangle(hdc, x, y, x + rw, y + rh); break;
      case 1: Ellipse(hdc, x, y, x + rw, y + rh); break;
      case 2: RoundRect(hdc, x, y, x + rw, y + rh, 12, 12); break;
      default:
      {
        POINT pts[5] = {
          {x + rw / 2, y},
          {x + rw, y + rh / 3},
          {x + rw * 3 / 4, y + rh},
          {x + rw / 4, y + rh},
          {x, y + rh / 3}
        };
        Polygon(hdc, pts, 5);
        break;
      }
    }
  }

  SelectObject(hdc, old_brush);
  SelectObject(hdc, old_pen);
  DeleteObject((HGDIOBJ)brush);
  DeleteObject((HGDIOBJ)pen);
}

static void bench_polyline(HDC hdc, HDC, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 751u + 83u;
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(45, 120, 210));
  HGDIOBJ old_pen = SelectObject(hdc, (HGDIOBJ)pen);

  POINT pts[64];
  DWORD counts[4] = {16, 16, 16, 16};
  for (int i = 0; i < 64; ++i)
  {
    pts[i].x = rnd(state, w);
    pts[i].y = rnd(state, h);
  }
  PolyPolyline(hdc, pts, counts, 4);

  POINT bez[3];
  for (int i = 0; i < 32; ++i)
  {
    MoveToEx(hdc, rnd(state, w), rnd(state, h), NULL);
    for (int p = 0; p < 3; ++p)
    {
      bez[p].x = rnd(state, w);
      bez[p].y = rnd(state, h);
    }
    PolyBezierTo(hdc, bez, 3);
  }

  SelectObject(hdc, old_pen);
  DeleteObject((HGDIOBJ)pen);
}

static void bench_text_draw(HDC hdc, HDC, int w, int h, uint64_t iter)
{
  static const char *texts[] = {
    "SWELL DrawText benchmark",
    "The quick brown fox jumps over 0123456789",
    "Compact UI text: transport, mixer, arrange",
    "Skia-backed GDI text drawing"
  };
  uint32_t state = (uint32_t)iter * 911u + 7u;

  LOGFONT lf = {0};
  lf.lfHeight = -14 - (int)(iter % 4);
  lf.lfWeight = (iter & 1) ? 700 : 400;
  lstrcpyn(lf.lfFaceName, "Arial", 32);
  HFONT font = CreateFontIndirect(&lf);
  HGDIOBJ old_font = font ? SelectObject(hdc, (HGDIOBJ)font) : NULL;

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(20 + (int)(iter % 80), 40, 80));

  for (int i = 0; i < 96; ++i)
  {
    int x = rnd(state, std::max(1, w - 280));
    int y = rnd(state, std::max(1, h - 24));
    RECT r = {x, y, x + 280, y + 24};
    DrawText(hdc, texts[i & 3], -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOCLIP);
  }

  if (old_font) SelectObject(hdc, old_font);
  if (font) DeleteObject((HGDIOBJ)font);
}

static void bench_text_metrics(HDC hdc, HDC, int, int, uint64_t iter)
{
  LOGFONT lf = {0};
  lf.lfHeight = -12 - (int)(iter % 10);
  lf.lfWeight = (iter & 1) ? 700 : 400;
  lstrcpyn(lf.lfFaceName, (iter & 2) ? "Arial" : "Sans", 32);
  HFONT font = CreateFontIndirect(&lf);
  HGDIOBJ old_font = font ? SelectObject(hdc, (HGDIOBJ)font) : NULL;

  volatile int sink = 0;
  for (int i = 0; i < 256; ++i)
  {
    TEXTMETRIC tm = {0};
    if (GetTextMetrics(hdc, &tm)) sink += tm.tmHeight + tm.tmAveCharWidth;
    char face[64] = {0};
    sink += GetTextFace(hdc, sizeof(face), face);
  }
  (void)sink;

  if (old_font) SelectObject(hdc, old_font);
  if (font) DeleteObject((HGDIOBJ)font);
}

static void bench_bitblt(HDC hdc, HDC source, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 4099u + 101u;
  for (int i = 0; i < 128; ++i)
  {
    int bw = 16 + rnd(state, 96);
    int bh = 16 + rnd(state, 96);
    int sx = rnd(state, std::max(1, w - bw));
    int sy = rnd(state, std::max(1, h - bh));
    int dx = rnd(state, std::max(1, w - bw));
    int dy = rnd(state, std::max(1, h - bh));
    BitBlt(hdc, dx, dy, bw, bh, source, sx, sy, SRCCOPY);
  }
}

static void bench_stretchblt(HDC hdc, HDC source, int w, int h, uint64_t iter)
{
  uint32_t state = (uint32_t)iter * 6151u + 503u;
  for (int i = 0; i < 64; ++i)
  {
    int sw = 16 + rnd(state, 128);
    int sh = 16 + rnd(state, 128);
    int dw = 16 + rnd(state, 160);
    int dh = 16 + rnd(state, 160);
    int sx = rnd(state, std::max(1, w - sw));
    int sy = rnd(state, std::max(1, h - sh));
    int dx = rnd(state, std::max(1, w - dw));
    int dy = rnd(state, std::max(1, h - dh));
    StretchBlt(hdc, dx, dy, dw, dh, source, sx, sy, sw, sh, SRCCOPY);
  }
}

static void bench_mixed_frame(HDC hdc, HDC source, int w, int h, uint64_t iter)
{
  bench_fill_rect(hdc, source, w, h, iter);
  bench_lines(hdc, source, w, h, iter);
  bench_shapes(hdc, source, w, h, iter);
  bench_text_draw(hdc, source, w, h, iter);
  bench_bitblt(hdc, source, w, h, iter);
}

static const Bench kBenches[] = {
  {"fill_rect", "gdi", 128, setup_target, bench_fill_rect},
  {"lines", "gdi", 256, setup_target, bench_lines},
  {"shapes", "gdi", 48, setup_target, bench_shapes},
  {"polyline_bezier", "gdi", 36, setup_target, bench_polyline},
  {"text_draw", "text", 96, setup_target, bench_text_draw},
  {"text_metrics", "text", 512, setup_target, bench_text_metrics},
  {"bitblt", "gdi", 128, setup_target, bench_bitblt},
  {"stretchblt", "gdi", 64, setup_target, bench_stretchblt},
  {"mixed_frame", "mixed", 656, setup_target, bench_mixed_frame},
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

static BenchResult run_bench(const Bench &bench, const Options &opt)
{
  using clock = std::chrono::steady_clock;

  BenchResult result;
  result.name = bench.name;
  result.category = bench.category;
  result.width = opt.width;
  result.height = opt.height;
  result.target_ms = opt.duration_ms;

  HDC target = SWELL_CreateMemContext(NULL, opt.width, opt.height);
  HDC source = SWELL_CreateMemContext(NULL, opt.width, opt.height);
  if (!target || !source)
  {
    fprintf(stderr, "failed to create memory HDC for benchmark %s\n", bench.name);
    if (target) SWELL_DeleteGfxContext(target);
    if (source) SWELL_DeleteGfxContext(source);
    return result;
  }

  if (bench.setup) bench.setup(target, source, opt.width, opt.height);

  const auto warm_until = clock::now() + std::chrono::milliseconds(100);
  uint64_t warm_iter = 0;
  while (clock::now() < warm_until)
    bench.run(target, source, opt.width, opt.height, warm_iter++);

  const auto start = clock::now();
  const auto deadline = start + std::chrono::milliseconds(opt.duration_ms);
  uint64_t iterations = 0;
  while (clock::now() < deadline)
  {
    bench.run(target, source, opt.width, opt.height, iterations);
    ++iterations;
  }
  const auto end = clock::now();

  result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
  result.iterations = iterations;
  result.operations = iterations * bench.ops_per_iteration;
  if (result.elapsed_ms > 0.0 && result.operations > 0)
  {
    result.ops_per_second = (double)result.operations * 1000.0 / result.elapsed_ms;
    result.ns_per_op = result.elapsed_ms * 1000000.0 / (double)result.operations;
  }

  SWELL_DeleteGfxContext(source);
  SWELL_DeleteGfxContext(target);
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
  json_indent(p, 2); fputs("\"app\":", stdout); if (p) putchar(' '); json_string("bench_swell_app"); putchar(',');
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
    json_indent(p, 6); fputs("\"width\":", stdout); if (p) putchar(' '); printf("%d,", r.width);
    json_indent(p, 6); fputs("\"height\":", stdout); if (p) putchar(' '); printf("%d,", r.height);
    json_indent(p, 6); fputs("\"target_ms\":", stdout); if (p) putchar(' '); printf("%d,", r.target_ms);
    json_indent(p, 6); fputs("\"elapsed_ms\":", stdout); if (p) putchar(' '); printf("%.3f,", r.elapsed_ms);
    json_indent(p, 6); fputs("\"iterations\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.iterations);
    json_indent(p, 6); fputs("\"operations\":", stdout); if (p) putchar(' '); printf("%llu,", (unsigned long long)r.operations);
    json_indent(p, 6); fputs("\"ops_per_second\":", stdout); if (p) putchar(' '); printf("%.3f,", r.ops_per_second);
    json_indent(p, 6); fputs("\"ns_per_op\":", stdout); if (p) putchar(' '); printf("%.3f", r.ns_per_op);
    json_indent(p, 4); putchar('}');
  }

  json_indent(p, 2); putchar(']');
  json_indent(p, 0); putchar('}');
  putchar('\n');
}

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
  SWELL_ExtendedAPI("APPNAME", (void *)"BenchSwellApp");

  std::vector<BenchResult> results;
  for (const Bench &b : kBenches)
  {
    if (has_bench(opt, b.name))
      results.push_back(run_bench(b, opt));
  }

  print_json(opt, results);
  return 0;
}
#endif
