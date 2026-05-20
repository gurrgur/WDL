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
    --setenv SCROLL_DURATION_SEC "25" \
    perf record -F 999 -g -o /tmp/perf_baseline.data -- \
    reaper /home/marcus/Audio/REAPER/projects/testcase/testcase.RPP

# 4. Analyze hotspots
# 5. Optimize
```

## Scene description

`scripts/scroll_reaper.lua` waits for a `REAPERTrackListWindow`, then sends
continuous sinusoidal `WM_MOUSEWHEEL` messages via `PostMessage`. REAPER
handles these by adjusting timeline zoom and repainting the track area.
The profile captures the full loop:

```
Lua tick → PostMessage → Flush → SendMessage → REAPER wndproc
  → invalidation → Skia paint → SDL texture upload → present
```
