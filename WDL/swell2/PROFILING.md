# PROFILING.md — Swell2 Lua Scroll Scene Optimization

## Quick start (copy-paste workflow)

```bash
# 1. Build swell2 with RelWithDebInfo (optimized + debug symbols)
cd swell2
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -G Ninja \
  -DSWELL2_BUILD_BENCHMARKS=OFF \
  -DSWELL2_FRAME_POINTERS=ON
cmake --build build

# 2. Run the profiling scene (from WDL repo root)
cd /home/marcus/Workspace/WDL/WDL
SCRIPT="$(pwd)/swell2/scripts/scroll_reaper.lua"
FRAMES="$(pwd)/swell2/build/frames.csv"

# 3a. Pure timing run (no perf overhead — best for frame_ms baseline)
#     SWELL_NO_VSYNC=1 disables vsync so frame_ms reflects true paint+
#     upload cost without SDL_RenderPresent stalls.
timeout -s KILL 14s bwrap \
  --bind / / --dev /dev --dev-bind-try /dev/dri /dev/dri \
  --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
  --proc /proc --tmpfs /tmp \
  --setenv XDG_RUNTIME_DIR "$XDG_RUNTIME_DIR" \
  --bind "$XDG_RUNTIME_DIR" "$XDG_RUNTIME_DIR" \
  --bind "$(pwd)/swell2/build/libSwell.so" /usr/lib/REAPER/libSwell.so \
  --setenv SWELL_PROF_SCRIPT "$SCRIPT" \
  --setenv SWELL_PROFILE_LOG "$FRAMES" \
  --setenv SCROLL_DURATION_SEC "12" \
  --setenv SWELL_NO_VSYNC "1" \
  reaper /home/marcus/Audio/REAPER/projects/testcase/testcase.RPP

# 3b. perf record run (for hotspot attribution — adds ~5% sampling overhead)
timeout -s KILL 15s bwrap \
  --bind / / --dev /dev --dev-bind-try /dev/dri /dev/dri \
  --ro-bind /usr /usr --ro-bind /lib /lib --ro-bind /lib64 /lib64 \
  --proc /proc --tmpfs /tmp \
  --setenv XDG_RUNTIME_DIR "$XDG_RUNTIME_DIR" \
  --bind "$XDG_RUNTIME_DIR" "$XDG_RUNTIME_DIR" \
  --bind "$(pwd)/swell2/build/libSwell.so" /usr/lib/REAPER/libSwell.so \
  --setenv SWELL_PROF_SCRIPT "$SCRIPT" \
  --setenv SWELL_PROFILE_LOG "$FRAMES" \
  --setenv SCROLL_DURATION_SEC "12" \
  --setenv SWELL_NO_VSYNC "1" \
  perf record -F 999 --call-graph dwarf,16384 \
  -o "$(pwd)/swell2/build/perf.data" -- \
  reaper /home/marcus/Audio/REAPER/projects/testcase/testcase.RPP

# 4. Aggregate frame counters
awk -F, 'NR>1 && NF>4 {
  n++; sum+=$4; ps+=$5; us+=$6; prs+=$7;
  if($4>max)max=$4
} END{
  printf "n=%d frame_avg=%.3f paint=%.3f upload=%.3f present=%.3f max=%.3f\n",
         n, sum/n, ps/n, us/n, prs/n, max
}' "$FRAMES"

# 5. Hotspot attribution
perf report -i swell2/build/perf.data --stdio --no-children | head -30
perf report -i swell2/build/perf.data --stdio --no-children --dso=libSwell.so | head -30
perf report -i swell2/build/perf.data --stdio --children --dso=libSwell.so | head -30

# 6. Inspect frame CSV (slowest first)
sort -t, -k4 -g -r "$FRAMES" | head -10
column -s, -t < "$FRAMES" | less -S
```

## Methodology notes

**Always run multiple iterations (3+).** Frame timings are noisy — single
runs can swing ±5%. Run baseline and post-change 3 times each, compare
medians:

```bash
for i in 1 2 3; do
  FRAMES="$(pwd)/swell2/build/frames_r$i.csv"
  rm -f "$FRAMES"
  timeout -s KILL 14s bwrap ... reaper ...
  awk -F, 'NR>1 && NF>4 {n++; sum+=$4} END{printf "r%s frame=%.3f\n", '$i', sum/n}' "$FRAMES"
done
```

**Stash for clean A/B compare.** Compare patched vs baseline without
contamination:

```bash
git stash && cmake --build build      # measure baseline
git stash pop && cmake --build build  # measure patched
```

**Disable vsync for paint optimization work.** With `SWELL_NO_VSYNC=1`,
`present_ms` drops from ~2 ms (with outliers up to 50 ms) to ~0.2 ms.
Frame outliers caused by missed vsync vanish, and `frame_ms` reflects
true paint+upload cost. Leave vsync on when measuring perceived smoothness.

**Watch the frame_ms distribution, not just the average.** A handful of
50 ms vsync stalls pulls the average up by ~1 ms while the typical
frame is unchanged. `sort -t, -k4 -g -r frames.csv | head` exposes outliers.

