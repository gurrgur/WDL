# PROFILING.md — Swell2 Lua Scroll Scene Optimization

## Quick start (copy-paste workflow)

```bash
# 1. Build swell2 with RelWithDebInfo (optimized + debug symbols)
cd swell2/build
cmake -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSWELL2_BUILD_BENCHMARKS=OFF -G Ninja
cmake --build . -j$(nproc)

# 2. Run the profiling scene (from WDL repo root)
cd /home/marcus/Workspace/WDL/WDL
SCRIPT="$(pwd)/swell2/scripts/scroll_reaper.lua"
FRAMES="$(pwd)/swell2/build/frames_baseline.csv"

# 3. Profile with perf (always use timeout -s KILL!)
  timeout -s KILL 30s bwrap \
    --bind / / --dev /dev --dev-bind-try /dev/dri /dev/dri \
    --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
    --proc /proc --tmpfs /tmp \
    --setenv XDG_RUNTIME_DIR "$XDG_RUNTIME_DIR" \
    --bind "$XDG_RUNTIME_DIR" "$XDG_RUNTIME_DIR" \
    --bind "$(pwd)/swell2/build/libSwell.so" /usr/lib/REAPER/libSwell.so \
    --setenv SWELL_INSPECTOR "1" \
    --setenv SWELL_PROF_SCRIPT "$SCRIPT" \
    --setenv SWELL_PROFILE_LOG "$FRAMES" \
    --setenv SCROLL_DURATION_SEC "15" \
    perf record -F 999 -g -o "$(pwd)/swell2/build/perf_baseline.data" -- \
    reaper /home/marcus/Audio/REAPER/projects/testcase/testcase.RPP

# 4. Analyze hotspots
perf report -i swell2/build/perf_baseline.data --stdio --children

# 5. Analyze frame counters
column -s, -t < "$FRAMES" | less -S

# 6. Optimize
```

## Frame counter CSV

Set `SWELL_PROFILE_LOG=/path/to/frames.csv` to record one CSV row per painted
top-level frame:

```text
frame,hwnd,class,frame_ms,paint_ms,upload_ms,present_ms,
invalidates,paint_windows,dirty_x,dirty_y,dirty_w,dirty_h,
surface_w,surface_h,upload_pixels,full_upload
```

`frame_ms` is paint + upload + present for that SWELL frame. `paint_ms` wraps
`SWELL_internalSkiaPaint()`. `upload_ms` wraps `SDL_UpdateTexture()`.
`present_ms` wraps `SDL_RenderTexture()` + `SDL_RenderPresent()`.
`invalidates` counts invalidations batched into that frame. `paint_windows`
counts HWNDs repainted by recursive SWELL paint. `upload_pixels` is the
clamped texture upload area.

## Scene description

`scripts/scroll_reaper.lua` waits for a `REAPERTrackListWindow`, then sends
continuous sinusoidal `WM_MOUSEWHEEL` messages via `PostMessage`. REAPER
handles these by adjusting timeline zoom and repainting the track area.
The profile captures the full loop:

```
Lua tick → PostMessage → Flush → SendMessage → REAPER wndproc
  → invalidation → Skia paint → SDL texture upload → present
```
