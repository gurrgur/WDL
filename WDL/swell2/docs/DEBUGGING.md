# SWELL2 Debugging Guide

## Testing with REAPER

REAPER loads `libSwell.so` at startup and resolves all SWELL functions via
`SWELLAPI_GetFunc`. The canonical way to test is a bubblewrap sandbox that
overlays your build output onto the system library path:

```bash
# Build with debug symbols
cmake -DCMAKE_BUILD_TYPE=Debug -B build-debug
cmake --build build-debug

# Run REAPER under gdb with your libSwell.so
GDK_BACKEND=x11 bwrap \
  --ro-bind / / \
  --bind "$PWD/build-debug/libSwell.so" /usr/lib/REAPER/libSwell.so \
  --bind "$HOME/.config/REAPER" "$HOME/.config/REAPER" \
  --bind "$HOME/.cache" "$HOME/.cache" \
  --tmpfs /tmp \
  --bind /tmp/.X11-unix /tmp/.X11-unix \
  --dev /dev \
  --proc /proc \
  --setenv GDK_BACKEND x11 \
  -- \
  gdb /usr/lib/REAPER/reaper
```

On Wayland systems, `GDK_BACKEND=x11` may be required for REAPER's GDK2
backend to connect to XWayland.

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