**Bucket by phase.** `paint_ms` >> `upload_ms` >> `present_ms` (typical
scroll scene). If a change moves `paint_ms`, it's a real GDI/Skia win.
If only `frame_ms` moves and the phases don't, it's likely vsync or
scheduler noise.

**Unsymbolized REAPER addresses dominate.** ~40 %+ of CPU is in REAPER's
own `wndproc` (no debug symbols in the shipped binary). These are
opaque — focus optimization on libSwell.so and Skia symbols. Use
`perf report --dso=libSwell.so` to filter to the surface we control.

**DWARF unwinding misses frames into stripped REAPER code.** Callgraph
attribution stops at the first un-symbolized REAPER frame; you'll see
hex addresses with no resolved callers. The flat profile and the
`--dso=libSwell.so` filter are more useful than the call tree for
diff-based optimization.

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
clamped texture upload area. `full_upload=1` when the dirty region covered
the whole surface (partial-upload optimization didn't trigger).

## Environment toggles

| Env var                  | Effect                                                |
|--------------------------|-------------------------------------------------------|
| `SWELL_PROF_SCRIPT`      | Lua script run on startup (e.g. scroll_reaper.lua)    |
| `SWELL_PROFILE_LOG`      | Path to per-frame CSV log                             |
| `SWELL_GDI_PROFILE_LOG`  | Path to per-GDI-function CSV (calls, total_ns, etc.)  |
| `SCROLL_DURATION_SEC`    | How long the Lua scroll loop runs before quitting     |
| `SWELL_NO_VSYNC`         | `1` disables SDL renderer vsync (clean paint timing)  |

## GDI API attribution

`SWELL_GDI_PROFILE_LOG=/path` activates per-call instrumentation on the hot
GDI entry points (`BitBlt`, `StretchBlt`, `SWELL_FillRect`, `SWELL_DrawText`,
`Rectangle`, `LineTo`, `SelectObject`, `GetDC`/`ReleaseDC`, etc.). Each
function increments a thread-safe counter + accumulates wall nanoseconds.
The CSV is dumped on `swell.exit()` or normal process exit:

```text
name,calls,total_ns,avg_ns,total_ms
BitBlt,14230,644542061,45294.6,644.542
SWELL_DrawText,934,24299831,26016.9,24.300
...
```

Use when you suspect a particular GDI primitive is the bottleneck, or to
prove how much paint time is spent inside Swell vs inside the host's own
wndproc internals. **Caveat:** `timeout -s KILL` skips the dump; use a
plain `timeout` (default SIGTERM) and ensure the Lua script calls
`swell.exit()` at the end of the scroll.

For the scroll_reaper scene with REAPER, total tracked GDI time is ~780 ms
over 10 s (~1.2 ms/frame) vs paint total of ~6900 ms (~10.3 ms/frame) —
**~89 % of paint is REAPER wndproc internal work that does not call any
Swell GDI primitive.** BitBlt dominates the GDI fraction at ~83 %; the
remaining tracked GDI (DrawText, FillRect, ReleaseDC, GetTextMetrics) is
collectively under 0.2 ms/frame. The implication: further wins on this
scene require either changing how REAPER draws or moving the Skia surface
to GPU.

## Scene description

`scripts/scroll_reaper.lua` waits for a `REAPERTrackListWindow`, then sends
continuous sinusoidal `WM_MOUSEWHEEL` messages via `PostMessage`. REAPER
handles these by adjusting timeline zoom and repainting the track area.
The profile captures the full loop:

```
Lua tick → PostMessage → Flush → SendMessage → REAPER wndproc
  → invalidation → Skia paint → SDL texture upload → present
```

The dirty region during scroll typically covers most of the track window
(~3 M pixels per frame). Wins come from making Skia raster ops cheaper
(BitBlt fast path, sprite blitter), not from reducing paint scope.

## Known hotspots (post Pass 1)

- `__memmove_avx512_unaligned_erms` (~14 % flat): split between
  `BitBlt` writePixels memcpy, `SDL_UpdateTexture` driver copy, and
  REAPER-internal buffer copies.
- REAPER `wndproc` internals (40 %+ unsymbolized): track layout, MIDI
  rendering, item drawing. Out of scope for swell2.
- Skia raster pipeline (`sse2::lowp::*`, `rect_memcpy`, `rect_memset32`):
  the cost of every fill / blit / text run REAPER issues.

## Past optimization passes

1. **Sub-rect snapshots → full-surface snapshots in BitBlt/StretchBlt**
   (`perf(swell2): avoid subset blit snapshots`). Full snapshots are COW
   on raster surfaces; subset snapshots forced a pixel copy. Net: small.

2. **writePixels fast path + kFast_SrcRectConstraint in BitBlt/StretchBlt.**
   For SRCCOPY 1:1 raster→raster with an integer-translate canvas matrix
   and the dst rect inside the clip, bypass `drawImageRect` entirely and
   call `SkSurface::writePixels` (per-row memcpy). Frame avg dropped from
   ~18.0 ms to ~17.4 ms (vsync on) and from ~13.0 ms to ~12.5 ms (vsync
   off). Removes `sse2::lowp::gather_8888` + `matrix_translate` from
   the profile.
