# SWELL2 Debugging Guide

## Testing with REAPER

REAPER loads `libSwell.so` at startup and resolves all SWELL functions via
`SWELLAPI_GetFunc`. The canonical way to test is a bubblewrap sandbox that
overlays your build output onto the system library path:

```bash
# Build with debug symbols
cmake -DCMAKE_BUILD_TYPE=Debug -B build-debug
cmake --build build-debug

# Kill any existing REAPER instance first (single-instance detection will
# cause a fresh run to immediately exit with "activating running instance")
ps aux | grep "REAPER/reaper" | grep -v grep | awk '{print $2}' | xargs -r kill -9

# Quick smoke test — capture exit code OUTSIDE the pipe
bwrap --bind / / \
    --dev /dev \
    --ro-bind /usr /usr \
    --ro-bind /lib /lib \
    --ro-bind /lib64 /lib64 \
    --proc /proc \
    --tmpfs /tmp \
    --setenv XDG_RUNTIME_DIR "$XDG_RUNTIME_DIR" \
    --bind "$XDG_RUNTIME_DIR" "$XDG_RUNTIME_DIR" \
    --bind "$PWD/build-debug/libSwell.so" /usr/lib/REAPER/libSwell.so \
    reaper
  -- \
  timeout --kill-after=2 5 /usr/lib/REAPER/reaper > /tmp/reaper_test.log 2>&1
echo "EXIT: $?"

# exit 137 = SIGKILL'd by timeout --kill-after (REAPER alive = success)
# exit 124 = SIGTERM'd by timeout and process exited cleanly (also success)
# exit 0   = REAPER self-exited (GUI failed, check stubs)
# exit 139 = SIGSEGV (function pointer was NULL or bad memory access)

# NOTE: Do NOT use `bwrap ... | grep ... | tail; echo "EXIT: $?"` — that
# prints tail's exit code (always 0), masking bwrap's real exit code.
# Always redirect to a file and check exit code before reading the file.

# Interactive debug with gdb
bwrap \
  --ro-bind / / \
  --bind "$PWD/build-debug/libSwell.so" /usr/lib/REAPER/libSwell.so \
  --bind "$HOME/.config/REAPER" "$HOME/.config/REAPER" \
  --bind "$HOME/.cache" "$HOME/.cache" \
  --tmpfs /tmp \
  --bind /tmp/.X11-unix /tmp/.X11-unix \
  --dev /dev \
  --proc /proc \
  --setenv DISPLAY ":0" \
  --setenv SDL_VIDEO_DRIVER x11 \
  --setenv GDK_BACKEND x11 \
  -- \
  gdb /usr/lib/REAPER/reaper
```

On Wayland systems, set `DISPLAY=:0` and `SDL_VIDEO_DRIVER=x11` to force SDL3
to use XWayland. The original SWELL GDK backend uses `GDK_BACKEND=x11`.
Both are needed when testing SDL3 backend under Wayland.

## Tracing Which Functions REAPER Calls

To see which functions are actually called (not just resolved), inject
fprintf traces into every function body:

```python
# inject_traces.py — adds fprintf(stderr, "SWELL_CALL: Func\n") after each {
# See AGENTS.md "Tracing function calls" section for the full script.
```

Then run and filter:
```bash
cmake --build build-debug
bwrap ... timeout 5 /usr/lib/REAPER/reaper 2>&1 | \
  grep "SWELL_CALL:" | sed 's/.*SWELL_CALL: //' | sort -u
```

This reveals exactly which stubs are hit and in what order REAPER calls them.

## REAPER Call Sequence (fully running)

With `swell-dlg.cpp` and `swell-controls.cpp` implemented, REAPER runs its full
message loop. The 100+ SWELL functions called during a normal session include:

**Init phase:**
```
SWELL_initargs → SWELL_RegisterCustomControlCreator → SWELL_Internal_PostMessage_Init
→ SWELL_Register_Cursor_Resource (×60+) → SWELL_ExtendedAPI → GetModuleFileName
→ lstrcpyn → GetPrivateProfileInt/String/Struct → SWELL_GenerateGUID
→ SWELL_EnableRightClickEmulate → AddFontResourceEx → LoadNamedImage
→ RegisterClipboardFormat → CreateEvent → GetCurrentThreadId → SetThreadPriority
→ GetAsyncKeyState → GetTempPath → LoadLibrary → SWELL_LoadMenu
```

**Dialog creation:**
```
SWELL_CreateDialog → SWELL_Make* (Button/EditField/Label/Control/Combo/CheckBox)
→ swell_oswindow_manage → swell_oswindow_resize → swell_oswindow_update_style
→ ShowWindow → SetMenu → DrawMenuBar → SetDlgItemText → GetDlgItem
```

**Message loop (per iteration):**
```
SWELL_RunMessageLoop → SWELL_MessageQueue_Flush → SWELL_RunEvents
→ GetTickCount → Sleep → SetTimer/KillTimer → InvalidateRect
→ BeginPaint → SWELL_internalSkiaPaint → BitBlt → EndPaint
→ swell_oswindow_updatetoscreen → swell_oswindow_invalidate
→ SWELL_GetCtxFrameBuffer → SendMessage → DefWindowProc
→ GetWindowRect/ClientRect → GetCursorPos → GetAsyncKeyState
```

**Pre-swell-dlg.cpp (historical):** `SWELL_CreateDialog` returned `NULL`,
REAPER's main window creation failed, REAPER exited with code 0.

