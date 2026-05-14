# SWELL2 Implementation Progress

## Complete (builds as libSwell.so)

| # | Module | Status | Notes |
|---|--------|--------|-------|
| 1 | `swell-internal.h` | done | All internal types: HWND__, HDC__, HGDIOBJ__, HMENU__, HTREEITEM__, control states, timer/PMQ, globals, internal fn decls. |
| 2 | `swell-gdi-internalpool.h` | done | Pool API (merged into swell-gdi.cpp). Caps: 100 HDC, 200 HGDIOBJ. |
| 3 | `swell-ini.cpp` | done | WritePrivateProfileString/Int/Struct/Section, GetPrivateProfileString/Int/Struct/Section. Thread-safe, flock, struct as hex+CRC32. |
| 4 | `swell-gdi.cpp` | done | HDC lifecycle, GDI objects, Skia drawing/blit, LoadNamedImage (SkCodec), fonts (FreeType+SkFontScanner), text metrics, clip regions. |
| 5 | `swell-wnd.cpp` | done | HWND__ lifecycle, SendMessage/DefWindowProc, PostMessage queue, timers, focus chain, window hierarchy, props, coordinates, message loop, WM_NCLBUTTONDOWN→menu bar click. |
| 6 | `swell-backend-headless.cpp` | done | All swell_oswindow_* no-ops. |
| 7 | `swell-backend-sdl3.cpp` | done | SDL3 OS backend: window mgmt, event translation, Skia screen update, keyboard/mouse, menu bar painting. |
| 8 | `swell-appstub.cpp` | done | SWELLAPI_GetFunc export with sorted function-pointer lookup table. |
| 9 | `swell-controls.cpp` | done | 10 built-in control WNDPROCs: button, edit, label, listview, treeview, combo, tab, trackbar, progress, SWELL_MakeControl dispatcher. |
| 10 | `swell-dlg.cpp` | done | SWELL_DialogBox/SWELL_CreateDialog/EndDialog, SWELL_Make* factories, UI scaling, modal window helpers. |
| 11 | `swell-menu.cpp` | done | HMENU lifecycle, item manipulation, TrackPopupMenu (SDL3 popup window), menu bar painting, menubar_hittest, menu generation from list. |
| 12 | `swell-misc.cpp` | partial | Clipboard (SDL3), MessageBox (SDL3), file dialogs (zenity), ShellExecute (xdg-open), threads/events (SetThreadPriority→nice, GlobalSize with prefix-header), monitors, cursors (system + from-file, SDL3), GUID, rect utils, SWELL_ExtendedAPI, ImageList (Add/Remove/ReplaceIcon with refcounting), SWELL_ChooseColor (zenity), SWELL_ChooseFont (fc-list+zenity). **Stubs remain:** drag-drop, ListView_SetGridColor/SelColors, SWELL_GetGestureInfo, GL/Metal. |
| 13 | `swell-kb.cpp` | done | SWELL_KeyToASCII (letters, digits, numpad, US punctuation), SWELL_EnableRightClickEmulate (no-op on Linux). |
| 14 | `swell-modstub.cpp` | done | SWELL_dllMain + DllMain alias for SWELL_PROVIDED_BY_APP mode. |
| 15 | `swell-stubs.cpp` | done | ListView/TreeView/TabCtrl SendMessage helpers. macOS-only stubs under `#ifdef SWELL_TARGET_OSX`. |

## Remaining (known stubs / not yet implemented)

| Module | Functions | Notes |
|--------|-----------|-------|
| `swell-misc.cpp` | `DragQueryPoint`, `DragFinish`, `DragQueryFile`, `SWELL_InitiateDragDrop*`, `SWELL_FinishDragDrop` | Drag-drop entirely stubbed. Needs SDL3 DnD event handling. Complex. |
| `swell-misc.cpp` | `SWELL_GetGestureInfo` | Stub (no gesture support on Linux desktop). |
| `swell-misc.cpp` | GL/Metal: `SWELL_SetViewGL`, `SWELL_GetViewGL`, `SWELL_SetGLContextToView` | Stubs (OpenGL-in-window not yet plumbed). |
| `swell-stubs.cpp` | `SWELL_GetListViewHeaderHeight` | Hardcoded 20px. Should measure font. |
| `swell-stubs.cpp` | `SWELL_SetListViewFastClickMask`, `ListView_SetGridColor`, `ListView_SetSelColors` | No-ops. |

### macOS-only (correctly omitted on Linux)
SWELL_CB_*, SWELL_TB_*, SWELL_PostQuitMessage, SWELL_FlushWindow, SWELL_TerminateProcess, SWELL_CreateProcessIO, SWELL_ReadWriteProcessIO, etc. — all under `#ifdef SWELL_TARGET_OSX`.

## Build

```bash
cd swell2 && cmake -DCMAKE_BUILD_TYPE=Debug -B build-debug && cmake --build build-debug
# → build-debug/libSwell.so
```

## Test with REAPER

```bash
# Kill existing (single-instance)
ps aux | grep "REAPER/reaper" | grep -v grep | awk '{print $2}' | xargs -r kill -9

# Run with timeout
bwrap --ro-bind / / \
  --bind "$PWD/build-debug/libSwell.so" /usr/lib/REAPER/libSwell.so \
  --bind "$HOME/.config/REAPER" "$HOME/.config/REAPER" \
  --bind "$HOME/.cache" "$HOME/.cache" \
  --tmpfs /tmp --bind /tmp/.X11-unix /tmp/.X11-unix \
  --dev /dev --proc /proc \
  --setenv DISPLAY ":0" --setenv SDL_VIDEO_DRIVER x11 --setenv GDK_BACKEND x11 \
  -- timeout --kill-after=2 5 /usr/lib/REAPER/reaper > /tmp/reaper.log 2>&1
echo "EXIT: $?"
# 137 = success (SIGKILL'd by timeout --kill-after)
