/*
  SWELL2 Lua scripting engine.
  Activated via SWELL_PROF_SCRIPT=path/to/script.lua env var.
  Provides ~30 swell bindings for automated testing, profiling, fuzzing.

  Script callbacks (optional):
    tick()       — called every SWELL_RunMessageLoop iteration
    on_frame(hwnd, frame_ms, painted) — called after paint

  Compiles only when SWELL2_HAS_LUA is defined (pkg-config lua found).
  Otherwise all functions are no-ops.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "swell-internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>

#ifdef SWELL2_HAS_LUA
// Lua 5.4+ headers do NOT automatically wrap in extern "C" when
// compiled as C++ (Lua itself can be compiled as C++). Use lua.hpp.
#include <lua.hpp>

// ---- globals ----
static lua_State *g_lua = nullptr;
static bool g_lua_had_error = false;
static std::chrono::steady_clock::time_point g_lua_init_time;

// ---- helper: get env var or default ----
static const char *lua_getenv_def(const char *name, const char *defval)
{
  const char *v = getenv(name);
  return (v && v[0]) ? v : defval;
}

// ---- helper: convert HWND to a metatable-wrapped string key ----
// We use lightuserdata for HWND pointers. The script passes them
// between bindings; pointer identity is == equality in Lua.
static inline void push_hwnd(lua_State *L, HWND hwnd)
{
  lua_pushlightuserdata(L, hwnd);
}

static inline HWND to_hwnd(lua_State *L, int idx)
{
  return (HWND)lua_touserdata(L, idx);
}

// ---- bindings ----

static int l_find_window(lua_State *L)
{
  const char *cls = luaL_optstring(L, 1, nullptr);
  const char *title = luaL_optstring(L, 2, nullptr);
  HWND h = FindWindowEx(nullptr, nullptr, cls, title);
  push_hwnd(L, h);
  return 1;
}

static int l_find_window_ex(lua_State *L)
{
  HWND par = to_hwnd(L, 1);
  HWND after = to_hwnd(L, 2);
  const char *cls = luaL_optstring(L, 3, nullptr);
  const char *title = luaL_optstring(L, 4, nullptr);
  HWND h = FindWindowEx(par, after, cls, title);
  push_hwnd(L, h);
  return 1;
}

static int l_get_class(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (h && h->m_classname)
    lua_pushstring(L, h->m_classname);
  else
    lua_pushnil(L);
  return 1;
}

static int l_get_text(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (h)
    lua_pushstring(L, h->m_title.Get());
  else
    lua_pushnil(L);
  return 1;
}

static int l_get_rect(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  RECT r;
  GetWindowRect(h, &r);
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, r.left);  lua_setfield(L, -2, "x");
  lua_pushinteger(L, r.top);   lua_setfield(L, -2, "y");
  lua_pushinteger(L, r.right - r.left); lua_setfield(L, -2, "w");
  lua_pushinteger(L, r.bottom - r.top); lua_setfield(L, -2, "h");
  return 1;
}

static int l_get_id(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  lua_pushinteger(L, h->m_id);
  return 1;
}

static int l_get_style(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  lua_pushinteger(L, (lua_Integer)h->m_style);
  return 1;
}

static int l_is_visible(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  lua_pushboolean(L, h->m_visible ? 1 : 0);
  return 1;
}

static int l_is_enabled(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  lua_pushboolean(L, h->m_enabled ? 1 : 0);
  return 1;
}

static int l_has_focus(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  lua_pushboolean(L, (h == g_swell_focus) ? 1 : 0);
  return 1;
}

static int l_get_parent(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h || !h->m_parent) { lua_pushnil(L); return 1; }
  push_hwnd(L, h->m_parent);
  return 1;
}

static int l_get_children(lua_State *L)
{
  HWND hwnd = to_hwnd(L, 1);
  HWND ch = hwnd ? GetWindow(hwnd, GW_CHILD) : g_swell_top_level_list;
  lua_createtable(L, 0, 0);
  int i = 1;
  while (ch) {
    push_hwnd(L, ch);
    lua_rawseti(L, -2, i++);
    ch = GetWindow(ch, GW_HWNDNEXT);
  }
  return 1;
}

static int l_get_child_by_id(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  int id = (int)luaL_checkinteger(L, 2);
  if (!h) { lua_pushnil(L); return 1; }
  HWND ch = GetDlgItem(h, id);
  push_hwnd(L, ch);
  return 1;
}

static int l_enum_windows(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TFUNCTION);
  HWND w = g_swell_top_level_list;
  while (w) {
    lua_pushvalue(L, 1);           // fn
    push_hwnd(L, w);               // hwnd
    lua_pushstring(L, w->m_classname ? w->m_classname : "");
    lua_pushstring(L, w->m_title.Get());
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
      fprintf(stderr, "swell-lua: enum_windows callback error: %s\n",
              lua_tostring(L, -1));
      lua_pop(L, 1);
      break;
    }
    int cont = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (!cont) break;
    w = GetWindow(w, GW_HWNDNEXT);
  }
  return 0;
}

static int l_show_window(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  int cmd = (int)luaL_checkinteger(L, 2);
  if (h) ShowWindow(h, cmd);
  return 0;
}

static int l_set_foreground(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (h) SetForegroundWindow(h);
  return 0;
}

static int l_invalidate(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) return 0;
  // optional x,y,w,h; if omitted invalidate entire client area
  int nargs = lua_gettop(L);
  if (nargs >= 5) {
    RECT r;
    r.left   = (int)luaL_checkinteger(L, 2);
    r.top    = (int)luaL_checkinteger(L, 3);
    r.right  = r.left + (int)luaL_checkinteger(L, 4);
    r.bottom = r.top  + (int)luaL_checkinteger(L, 5);
    InvalidateRect(h, &r, FALSE);
  } else {
    InvalidateRect(h, nullptr, FALSE);
  }
  return 0;
}

static int l_post_message(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  UINT msg = (UINT)luaL_checkinteger(L, 2);
  WPARAM wp = (WPARAM)(intptr_t)luaL_checknumber(L, 3);
  LPARAM lp = (LPARAM)(intptr_t)luaL_checknumber(L, 4);
  if (h) PostMessage(h, msg, wp, lp);
  return 0;
}

static int l_send_message(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  UINT msg = (UINT)luaL_checkinteger(L, 2);
  WPARAM wp = (WPARAM)(intptr_t)luaL_checknumber(L, 3);
  LPARAM lp = (LPARAM)(intptr_t)luaL_checknumber(L, 4);
  if (!h) { lua_pushnil(L); return 1; }
  LRESULT r = SendMessage(h, msg, wp, lp);
  lua_pushinteger(L, (lua_Integer)r);
  return 1;
}

static int l_now_sec(lua_State *L)
{
  using namespace std::chrono;
  double t = duration<double>(steady_clock::now() - g_lua_init_time).count();
  lua_pushnumber(L, t);
  return 1;
}

static int l_sleep_ms(lua_State *L)
{
  int ms = (int)luaL_checkinteger(L, 1);
  usleep(ms * 1000);
  return 0;
}

static int l_print(lua_State *L)
{
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    if (i > 1) fputc('\t', stdout);
    const char *s = lua_tostring(L, i);
    if (s) fputs(s, stdout);
  }
  fputc('\n', stdout);
  fflush(stdout);
  return 0;
}

static int l_exit(lua_State *L)
{
  int code = (int)luaL_optinteger(L, 1, 0);
  _exit(code);
  return 0;
}

static int l_write_file(lua_State *L)
{
  const char *path = luaL_checkstring(L, 1);
  const char *data = luaL_checkstring(L, 2);
  FILE *f = fopen(path, "w");
  if (!f) { lua_pushboolean(L, 0); return 1; }
  fputs(data, f);
  fclose(f);
  lua_pushboolean(L, 1);
  return 1;
}

static int l_read_file(lua_State *L)
{
  const char *path = luaL_checkstring(L, 1);
  FILE *f = fopen(path, "r");
  if (!f) { lua_pushnil(L); return 1; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc(sz + 1);
  if (!buf) { fclose(f); lua_pushnil(L); return 1; }
  fread(buf, 1, sz, f);
  buf[sz] = 0;
  fclose(f);
  lua_pushstring(L, buf);
  free(buf);
  return 1;
}

static int l_get_window_rect_screen(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  RECT r;
  if (!GetWindowRect(h, &r)) { lua_pushnil(L); return 1; }
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, r.left);  lua_setfield(L, -2, "x");
  lua_pushinteger(L, r.top);   lua_setfield(L, -2, "y");
  lua_pushinteger(L, r.right - r.left); lua_setfield(L, -2, "w");
  lua_pushinteger(L, r.bottom - r.top); lua_setfield(L, -2, "h");
  return 1;
}

static int l_get_cursor_pos(lua_State *L)
{
  POINT pt;
  GetCursorPos(&pt);
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, pt.x); lua_setfield(L, -2, "x");
  lua_pushinteger(L, pt.y); lua_setfield(L, -2, "y");
  return 1;
}

static int l_set_cursor_pos(lua_State *L)
{
  int x = (int)luaL_checkinteger(L, 1);
  int y = (int)luaL_checkinteger(L, 2);
  SetCursorPos(x, y);
  return 0;
}

static int l_get_client_rect(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  if (!h) { lua_pushnil(L); return 1; }
  RECT r;
  GetClientRect(h, &r);
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, r.left);  lua_setfield(L, -2, "x");
  lua_pushinteger(L, r.top);   lua_setfield(L, -2, "y");
  lua_pushinteger(L, r.right);  lua_setfield(L, -2, "w");
  lua_pushinteger(L, r.bottom); lua_setfield(L, -2, "h");
  return 1;
}

static int l_client_to_screen(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  int x = (int)luaL_checkinteger(L, 2);
  int y = (int)luaL_checkinteger(L, 3);
  if (!h) { lua_pushnil(L); return 1; }
  POINT pt = {x, y};
  ClientToScreen(h, &pt);
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, pt.x); lua_setfield(L, -2, "x");
  lua_pushinteger(L, pt.y); lua_setfield(L, -2, "y");
  return 1;
}

static int l_screen_to_client(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  int x = (int)luaL_checkinteger(L, 2);
  int y = (int)luaL_checkinteger(L, 3);
  if (!h) { lua_pushnil(L); return 1; }
  POINT pt = {x, y};
  ScreenToClient(h, &pt);
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, pt.x); lua_setfield(L, -2, "x");
  lua_pushinteger(L, pt.y); lua_setfield(L, -2, "y");
  return 1;
}

static int l_fullscreen(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  bool on = lua_toboolean(L, 2);
  if (h)
    SWELL_ExtendedAPI(on ? "FULLSCREEN" : "-FULLSCREEN", h);
  return 0;
}

static int l_set_window_pos(lua_State *L)
{
  HWND h = to_hwnd(L, 1);
  int x = (int)luaL_optinteger(L, 2, 0);
  int y = (int)luaL_optinteger(L, 3, 0);
  int w = (int)luaL_optinteger(L, 4, 0);
  int hh = (int)luaL_optinteger(L, 5, 0);
  int flags = (int)luaL_optinteger(L, 6, 0);
  if (h) SetWindowPos(h, nullptr, x, y, w, hh, flags|SWP_NOZORDER);
  return 0;
}

// ---- binding table ----
static const luaL_Reg g_swell_bindings[] = {
  {"find_window",     l_find_window},
  {"find_window_ex",  l_find_window_ex},
  {"get_class",       l_get_class},
  {"get_text",        l_get_text},
  {"get_rect",        l_get_rect},
  {"get_id",          l_get_id},
  {"get_style",       l_get_style},
  {"is_visible",      l_is_visible},
  {"is_enabled",      l_is_enabled},
  {"has_focus",       l_has_focus},
  {"get_parent",      l_get_parent},
  {"get_children",    l_get_children},
  {"get_child",       l_get_child_by_id},
  {"enum_windows",    l_enum_windows},
  {"show_window",     l_show_window},
  {"set_foreground",  l_set_foreground},
  {"invalidate",      l_invalidate},
  {"post_message",    l_post_message},
  {"send_message",    l_send_message},
  {"get_cursor_pos",  l_get_cursor_pos},
  {"set_cursor_pos",  l_set_cursor_pos},
  {"get_client_rect", l_get_client_rect},
  {"client_to_screen",l_client_to_screen},
  {"screen_to_client",l_screen_to_client},
  {"fullscreen",      l_fullscreen},
  {"set_window_pos",  l_set_window_pos},
  {"now_sec",         l_now_sec},
  {"sleep_ms",        l_sleep_ms},
  {"print",           l_print},
  {"exit",            l_exit},
  {"write_file",      l_write_file},
  {"read_file",       l_read_file},
  {nullptr, nullptr}
};

// ---- global SWELL table setup ----
static void swell_lua_setup_globals(lua_State *L)
{
  luaL_newlibtable(L, g_swell_bindings);
  luaL_setfuncs(L, g_swell_bindings, 0);
  lua_setglobal(L, "swell");

  // expose common Win32 constants under swell.* for message posting
  auto push_const = [&](const char *name, int val) {
    lua_getglobal(L, "swell");
    lua_pushinteger(L, val);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
  };
  push_const("SW_HIDE",          0);
  push_const("SW_SHOWNORMAL",    1);
  push_const("SW_SHOW",          1);
  push_const("SW_SHOWMAXIMIZED", 3);
  push_const("SW_SHOWMINIMIZED", 2);
  push_const("SW_RESTORE",       5);

  push_const("SWP_NOSIZE",       0x0001);
  push_const("SWP_NOMOVE",       0x0002);
  push_const("SWP_NOZORDER",     0x0004);
  push_const("SWP_NOREDRAW",     0x0008);
  push_const("SWP_NOACTIVATE",   0x0010);
  push_const("SWP_SHOWWINDOW",   0x0040);

  push_const("WM_KEYDOWN",       0x0100);
  push_const("WM_KEYUP",         0x0101);
  push_const("WM_CHAR",          0x0102);
  push_const("WM_MOUSEWHEEL",    0x020A);
  push_const("WM_MOUSEHWHEEL",   0x020E);
  push_const("WM_LBUTTONDOWN",   0x0201);
  push_const("WM_LBUTTONUP",     0x0202);
  push_const("WM_RBUTTONDOWN",   0x0204);
  push_const("WM_RBUTTONUP",     0x0205);
  push_const("WM_COMMAND",       0x0111);
  push_const("WM_NOTIFY",        0x004E);
  push_const("WM_SIZE",          0x0005);
  push_const("WM_MOVE",          0x0003);
  push_const("WM_CLOSE",         0x0010);
  push_const("WM_DESTROY",       0x0002);

  push_const("VK_CONTROL",       0x11);
  push_const("VK_SHIFT",         0x10);
  push_const("VK_MENU",          0x12);   // alt
  push_const("VK_RETURN",        0x0D);
  push_const("VK_ESCAPE",        0x1B);
  push_const("VK_BACK",          0x08);
  push_const("VK_TAB",           0x09);
  push_const("VK_LEFT",          0x25);
  push_const("VK_UP",            0x26);
  push_const("VK_RIGHT",         0x27);
  push_const("VK_DOWN",          0x28);
  push_const("VK_ADD",           0x6B);
  push_const("VK_SUBTRACT",      0x6D);
}

// ---- call a Lua function by name, safely ----
static void swell_lua_call_fn(const char *name, int nargs)
{
  if (!g_lua || g_lua_had_error) return;
  lua_getglobal(g_lua, name);
  if (lua_isnil(g_lua, -1)) {
    lua_pop(g_lua, 1);
    return;
  }
  // move function before args
  if (nargs > 0)
    lua_insert(g_lua, -(nargs + 1));

  if (lua_pcall(g_lua, nargs, 0, 0) != LUA_OK) {
    fprintf(stderr, "swell-lua: error in %s(): %s\n",
            name, lua_tostring(g_lua, -1));
    lua_pop(g_lua, 1);
    g_lua_had_error = true;
  }
}

// ---- public API ----

void swell_lua_init()
{
  if (g_lua) return;

  const char *script_path = getenv("SWELL_PROF_SCRIPT");
  if (!script_path || !script_path[0]) return;

  g_lua = luaL_newstate();
  if (!g_lua) {
    fprintf(stderr, "swell-lua: failed to create Lua state\n");
    return;
  }
  luaL_openlibs(g_lua);
  swell_lua_setup_globals(g_lua);

  g_lua_init_time = std::chrono::steady_clock::now();

  if (luaL_loadfile(g_lua, script_path) != LUA_OK ||
      lua_pcall(g_lua, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "swell-lua: script error: %s\n",
            lua_tostring(g_lua, -1));
    lua_pop(g_lua, 1);
    g_lua_had_error = true;
  }
}

void swell_lua_tick()
{
  swell_lua_init();
  swell_lua_call_fn("tick", 0);
}

void swell_lua_notify_frame(HWND hwnd, double frame_ms, bool painted)
{
  swell_lua_init();
  if (!g_lua || g_lua_had_error) return;

  lua_getglobal(g_lua, "on_frame");
  if (lua_type(g_lua, -1) != LUA_TFUNCTION) {
    lua_pop(g_lua, 1);
    return;
  }
  push_hwnd(g_lua, hwnd);
  lua_pushnumber(g_lua, frame_ms);
  lua_pushboolean(g_lua, painted ? 1 : 0);

  if (lua_pcall(g_lua, 3, 0, 0) != LUA_OK) {
    fprintf(stderr, "swell-lua: error in on_frame(): %s\n",
            lua_tostring(g_lua, -1));
    lua_pop(g_lua, 1);
    g_lua_had_error = true;
  }
}

void swell_lua_shutdown()
{
  if (g_lua) {
    swell_lua_call_fn("shutdown", 0);
    lua_close(g_lua);
    g_lua = nullptr;
  }
}

#else // !SWELL2_HAS_LUA

void swell_lua_init() {}
void swell_lua_tick() {}
void swell_lua_notify_frame(HWND, double, bool) {}
void swell_lua_shutdown() {}

#endif
