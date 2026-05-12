# SWELL2 Implementation Progress

## Complete (builds as libSwell.so)

| # | Module | Status | Notes |
|---|--------|--------|-------|
| 1 | `swell-internal.h` | done | All internal types: HWND__, HDC__, HGDIOBJ__, HMENU__, HTREEITEM__, control states, timer/PMQ, globals, internal fn decls. Skia types real, `NOMINMAX` guard. |
| 2 | `swell-gdi-internalpool.h` | done | Pool API: SWELL_GDP_CTX_NEW/DELETE, GDP_OBJECT_NEW/DELETE, HGDIOBJ_VALID, HDC_VALID. Caps: 100 HDC, 200 HGDIOBJ. |
| 3 | `swell-ini.cpp` | done | WritePrivateProfileString/Int/Struct/Section, GetPrivateProfileString/Int/Struct/Section. Thread-safe (WDL_Mutex), inter-process (flock), struct as hex+CRC32. |
| 4 | `swell-gdi.cpp` | done | HDC lifecycle (CreateMemContext/BeginPaint/EndPaint/GetDC/ReleaseDC), GDI object create/delete (CreatePen/Font/Bitmap/SelectObject/DeleteObject/GetStockObject, SelectObject sentinel pattern per RENDERING.md §3), drawing/blit stubs (no-ops), text metrics (fallback vals), color conversion (native↔SkColor), clip stubs, pool impl. |
| 5 | `swell-wnd.cpp` | done | HWND__ ctor/dtor, SendMessage, DefWindowProc, SwellDialogDefaultWindowProc, PostMessage queue (thread-safe, max 1024), timers (SetTimer/KillTimer/fireTimers), focus chain (SetFocus/GetFocus), DestroyWindow protocol (WM_DESTROY→NCDESTROY), ShowWindow/EnableWindow/IsWindow*, window hierarchy (GetParent/SetParent/GetWindow/IsChild/EnumWindows/EnumChildWindows/FindWindowEx/GetDlgItem), GetWindowLong/SetWindowLong, Prop list, coordinate conv (ClientToScreen/ScreenToClient/GetClientRect/GetWindowRect/SetWindowPos/WindowFromPoint), InvalidateRect/UpdateWindow, ScrollWindow, GetClassName/SWELL_SetClassName, SWELL_BroadcastMessage, custom control creator registration, SWELL_GetDefaultButtonID, helper functions (SWELL_DrawFocusRect/IsGroupBox/IsButton/IsStaticText), MulDiv/lstrcpyn, Sleep/GetTickCount/GetFileTime, SWELL_RunMessageLoop (flush→events→timers). |
| 6 | `swell-backend-headless.cpp` | done | All swell_oswindow_* no-ops, SWELL_initargs stub, SWELL_RunEvents stub, SWELL_CreateXBridgeWindow/SWELL_GetOSWindow/SWELL_GetOSEvent stubs. |
| 7 | `CMakeLists.txt` | done | C++17, links Skia (pkg-config), pthread, dl. Defines NOMINMAX. Builds libSwell.so (5.9MB). |

## Remaining (not started)

| # | Module | Notes |
|---|--------|-------|
| 8 | `swell-controls.cpp` | 10 built-in control WNDPROCs (button, edit, label, listview, treeview, combo, tab, trackbar, progress, SWELL_MakeControl dispatcher) |
| 9 | `swell-dlg.cpp` | Dialog creation (SWELL_DialogBox/SWELL_CreateDialog/EndDialog), SWELL_Make* control factories, dialog coordinate scaling, modal window helpers |
| 10 | `swell-menu.cpp` | HMENU lifecycle, item manipulation, TrackPopupMenu, menu bar painting |
| 11 | `swell-misc.cpp` | Clipboard, drag-drop, monitors, MessageBox, file dialogs, ShellExecute, threads, GUID, rect utils, cursors, ImageList, ListView/TreeView/Tab helpers, GL/Metal stubs |
| 12 | `swell-kb.cpp` | SWELL_KeyToASCII, accelerator processing, right-click emulation |
| 13 | `swell-modstub.cpp` | DllMain shim for SWELL_PROVIDED_BY_APP mode |
| 14 | `swell-appstub.cpp` | Standalone app entry (SWELLAppMain dispatch) |
| 15 | Rendering/windowing backend | GDK/SDL3 OS backend (swell-backend-gdk.cpp) — explicitly deferred |

## Build

```bash
cd swell2 && cmake -B build && cmake --build build
# → build/libSwell.so
```
