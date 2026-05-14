# AGENTS.md — SWELL2

SWELL2 is a **clean-room C++17 reimplementation** of SWELL (Simple/Small
Win32 Emulation Layer for Linux/OSX) using **Skia** for all GDI rendering.

## Purpose

SWELL provides a Win32-API-compatible layer so that Windows GUI code
(RegisterClass, CreateWindow, SendMessage, GDI drawing, dialogs, controls,
menus, etc.) compiles and runs on **Linux** without modification.

## Architecture overview

```
swell.h                       Public entry point; includes types + functions
├── swell-types.h             Win32 type defs, constants, structs (1525 lines)
├── swell-functions.h         All public API via SWELL_API_DEFINE macro (1101 lines)
└── swell-win32.h             Helpers for actual Win32 builds (ctx menu disable)

swell-internal.h              Internal declarations (HWND__, HDC__, HMENU__, control
                                states, timer/PMQ records, color theme, helper funcs)

swell-gdi-internalpool.h      HDC__ and HGDIOBJ__ pool management (internal)
swell-dlggen.h                Dynamic control creation from .rc resource scripts
swell-menugen.h               Dynamic menu generation from .rc resource scripts
swellappmain.h                Legacy ObjC app stubs (not used by swell2)

Core modules:
  swell-gdi.cpp               GDI: HDC/object lifecycle, drawing, text, images (1704 lines)
  swell-wnd.cpp               Window mgmt: HWND__, SendMessage, PostMessage, timers (1714 lines)
  swell-dlg.cpp               Dialog creation + SWELL_GenerateDialogFromList (634 lines)
  swell-controls.cpp          Control WNDPROCs: button, edit, listview, treeview,
                                combo, tab, trackbar, progress (2560 lines)
  swell-menu.cpp              HMENU__, TrackPopupMenu, menu bar, resource loading (1126 lines)
  swell-misc.cpp              Clipboard, drag-drop, MessageBox, file dialogs,
                                threads, cursors, ImageList, GUID, ShellExecute (1298 lines)
  swell-ini.cpp               INI file read/write: PrivateProfileString/Int (506 lines)
  swell-kb.cpp                Keyboard: SWELL_KeyToASCII, accelerators (126 lines)
  swell-stubs.cpp             ListView/TreeView SendMessage helpers, misc stubs (666 lines)

Backends (only one linked per build):
  swell-backend-sdl3.cpp      SDL3 OS backend: real windows, event translation (1011 lines)
  swell-backend-headless.cpp  Headless: all oswindow calls are no-ops (98 lines)

Support:
  swell-appstub.cpp           SWELLAPI_GetFunc for host apps (function-ptr table) (49 lines)
  swell-modstub.cpp           DllMain shim for SWELL_PROVIDED_BY_APP / plugin mode (75 lines)

Resource generation:
  swell_resgen.pl / .php / .sh  Convert Win32 .rc files -> swell-dlggen.h dialogs
```

## Key design decisions

1. **Skia is the only GDI renderer.** No LICE, no Cairo, no platform-native
   drawing. All drawing goes through `SkCanvas` + `SkSurface`.

2. **Two backends, selected at build time via CMake:**
   - SDL3 (`-DSWELL_TARGET_SDL3`) — real OS windows, event loop, hardware surfaces
   - Headless — all `swell_oswindow_*` functions are empty stubs (for headless rendering/screenshots)

3. **Two linking modes via macro toggle:**
   - **App mode (default):** `SWELL_API_DEFINE(ret, func, parms)` — normal function definitions. The app links directly against the swell2 library. `swell-appstub.cpp` provides a `SWELLAPI_GetFunc()` name→function-pointer table.
   - **Plugin mode (`-DSWELL_PROVIDED_BY_APP`):** Every API function becomes an extern function pointer (`extern ret (*func)parms;`). `swell-modstub.cpp` provides `SWELL_dllMain()` which resolves all pointers from the host's `getfunc`. Only the plugin compiles `swell-modstub.cpp`; the host links the full library.