## Known Stub Return-Value Issues

Some stubs return values that make REAPER behave unexpectedly:

| Function | Stub Returns | Should Return | Effect |
|---|---|---|---|
| `SWELL_DialogBox` | -1 | non-zero (success) | REAPER exits immediately |
| `SWELL_CreateDialog` | NULL | valid HWND | No main window |
| `SWELL_MakeButton` etc. | NULL | valid HWND | No controls in dialogs |
| `SWELL_CreateXBridgeWindow` | NULL | valid HWND | VST plugin windows fail |
| `EnumDisplayMonitors` | FALSE | TRUE + callback | REAPER thinks no display |
| `GetSystemMetrics` | 0 for all | plausible values | 0x0 screen → may exit |
| `GetMonitorInfo` | FALSE | TRUE | Monitor detection fails |

The display-related stubs (`GetSystemMetrics`, `EnumDisplayMonitors`) are now
fixed to return plausible values (1920x1080 screen, 1 monitor).

## Common Crash Patterns

### SIGSEGV at 0x0000000000000000

**Cause:** REAPER resolved a SWELL function pointer via `SWELLAPI_GetFunc`,
got NULL, and called it.

**Fix:** Every function declared with `SWELL_API_DEFINE` in `swell-functions.h`
must have a definition in libSwell.so. Add a stub to `swell-stubs.cpp`.

**Audit missing symbols:**
```bash
# Extract all declared function names
grep -oP 'SWELL_API_DEFINE\([^,]+,\s*\K\w+' swell-functions.h | sort > /tmp/declared.txt

# Extract all defined (exported) function names
nm -C --defined-only build-debug/libSwell.so | grep -oP ' T \K\w+' | sort > /tmp/defined.txt

# Show what's missing
comm -23 /tmp/declared.txt /tmp/defined.txt
```

Ignore entries `func` and `function_name` (from comments in swell-functions.h).
Functions inside `#ifdef SWELL_TARGET_OSX` will show as missing on Linux — this is expected.

### Undefined symbol at .so load time

Error like: `undefined symbol: _Z24SWELL_SetMenuDestination...`

**Cause:** A function is in the `api_table[]` (referenced by `swell-appstub.cpp`)
but not defined. `nm -D libSwell.so | grep U` shows unresolved symbols.

**Fix:** Check if the function is incorrectly inside an `#ifdef SWELL_TARGET_OSX`
block in `swell-stubs.cpp`. Some functions (e.g. `SWELL_SetMenuDestination`) are
declared unconditionally in `swell-functions.h` despite "macOS only" comments.
Trust the preprocessor scope, not the comment.

### Mangled C++ names vs extern "C"

SWELL functions on Linux have C++ linkage (no `extern "C"`) because
`SWELL_API_DEFINE` produces plain `ret func parms;` declarations. This means
function names are mangled. When debugging with `nm`, use `nm -C` to demangle.

## Build Issues

### swell-types.h `#if 0` guards

`swell-types.h:1434` wraps many `SM_*` and other constants in `#if 0 // these
are disabled until implemented`. When you implement a function that uses these
constants (e.g. `GetSystemMetrics`), change the guard to `#if 1`. The kept
headers rule means "don't change the API" — enabling already-declared constants
is expected when the implementation catches up.

### min/max macro conflicts

`swell-types.h` defines `min`/`max` as Win32-compat macros. These shadow
`std::min`/`std::max` and Skia's member functions.

- CMake defines `NOMINMAX` via `target_compile_definitions`
- In files that `#include "swell.h"` then STL/Skia headers: `#undef min` /
  `#undef max` after the swell include

### Skia API version mismatch

The installed Skia may be m118+ where `SkSurface::MakeRasterN32Premul` no longer
exists. Use `SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h))` instead.
Check `/usr/include/include/core/SkSurface.h` for available factory functions.

### WDL include paths

From `swell2/`, WDL headers are at `../` (not `../../` as the older docs say).
Verify with: `ls ../mutex.h ../ptrlist.h ../wdlstring.h ../heapbuf.h`

### WDL_FastString / WDL_TypedBuf headers

- `WDL_FastString` is actually `WDL_String` from `../wdlstring.h`
  (with `#define WDL_String WDL_FastString` at bottom)
- `WDL_TypedBuf` is in `../heapbuf.h`

## WDL_MutexLock Usage

`WDL_MutexLock` is RAII — constructor locks, destructor unlocks. There are no
`Enter()`/`Leave()` methods on the lock object. For manual lock/unlock, call
`mutex.Enter()` and `mutex.Leave()` directly on the `WDL_Mutex` object.

## swell.h Double-Include Trick

`swell.h` includes `swell-functions.h` **outside** the `_WDL_SWELL_H_` guard.
This allows `swell-appstub.cpp` to:
1. First include swell.h → normal function declarations
2. `#undef _WDL_SWELL_H_API_DEFINED_` + redefine `SWELL_API_DEFINE` as table entry
3. Re-include `swell-functions.h` → generates `{name, ptr}` entries for the lookup table

## REAPER Startup Flow

1. REAPER's ELF loader resolves `SWELLAPI_GetFunc` from libSwell.so
2. REAPER calls `SWELLAPI_GetFunc("function_name")` for each SWELL function it uses
3. REAPER stores returned pointers and calls through them
4. If any pointer is NULL → SIGSEGV at 0x0

## Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -B build-debug
cmake --build build-debug
# libSwell.so with -g, -O0, no stripping
```
