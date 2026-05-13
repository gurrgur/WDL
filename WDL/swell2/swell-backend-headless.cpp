/*
  SWELL2 headless backend — no display, all OS window functions are no-ops.
  WM_PAINT is never synthesized by OS expose events.
*/

#include "swell-internal.h"

// ---- swell_oswindow_* stubs ----

void swell_oswindow_manage(HWND hwnd, bool wantFocus)
{
}

void swell_oswindow_destroy(HWND hwnd)
{
}

void swell_oswindow_resize(HWND hwnd, int reposflag, RECT *r)
{
}

void swell_oswindow_focus(HWND hwnd)
{
}

void swell_oswindow_update_style(HWND hwnd, DWORD oldstyle)
{
}

void swell_oswindow_update_enable(HWND hwnd)
{
}

void swell_oswindow_update_text(HWND hwnd)
{
}

void swell_oswindow_invalidate(HWND hwnd, const RECT *r)
{
}

void swell_oswindow_updatetoscreen(HWND hwnd, const RECT *r)
{
}

// ---- SWELL_GetViewPort ----

void SWELL_GetViewPort(RECT *r, const RECT *sourcerect, bool wantWork)
{
  (void)sourcerect; (void)wantWork;
  if (r) { WinSetRect(r, 0, 0, 1920, 1080); }
}

HWND swell_oswindow_to_hwnd(SWELL_OSWINDOW osw)
{
  return NULL;
}

SWELL_OSWINDOW swell_oswindow_from_hwnd(HWND hwnd)
{
  return NULL;
}

// ---- SWELL_RunEvents ----

void SWELL_RunEvents()
{
}

// ---- SWELL_initargs ----

#ifndef SWELL_TARGET_OSX
void SWELL_initargs(int *argc, char ***argv)
{
}
#endif

// ---- SWELL_RunMessageLoop ----
// Defined in swell-wnd.cpp

// ---- SWELL_CreateXBridgeWindow ----

#ifndef SWELL_TARGET_OSX
HWND SWELL_CreateXBridgeWindow(HWND viewpar, void **wref, const RECT *r)
{
  return NULL;
}

void *SWELL_GetOSWindow(HWND hwnd, const char *type)
{
  return NULL;
}

void *SWELL_GetOSEvent(const char *type)
{
  return NULL;
}
#endif