4. **Opaque handle struct system.** All Win32 handles (`HWND__`, `HDC__`,
   `HMENU__`, `HGDIOBJ__`, `HTREEITEM__`, `HIMAGELIST__`) are structs
   declared in `swell-internal.h`. Casts between `HWND` (which is `HWND__*`)
   and the struct are used throughout. Control state is stored as
   `SetWindowLong(hwnd, GWL_USERDATA, ...)` pointing to per-control data structs.

5. **DPI scaling.** `g_swell_ui_scale` (256 = 1.0x, 512 = 2.0x). Physical↔logical
   pixel conversion functions in `swell-internal.h`. SDL3 reports in logical
   pixels; SWELL operates in physical pixels.

6. **Color theme.** `g_swell_ctheme` (struct `swell_colortheme`) holds the
   current system color scheme. All drawing consults these colors. No system
   theme APIs are called — this is self-contained.

7. **Cockos WDL dependency.** swell2 depends on sibling WDL utilities in `../`:
   - `mutex.h` — `WDL_Mutex` (pthread mutex wrapper)
   - `ptrlist.h` — `WDL_PtrList`, `WDL_PtrList_DeleteOnDestroy`
   - `heapbuf.h` — `WDL_TypedBuf`
   - `assocarray.h` — `WDL_StringKeyedArray`
   - `wdlstring.h` — `WDL_FastString` (aliased to `WDL_String`)
   - `wdlcstring.h` — `lstrcpyn_safe()`

## Build system

- **CMake** (C++17, `CMAKE_POSITION_INDEPENDENT_CODE ON`)
- **Required:** Skia via pkg-config (`pkg_check_modules(SKIA REQUIRED)`)
- **Optional:** SDL3 via pkg-config (`pkg_check_modules(SDL3)`)
- **Required libs:** pthread, dl, fontconfig
- **Output:** shared library `libSwell.so`

```
cd build && cmake .. && make -j$(nproc)          # SDL3 backend (if found)
cd build-headless && cmake .. && make -j$(nproc)  # Headless backend
```

The CMakeLists.txt conditionally adds `swell-backend-sdl3.cpp` or
`swell-backend-headless.cpp` based on whether SDL3 pkg-config succeeds.

## File-module rules

- **To add new Win32 types/defines:** edit `swell-types.h`
- **To add new API functions:** add to `swell-functions.h` using the
  `SWELL_API_DEFINE(ret, name, (params))` convention
- **Implementation goes in the appropriate .cpp module** (e.g. GDI
  functions in `swell-gdi.cpp`, window functions in `swell-wnd.cpp`,
  misc in `swell-misc.cpp`)
- **Internal helper declarations** go in `swell-internal.h`

## Notable patterns

- `#ifndef NOMINMAX` / `#define NOMINMAX` at top of every .cpp (prevents
  `<windows.h>`-style min/max macro conflicts with C++ standard library).
- `swell-functions.h` includes a macro named `Polygon` which clashes with
  `SkPath::Polygon` — files that use SkPath must `#undef Polygon` after
  including `swell-internal.h`.
- `swell-types.h` defines `min`/`max` as Win32 compat macros. Files that
  need `<algorithm>` or `<map>` must `#undef min` / `#undef max` first.
- Timer and PostMessage queues use `WDL_Mutex` for thread safety.
- GDI object pool (HDC__, HGDIOBJ__) uses mutex-protected free-lists
  with `_infreelist` sentinel flags on freed objects.
- `SWELL_AutoReleaseHelper` is a no-op outside Apple targets.
- Some ObjC files (`swellappmain.h`) are legacy and do not apply to swell2.

## Relationship to original SWELL

The original `swell/` directory contains the mature, battle-tested
implementation with macOS/Cocoa (.mm files) and multiple GDI backends
(LICE, Skia, GDK). `swell2/` is the **clean-room reimplementation** that:

- Drops all macOS/Cocoa/ObjC code
- Drops the LICE and GDK GDI backends
- Consolidates all rendering through Skia
- Uses CMake instead of a hand-written Makefile
- Supports only Linux (SDL3 or headless)
- Produces a shared library instead of a static one
- Has a simpler, more modern codebase at ~15k lines total
