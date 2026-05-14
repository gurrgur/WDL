# SWELL2 — Agent Primer

SWELL2 is a clean-room reimplementation of Cockos SWELL (Simple/Small Win32
Emulation Layer). The goal is a correct, readable, maintainable codebase that
passes the same behavioral contracts as the original, built from scratch using
modern C++17 practices.

This file is the entry point for any agent or developer starting work here.
Read it before touching any code.

---

## What This Project Is

SWELL is a Win32 subset emulation layer for Linux and macOS. It lets Win32
GUI applications (plugins, DAW UIs, etc.) run unmodified on non-Windows
platforms by providing Win32 API functions (`CreateWindow`, `SendMessage`,
`CreateFont`, `TrackPopupMenu`, etc.) as thin wrappers over platform APIs.

This directory is the reimplementation. The original SWELL lives in
`../swell/`. Do not read implementation code from there — use the specs in
`docs/` instead. Reading the original for reference is acceptable when a spec
is incomplete, but the new code must be independently written.

---

## Workflow

- **Commit after finishing a piece of work.** Don't accumulate unrelated changes in the working tree. Each logical unit of work gets its own commit with a descriptive message.
- **When you notice docs are outdated, update them.** Specs in `docs/` are authoritative; if implementation reveals a gap or error in the spec, fix the spec in the same commit (or a preceding one). This file (`AGENTS.md`) tracks session findings — add to it as you discover things.

---

## Directory Layout

```
swell2/
  AGENTS.md                ← this file
  PROGRESS.md              ← implementation progress tracker
  docs/
    SPEC.md                ← complete API surface (types, constants, functions)
    PROTOCOLS.md           ← message encodings, call contracts, ordering guarantees
    RENDERING.md           ← GDI model, HDC state, Skia/paint pipeline, fonts
    EVENT-LOOP.md          ← message loop, OS backend, WM_PAINT synthesis
    CONTROLS.md            ← built-in control state and behavior
    DEBUGGING.md           ← common crash patterns, symbol auditing, REAPER testing

  # --- Kept headers (do not rewrite) ---
  swell.h                  ← main include; platform detection; top-level macros
  swell-types.h            ← all public types, structs, constants (Win32 subset)
  swell-functions.h        ← all ~200 API function declarations via SWELL_API_DEFINE
  swell-dlggen.h           ← dialog resource registration macros
  swell-menugen.h          ← menu resource registration macros
  swell-win32.h            ← Win32 passthrough helpers (not used on Linux)
  swellappmain.h           ← macOS NSApplication/NSAppController interface (macOS only)
  gtkimcontextsimpleseqs.h ← GTK IM context key sequence table (GDK backend data)
  swell_resgen.{pl,php,sh} ← resource compiler scripts (.rc → .rc_mac_dlg etc.)

  # --- To be written ---
  swell-internal.h         ← internal types: HWND__, HDC__, HGDIOBJ__, etc. (done)
  swell-gdi-internalpool.h ← GDI object pool (HDC/HGDIOBJ free lists) (done — merged into swell-gdi.cpp)
  swell-ini.cpp            ← INI file read/write (done)
  swell-gdi.cpp            ← GDI: drawing, fonts, bitmaps, text; Skia rendering (done)
  swell-wnd.cpp            ← window management: HWND lifecycle, messages, timers (done)
  swell-stubs.cpp          ← stubs for all ~240 SWELL_API_DEFINE functions (done)
  swell-appstub.cpp        ← SWELLAPI_GetFunc export (done)
  swell-backend-headless.cpp ← headless backend (done)
  swell-backend-sdl3.cpp   ← SDL3 OS backend; window mgmt, event translation, Skia screen update (done)
  swell-dlg.cpp            ← dialog creation and modal loop (done)
  swell-controls.cpp       ← built-in control WNDPROCs (done)
  swell-menu.cpp           ← HMENU, TrackPopupMenu, menu bar — NOT YET STARTED
  swell-misc.cpp           ← clipboard, drag-drop, monitors, MessageBox, file dialogs — NOT YET STARTED
  swell-kb.cpp             ← keyboard routing, accelerator handling — NOT YET STARTED
  swell-modstub.cpp        ← DllMain shim for plugin mode — NOT YET STARTED
  CMakeLists.txt           ← build system (done)
```

---

## Normative References

Read these before implementing anything in their domain. They are authoritative.

| Doc | Covers |
|---|---|
| `docs/SPEC.md` | Every type, constant, macro, and function signature. Deviations from Win32 are listed in §31. |
| `docs/PROTOCOLS.md` | Exact wParam/lParam encoding for 40+ messages. WNDPROC/DLGPROC contracts. Dialog/focus/timer/menu/PostMessage protocols. |
| `docs/RENDERING.md` | HDC state machine. HGDIOBJ types. SelectObject semantics. Skia surface model. Paint pipeline. FreeType font system. |
| `docs/EVENT-LOOP.md` | SWELL_RunMessageLoop steps. GDK event translation. WM_PAINT synthesis. OS window lifecycle. Window creation without dialog template. |
| `docs/CONTROLS.md` | Internal state structs and behavior for Button, Edit, Static, ListBox, ListView, TreeView, ComboBox, TabControl, Trackbar, ProgressBar. |

When a spec and the original source conflict, the spec wins. If the spec is
silent on something important, consult the original source and update the spec
before writing code.

---

## Kept Headers — What They Are

### `swell-types.h`
All public types. **Do not modify.** This is the ABI contract: `HWND__`,
`HMENU__`, `HDC__`, `HGDIOBJ__` are forward-declared here as opaque structs;
they are completed in `swell-internal.h`. All Win32 integer types, callback
types, struct types (RECT, MSG, LVITEM, etc.), and constants (WM_*, VK_*, etc.)
are defined here.

### `swell-functions.h`
All ~200 API functions declared via `SWELL_API_DEFINE(ret, name, parms)`. When
`SWELL_PROVIDED_BY_APP` is defined, these become `extern` function pointers
(plugin mode). Otherwise they are direct function declarations. **Do not
modify.**

### `swell.h`
Main include. Pulls in `swell-types.h` and `swell-functions.h`. Defines
`SWELL_POSTMESSAGE_DELEGATE_IMPL` (macOS), `SWELL_CB_*` / `SWELL_TB_*`
convenience macros, and `SWELL_AutoReleaseHelper`. **Minimize modifications.**

### `swell-dlggen.h`
Macros that let `.rc`-derived files register dialog resources at static-init
time. Defines `SWELL_DialogResourceIndex`, `SWELL_DlgResourceEntry`,
`SWELL_DialogRegHelper`, `SWELL_DEFINE_DIALOG_RESOURCE_BEGIN/END`, and
`SWELL_DLG_WS_*` flags. **Do not modify.**

### `swell-menugen.h`
Same pattern for menus: `SWELL_MenuResourceIndex`, `SWELL_MenuGenHelper`,
`SWELL_DEFINE_MENU_RESOURCE_BEGIN/END`. **Do not modify.**

---

## To-Write Headers

### `swell-internal.h`
Defines the implementation side of all opaque types. Only included by swell2
implementation files, never by app code.

Must define:
```
HWND__          window node: parent/children/next/prev/owner/owned linked lists,
                m_title, m_position, m_style, m_exstyle, m_id, m_wndproc,
                m_dlgproc, m_classname, m_font, m_private_data, m_invalidated,
                m_child_invalidated, m_visible, m_enabled, m_wantfocus,
                m_focused_child, m_menu, m_paintctx, m_hashaddestroy,
                m_backingstore, m_oswindow, m_userdata, m_extra[64]

HMENU__         menu: WDL_PtrList of SWELL_MenuItem

HGDIOBJ__       GDI object: type (TYPE_PEN/BRUSH/FONT/BITMAP), color (SkColor),
                wid, alpha, typedata, additional_refcnt, _infreelist, _next

HDC__           device context: canvas (SkCanvas*), surface (sk_sp<SkSurface>), surface_offs,
                dirty_rect, curpen, curbrush, curfont, cur_text_color_int,
                curbkcol, curbkmode, lastpos_x/y, _infreelist, _next

HTREEITEM__     tree node: m_value, m_param, m_state, m_haschildren,
                m_image/m_selimage, m_children[]

SWELL_OSWINDOW  typedef GdkWindow* (GDK backend) or void* (headless)

swell_gdpLocalContext   paint context passed as WM_PAINT wParam: ctx (HDC__),
                        clipr (RECT)

Control state types: buttonWindowState, __SWELL_editControlState,
    listViewState, SWELL_ListView_Row, SWELL_ListView_Col,
    treeViewState, __SWELL_ComboBoxInternalState, tabControlState

Timer types: TimerInfoRec (hwnd, timerid, interval, lastFire, tProc, refcnt, _next)

PostMessage queue types: PMQ_rec (hwnd, msg, wParam, lParam, _next)

Internal function declarations (not in swell-functions.h):
    SWELL_internalSkiaPaint, swell_oswindow_*, DefWindowProc internals, etc.
```

See `docs/RENDERING.md §1-2` and `docs/EVENT-LOOP.md §3` for field details.

### `swell-gdi-internalpool.h`
Free-list pools for HDC__ and HGDIOBJ__ to avoid per-draw allocation.

```c
HDC__*     SWELL_GDP_CTX_NEW();
void       SWELL_GDP_CTX_DELETE(HDC__*);
HGDIOBJ__* GDP_OBJECT_NEW();
void       GDP_OBJECT_DELETE(HGDIOBJ__*);
bool       HGDIOBJ_VALID(HGDIOBJ__* p, int reqType = 0);
bool       HDC_VALID(HDC__* ct);
```

Caps: 100 HDC, 200 HGDIOBJ. Mutex-protected. See `docs/RENDERING.md §9`.

---

## Module Responsibilities

### `swell-ini.cpp`

`GetPrivateProfileString/Int/Struct`, `WritePrivateProfileString/Struct/Section`,
`GetPrivateProfileSection`.

Rules: absolute path only; empty string → `~/.libSwell.ini`; thread-safe and
inter-process-safe via file locking; `GetPrivateProfileStruct` stores binary as
hex + CRC checksum. See `docs/SPEC.md §5.3`.

### `swell-gdi.cpp`

Everything GDI. Sections:
- HDC lifecycle: `SWELL_CreateMemContext`, `SWELL_DeleteGfxContext`, `BeginPaint`,
  `EndPaint`, `GetDC`, `GetWindowDC`, `ReleaseDC`
- GDI objects: `CreatePen`, `CreatePenAlpha`, `CreateSolidBrush`,
  `CreateSolidBrushAlpha`, `CreateFont`, `CreateFontIndirect`, `CreateBitmap`,
  `CreateIconIndirect`, `LoadNamedImage`, `SelectObject`, `DeleteObject`,
  `GetStockObject`, `SWELL_CloneGDIObject`, `GetObject`
- Drawing: `Rectangle`, `Ellipse`, `RoundRect`, `SWELL_FillRect`, `SWELL_Polygon`,
  `MoveToEx`, `LineTo` (=`SWELL_LineTo`), `SetPixel`, `PolyBezierTo`, `PolyPolyline`
- Blit: `BitBlt`, `StretchBlt`, `StretchBltFromMem`, `DrawImageInRect`
- State: `SetTextColor`, `GetTextColor`, `SetBkColor`, `SetBkMode`
- Text: `SWELL_DrawText` (=`DrawText`), `GetTextMetrics`, `GetTextFace`,
  `GetGlyphIndicesW`
- Colors: `GetSysColor`
- Clip: `SWELL_PushClipRegion`, `SWELL_SetClipRegion`, `SWELL_PopClipRegion`
- Context info: `SWELL_GetCtxGC`, `SWELL_GetCtxFrameBuffer`
- Font: `AddFontResourceEx`, `SWELL_GetDefaultFont`
- Paint pipeline: `SWELL_internalSkiaPaint`

See `docs/RENDERING.md` for full behavioral spec.

### `swell-wnd.cpp`

Window management. Sections:
- `HWND__` constructor/destructor (sends WM_NCDESTROY from destructor)
- `DestroyWindow`, `RecurseDestroyWindow`
- `SendMessage`, `DefWindowProc`, `SwellDialogDefaultWindowProc`
- `PostMessage`, `SWELL_MessageQueue_Flush`, `SWELL_MessageQueue_Clear`,
  `SWELL_Internal_PostMessage*`
- `SetTimer`, `KillTimer`
- `SWELL_RunMessageLoop`
- `ShowWindow`, `EnableWindow`, `IsWindowEnabled`, `IsWindowVisible`, `IsWindow`
- `SetFocus`, `GetFocus`, `SetForegroundWindow`, `GetForegroundWindow`
- `GetClientRect`, `GetWindowRect`, `SetWindowPos`, `GetWindowContentViewRect`
- `ClientToScreen`, `ScreenToClient`, `WindowFromPoint`
- `GetWindowLong`, `SetWindowLong`, `ScrollWindow`, `InvalidateRect`, `UpdateWindow`
- `GetParent`, `SetParent`, `GetWindow`, `IsChild`, `EnumWindows`,
  `EnumChildWindows`, `FindWindowEx`, `GetDlgItem`
- `GetProp`, `SetProp`, `RemoveProp`, `EnumPropsEx`
- `GetClassName`, `SWELL_SetClassName`
- `SetCapture`, `GetCapture`, `ReleaseCapture`
- `SWELL_BroadcastMessage`
- `SetDlgItemText`, `GetDlgItemText`, `SetDlgItemInt`, `GetDlgItemInt`,
  `GetWindowTextLength`, `CheckDlgButton`, `IsDlgButtonChecked`
- `SWELL_RegisterCustomControlCreator`, `SWELL_UnregisterCustomControlCreator`
- `SWELL_GenerateDialogFromList`
- `SWELL_SetWindowLevel`, `SWELL_GetWindowWantRaiseAmt`, `SWELL_SetWindowWantRaiseAmt`
- `SWELL_GetDefaultButtonID`
- `SWELL_DrawFocusRect`
- `SWELL_IsGroupBox`, `SWELL_IsButton`, `SWELL_IsStaticText`
- `SWELL_GetDesiredControlSize`, `SWELL_DisableContextMenu`
- `MulDiv`, `lstrcpyn`

See `docs/PROTOCOLS.md §1-3, §10` and `docs/EVENT-LOOP.md §5`.

### `swell-dlg.cpp`

Dialog creation and lifecycle:
- `SWELL_DialogBox`, `SWELL_CreateDialog`, `EndDialog`
- `SWELL_MakeSetCurParms`, `SWELL_MakeButton`, `SWELL_MakeEditField`,
  `SWELL_MakeLabel`, `SWELL_MakeControl`, `SWELL_MakeCombo`, `SWELL_MakeGroupBox`,
  `SWELL_MakeCheckBox`, `SWELL_MakeListBox`
- `swell_makeButton` (internal helper)
- Dialog coordinate scaling (`SWELL_UI_SCALE`, `g_swell_ui_scale`)
- `SWELL_ModalWindowStart/Run/End`, `SWELL_CloseWindow`

See `docs/PROTOCOLS.md §2` and `docs/EVENT-LOOP.md §5`.

### `swell-controls.cpp`

All built-in control WNDPROCs. One WNDPROC per control type:

```
buttonWindowProc      Button, CheckBox, RadioButton, GroupBox
editWindowProc        Edit (single-line and multi-line, UTF-8)
labelWindowProc       Static
listViewWindowProc    ListView AND ListBox (m_is_listbox flag distinguishes)
treeViewWindowProc    TreeView
comboWindowProc       ComboBox (CBS_DROPDOWNLIST and CBS_DROPDOWN)
tabControlWindowProc  Tab control
trackbarWindowProc    Trackbar
progressWindowProc    Progress bar
```

Also: `SWELL_MakeControl` dispatcher — maps classnames to the right WNDPROC or
calls the registered `SWELL_ControlCreatorProc` chain.

See `docs/CONTROLS.md` for state structs and per-control behavior.

### `swell-menu.cpp`

- `CreatePopupMenu`, `CreatePopupMenuEx`, `DestroyMenu`, `SWELL_DuplicateMenu`
- `AddMenuItem`, `SWELL_InsertMenu`, `InsertMenuItem`, `GetMenuItemInfo`,
  `SetMenuItemInfo`, `SetMenuItemModifier`, `SetMenuItemText`, `EnableMenuItem`,
  `DeleteMenu`, `CheckMenuItem`
- `GetSubMenu`, `GetMenuItemCount`, `GetMenuItemID`
- `SetMenu`, `GetMenu`, `DrawMenuBar`
- `TrackPopupMenu`
- `SWELL_LoadMenu`, `SWELL_Menu_AddMenuItem`, `SWELL_GenerateMenuFromList`
- `SWELL_GetDefaultWindowMenu`, `SWELL_SetDefaultWindowMenu`,
  `SWELL_GetDefaultModalWindowMenu`, `SWELL_SetDefaultModalWindowMenu`,
  `SWELL_GetCurrentMenu`, `SWELL_SetCurrentMenu`
- Menu bar painting (WM_NCPAINT), hit-testing (WM_NCHITTEST), NC mouse handling

See `docs/PROTOCOLS.md §11` and `docs/SPEC.md §10`.

### `swell-misc.cpp`

Catch-all for subsystems not large enough for their own file:
- Clipboard: `OpenClipboard`, `CloseClipboard`, `EmptyClipboard`, `GetClipboardData`,
  `SetClipboardData`, `RegisterClipboardFormat`, `EnumClipboardFormats`,
  `GlobalAlloc`, `GlobalLock`, `GlobalSize`, `GlobalUnlock`, `GlobalFree`
- Drag-drop: `DragQueryFile`, `DragQueryPoint`, `DragFinish`,
  `SWELL_InitiateDragDrop`, `SWELL_InitiateDragDropOfFileList`, `SWELL_FinishDragDrop`,
  `SWELL_DDrop_*` global callbacks
- Monitors: `SWELL_GetViewPort`, `EnumDisplayMonitors`, `GetMonitorInfo`,
  `GetSystemMetrics`
- Dialogs: `MessageBox`, `BrowseForFiles`, `BrowseForSaveFile`,
  `BrowseForDirectory`, `BrowseFile_SetTemplate`
- Shell: `ShellExecute`, `GetTempPath`
- Colors/fonts: `SWELL_ChooseColor`, `SWELL_ChooseFont`
- Notify icon: `NOTIFYICONDATA` handling (stub or GDK system tray)
- Misc: `SWELL_HideApp`, `SetOpaque`, `SetAllowNoMiddleManRendering`,
  `SWELL_ExtendedAPI`, `_controlfp`
- Time: `Sleep`, `GetTickCount`, `GetFileTime`
- Module: `GetModuleFileName`, `LoadLibrary`, `LoadLibraryGlobals`,
  `GetProcAddress`, `FreeLibrary`, `SWELL_GetBundle`
- Process: `SWELL_CreateProcess`, `SWELL_GetProcessExitCode`
- Threads: `CreateThread`, `GetCurrentThreadId`, `SetThreadPriority`, `CloseHandle`,
  `CreateEvent`, `CreateEventAsSocket`, `SetEvent`, `ResetEvent`,
  `WaitForSingleObject`, `WaitForAnySocketObject`
- GUID: `SWELL_GenerateGUID`
- Rect: `SWELL_PtInRect`, `WinOffsetRect`, `WinSetRect`, `WinUnionRect`,
  `WinIntersectRect`
- Cursor: all `SWELL_*Cursor*` functions, `SWELL_Register_Cursor_Resource`
- Input: `GetCursorPos`, `GetMessagePos`, `GetAsyncKeyState`,
  `SWELL_KeyToASCII`, `SWELL_GetGestureInfo`
- ListView helpers (non-WNDPROC): all `ListView_*`, `Header_*`,
  `SWELL_GetListViewHeaderHeight`, `SWELL_SetListViewFastClickMask`
- TreeView helpers: all `TreeView_*`
- Tab control helpers: all `TabCtrl_*`
- ImageList: `ImageList_CreateEx`, `ImageList_Remove`, `ImageList_ReplaceIcon`,
  `ImageList_Add`, `ImageList_Destroy`
- GL/Metal: `SWELL_SetViewGL`, `SWELL_GetViewGL`, `SWELL_SetGLContextToView`,
  `SWELL_FillDialogBackground`

### `swell-kb.cpp`

Keyboard handling:
- `SWELL_KeyToASCII`: translates VK + modifier flags to ASCII character.
- Accelerator table processing (called from `SwellDialogDefaultWindowProc`).
- `SWELL_EnableRightClickEmulate` (macOS Ctrl+click → right-click).

### `swell-backend-gdk.cpp`

GDK OS backend. Everything that touches GdkWindow directly:
- `swell_oswindow_manage`, `swell_oswindow_destroy`, `swell_oswindow_resize`,
  `swell_oswindow_focus`, `swell_oswindow_update_style`, `swell_oswindow_update_enable`,
  `swell_oswindow_update_text`, `swell_oswindow_invalidate`,
  `swell_oswindow_updatetoscreen`, `swell_oswindow_maximize`
- `SWELL_RunEvents` — `g_main_context_iteration` loop
- `swell_gdkEventHandler` — GDK event → SWELL message translation
- `SWELL_initargs` — `gdk_init`
- `SWELL_CreateXBridgeWindow`, `SWELL_GetOSWindow`, `SWELL_GetOSEvent`
- `SWELL_GetScaling256`
- `SWELL_internalSkiaPaint` entry from expose events
- `swell_oswindow_to_hwnd`, `swell_oswindow_from_hwnd`

### `swell-backend-headless.cpp`

Headless backend (no display). Stubs `swell_oswindow_*` to no-ops. WM_PAINT
is never synthesized by OS expose; call `SWELL_internalSkiaPaint` explicitly in
tests. Useful for unit-testing window logic without a display.

### `swell-modstub.cpp`

For plugin mode (`SWELL_PROVIDED_BY_APP`): defines a `DllMain` that receives
the `SWELLAPI_GetFunc` pointer and resolves all function pointers from
`swell-functions.h`. See `docs/PROTOCOLS.md §18`.

### `swell-appstub.cpp`

For standalone app mode: provides a `main()` that calls `SWELL_initargs`,
dispatches `SWELLAppMain` lifecycle messages, runs `SWELL_RunMessageLoop`, and
handles shutdown. See `docs/EVENT-LOOP.md §11`.

**CRITICAL:** Also exports `SWELLAPI_GetFunc(const char *name)` — the function
pointer lookup table required by REAPER and other apps that resolve SWELL
functions dynamically. Without this, REAPER crashes at startup (null function
pointer → SIGSEGV at 0x0). See `docs/DEBUGGING.md`.

---

## Key Design Constraints

### ABI compatibility

Public structs in `swell-types.h` are fixed. Do not add fields, reorder fields,
or change sizes of any type declared there. All changes to internal
representation happen in `swell-internal.h` only.

Function signatures in `swell-functions.h` are fixed. The implementation must
match them exactly.

### No CreatWindow / RegisterClass

SWELL has no `RegisterClass` or `CreateWindowEx`. Windows are created only via:
- `SWELL_CreateDialog` / `SWELL_DialogBox` (resource-based)
- `SWELL_CreateDialog(head, NULL, parent, (DLGPROC)wndproc, param)` (bare WNDPROC)
- `new HWND__(...)` within control creator callbacks (internal only)

Document this constraint clearly in comments wherever window allocation occurs.

### Thread safety rules

Only `PostMessage` and `GetTickCount` are thread-safe. Everything else is
main-thread only. Use `WDL_Mutex` from `../../mutex.h` for the PostMessage queue
and timer list. See `docs/PROTOCOLS.md §16`.

### String encoding

All text is UTF-8. Internal storage is `WDL_FastString`. `HWND__::m_title`
holds the window/control text. Character positions (cursor, selection) are
Unicode character indices, not byte offsets. Convert with
`WDL_utf8_charpos_to_bytepos`.

### Message delivery invariants

`SendMessage` is synchronous and non-reentrant per HWND (unless the proc
itself calls SendMessage recursively). After `m_hashaddestroy == 2`, SendMessage
returns 0 without calling the proc. See `docs/PROTOCOLS.md §1.1`.

### BOOL type

`BOOL` is `signed char`. Return `TRUE` (1) or `FALSE` (0). Never `!= FALSE`
comparisons against `int` return values. See `docs/SPEC.md §2.1`.

### RGB byte order

Default (non-Win32): `RGB(r,g,b) = (r<<16)|(g<<8)|b`. All GDI internals use
SkColor format (`SWELL_TO_SKCOLOR` converts). See `docs/SPEC.md §4.2`
and `docs/RENDERING.md §2.3`.

### SW_* constant values differ from Win32

`SW_SHOW=2`, `SW_SHOWNA=1`, etc. Never hardcode numeric values; use the
named constants from `swell-types.h`. See `docs/SPEC.md §3.5`.

### Keyboard lParam is not Win32

`lParam` for `WM_KEYDOWN`/`WM_KEYUP` is modifier flags (`FVIRTKEY|FSHIFT|
FCONTROL|FALT|FLWIN|0x1000000`), NOT the Win32 scan-code/repeat-count format.
See `docs/PROTOCOLS.md §19`.

---

## WDL Dependencies

swell2 depends on the WDL library (`../` from this directory — the parent of `swell2/`):

```
Skia              Skia 2D graphics library (SkCanvas, SkSurface, SkPaint, SkBitmap,
                   SkBlendMode, sk_sp<>, etc.)
../mutex.h        WDL_Mutex, WDL_MutexLock
../wdlstring.h    WDL_FastString (= WDL_String alias, UTF-8 string)
../ptrlist.h      WDL_PtrList<T>, WDL_PtrList_DeleteOnDestroy<T>
../heapbuf.h      WDL_TypedBuf<T> (templated typed buffer)
../wdlcstring.h   lstrcpyn_safe, WDL_stricmp, etc.
../wdlutf8.h      WDL_utf8_charpos_to_bytepos, WDL_utf8_get_charlen, etc.
```

### Skia version notes

The installed Skia may be a recent version (m118+) where the API differs from
older docs:

| Old API | New API |
|---------|---------|
| `SkSurface::MakeRasterN32Premul(w,h)` | `SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w,h))` |
| `SkSurface::MakeNull(w,h)` | `SkSurfaces::Null(w,h)` |

Check `/usr/include/include/core/SkSurface.h` for the exact signatures in your
installation. Use `pkg-config --cflags skia` for include paths.

### NOMINMAX

`swell-types.h` defines `min`/`max` macros that conflict with Skia and STL.
Always define `NOMINMAX` before including `swell.h` (CMake does this via
`target_compile_definitions`). Implementation files that include `swell.h`
before STL headers should `#undef min` / `#undef max` after the include.

---

## swell.h Double-Include Trick

`swell.h` includes `swell-functions.h` **outside** the `_WDL_SWELL_H_` include
guard. This is intentional — it allows `swell-appstub.cpp` to generate a
name→function-pointer lookup table:

```cpp
#include "swell.h"                    // 1st pass: normal declarations
#undef _WDL_SWELL_H_API_DEFINED_
#undef SWELL_API_DEFINE
#define SWELL_API_DEFINE(ret,fn,parms) {#fn, (void *)fn},
static struct api_ent { const char *name; void *func; } api_table[] = {
  #include "swell-functions.h"        // 2nd pass: table entries
};
```

`SWELLAPI_GetFunc` does a binary search on `api_table`. This is how REAPER
resolves all ~240 SWELL function pointers at load time.

---

## Suggested Implementation Order

Implement in this order to keep each step independently testable:

1. `swell-internal.h` — all internal type definitions ✅
2. `swell-gdi-internalpool.h` — pool infrastructure ✅ (merged into swell-gdi.cpp)
3. `swell-ini.cpp` — no dependencies; self-contained; easy to test ✅
4. `swell-gdi.cpp` — depends on Skia and pool only ✅ (real drawing, not stubs)
5. `swell-wnd.cpp` (partial) — HWND__ lifecycle, SendMessage, PostMessage queue, timers ✅
6. `swell-backend-headless.cpp` — lets you test window logic without GDK ✅
7. **`swell-appstub.cpp`** — SWELLAPI_GetFunc export ✅
8. **`swell-stubs.cpp`** — stubs for ALL remaining SWELL_API_DEFINE functions ✅
9. **`swell-backend-sdl3.cpp`** — SDL3 OS backend for real windows, events, rendering ✅
10. `swell-controls.cpp` — depends on wnd + gdi ✅
11. `swell-dlg.cpp` — depends on controls + wnd ✅
12. `swell-menu.cpp` — depends on wnd + gdi
13. `swell-misc.cpp` — depends on everything above
14. `swell-kb.cpp` — depends on wnd
15. `swell-backend-gdk.cpp` — OS integration; depends on everything (deferred)
16. `swell-modstub.cpp` — thin; implement last
17. `CMakeLists.txt` — wire it all together ✅ (C++17, Skia, SDL3 via pkg-config)

**Critical:** You MUST provide a definition for every function declared via
`SWELL_API_DEFINE` in `swell-functions.h`. Even stub no-ops are sufficient.
A missing symbol means REAPER gets NULL from `SWELLAPI_GetFunc` → crash.
Use `comm -23 <(grep SWELL_API_DEFINE swell-functions.h declarations) <(nm -D libSwell.so defined)` to audit.

Note: some functions in `swell-functions.h` are declared unconditionally
despite "macOS only" comments in the source. Check the actual preprocessor
scope, not the comment. Example: `SWELL_SetMenuDestination`.

---

## REAPER Status (2026-05-13)

### Current state: RUNNING ✅

REAPER runs with `libSwell.so` overlay via bwrap, stays alive indefinitely (killed
only by external timeout/SIGKILL). 37k+ SWELL calls per 7s run. SDL3 backend
creates windows, processes events, paints via Skia. Main window fully alive.

**Full set of SWELL functions called during a normal REAPER session:**

```
AddFontResourceEx, BeginPaint, BitBlt, ClientToScreen, CreateEvent, CreateFont,
CreateFontIndirect, CreatePen, CreateSolidBrush, DefWindowProc, DeleteMenu,
DeleteObject, DestroyWindow, DrawMenuBar, EnableMenuItem, EnableWindow, EndPaint,
GetAsyncKeyState, GetCapture, GetClassName, GetClientRect, GetCurrentThreadId,
GetCursorPos, GetDC, GetDlgItem, GetFocus, GetForegroundWindow, GetMenu,
GetMenuItemCount, GetModuleFileName, GetParent, GetPrivateProfileInt,
GetPrivateProfileString, GetPrivateProfileStruct, GetProp, GetStockObject,
GetSubMenu, GetSysColor, GetSystemMetrics, GetTempPath, GetTextMetrics,
GetTickCount, GetWindow, GetWindowContentViewRect, GetWindowDC, GetWindowLong,
GetWindowRect, InvalidateRect, IsChild, IsWindowVisible, KillTimer, LineTo,
LoadLibrary, LoadNamedImage, lstrcpyn, MoveToEx, MulDiv, RegisterClipboardFormat,
ReleaseCapture, ReleaseDC, RemoveProp, ScreenToClient, SelectObject, SendMessage,
SetBkColor, SetBkMode, SetCapture, SetDlgItemText, SetFocus, SetForegroundWindow,
SetMenu, SetMenuItemInfo, SetParent, SetProp, SetTextColor, SetThreadPriority,
SetTimer, SetWindowLong, SetWindowPos, ShowWindow, Sleep, SWELL_CreateDialog,
SWELL_CreateMemContext, SWELL_DeleteGfxContext, SWELL_DrawText,
SWELL_EnableRightClickEmulate, SWELL_ExtendedAPI, SWELL_FillDialogBackground,
SWELL_FillRect, SWELL_GenerateGUID, SWELL_GetCtxFrameBuffer,
SWELL_GetDefaultFont, SWELL_GetScaling256, SWELL_GetViewPort,
SWELL_initargs, SWELL_Internal_PostMessage_Init, SWELL_internalSkiaPaint,
SWELL_IsGroupBox, SWELL_LoadCursor, SWELL_LoadMenu, SWELL_MessageQueue_Clear,
SWELL_MessageQueue_Flush, SWELL_Polygon, SWELL_PtInRect,
SWELL_Register_Cursor_Resource, SWELL_RegisterCustomControlCreator,
SWELL_RunEvents, SWELL_RunMessageLoop, SWELL_SetClassName, SWELL_SetCursor,
SWELL_SetMenuDestination, SWELL_SetWindowWantRaiseAmt, SwellDialogDefaultWindowProc,
TrackPopupMenu, UpdateWindow, WindowFromPoint, WinIntersectRect, WinOffsetRect,
WinSetRect, WritePrivateProfileString, WritePrivateProfileStruct,
swell_DirtyContext, swell_oswindow_focus, swell_oswindow_invalidate,
swell_oswindow_manage, swell_oswindow_resize, swell_oswindow_update_enable,
swell_oswindow_update_style, swell_oswindow_updatetoscreen
```

**What is NOT yet called (open work):** ListView_*, TreeView_*, TabCtrl_* helper
functions (now implemented as SendMessage dispatchers), menu creation functions
beyond stubs, clipboard, drag-drop, file dialogs.

### Earlier state (pre-swell-dlg.cpp): self-exit

Before `swell-dlg.cpp` and `swell-controls.cpp` were implemented, REAPER exited
with code 0 immediately. Root cause: `SWELL_CreateDialog` returned `NULL` →
REAPER main window creation failed → REAPER fell through and exited normally.

---

## Session Findings (2026-05-13)

### REAPER is single-instance: kill before testing

REAPER uses a Unix socket or shared file for single-instance detection. If a
prior bwrap run left REAPER alive, a new run prints `activating running instance...done`
and exits immediately (EXIT:0). Always kill existing instances before a test run:

```bash
ps aux | grep "REAPER/reaper" | grep -v grep | awk '{print $2}' | xargs -r kill -9
```

### bwrap must bind config dirs and /proc

The minimal bwrap invocation in older docs is missing critical binds. Without
`--bind ~/.config/REAPER` REAPER has no config; without `--proc /proc` and
`--setenv DISPLAY :0` it fails before even printing SWELL calls. Use this full
command (from `docs/DEBUGGING.md`):

```bash
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
  -- timeout --kill-after=2 5 /usr/lib/REAPER/reaper
echo "EXIT: $?"
```

### timeout --kill-after=2 is required

Plain `timeout 5 reaper` hangs when REAPER catches SIGTERM and doesn't exit
(it tries to save the project). The outer timeout process then waits forever.
Use `timeout --kill-after=2 5 reaper` — sends SIGTERM at 5s, SIGKILL at 7s.

### Exit code 137 = success (bwrap killed), not failure

`bwrap ... -- timeout --kill-after=2 5 reaper; echo $?` prints `137` when the
kill-after SIGKILL fires (128+9). This means REAPER ran the full timeout period
without self-exiting. It is the success condition. Self-exit is EXIT:0.

### Pipeline exit code masks bwrap's exit code

`bwrap ... reaper 2>&1 | grep ... | tail -20; echo "EXIT: $?"` prints the exit
code of `tail`, always 0. Capture bwrap's exit code before the pipe:
```bash
bwrap ... reaper > /tmp/reaper.log 2>&1; echo "EXIT: $?"
```

### MAKEINTRESOURCE is NOT a string — never strcmp it

REAPER passes integer resource IDs as `(const char*)n` (MAKEINTRESOURCE). Calling
`strcmp(r->resid, resid)` on such a pointer crashes immediately (reads from address
`0x66` etc.). In `SWELL_CreateDialog`: check `(size_t)resid <= 0xFFFF` before
any string operation. Use pointer equality for integer IDs; `strcmp` only when
both sides are confirmed real string pointers (value > 0xFFFF).

### LVM_*/TVM_*/TCM_* constants not in swell-types.h

`swell-types.h` intentionally omits these message constants (they're defined by
the control implementor). Both `swell-controls.cpp` and `swell-stubs.cpp` need
local `#define` blocks. Values are standard Win32: `LVM_FIRST=0x1000`,
`TCM_FIRST=0x1300`, `TVM_FIRST=0x1100`. The SWELL2-specific tree navigation
messages (TVM_GETSELECTION, TVM_GETPARENT, TVM_GETCHILD, TVM_GETNEXTSIBLING,
TVM_GETROOT, TVM_DELETEALLITEMS) use TVM_FIRST+60..65.

### WDL_PtrList::Insert argument order

```cpp
// WRONG: list.Insert(item, index)
// RIGHT:
list.Insert(index, item);   // index comes FIRST
```

### swell-types.h #if 0 guard on SM_* constants

`swell-types.h:1434` wraps `SM_CYCAPTION`, `SM_CXBORDER`, `SM_CXDLGFRAME`, etc.
in `#if 0 // disabled until implemented`. Change to `#if 1` when implementing
`GetSystemMetrics`. Check the file for other `#if 0` blocks before assuming a
constant is missing.

### SWELL_GetCtxFrameBuffer must return the Skia raster pointer

REAPER calls this from its WM_TIMER handler and crashes if it returns nullptr.
Correct implementation:
```cpp
SkPixmap pm;
if (ct->surface->peekPixels(&pm))
  return const_cast<void*>(pm.addr());
return nullptr;
```

### ListView/TreeView/TabCtrl helpers live in swell-stubs.cpp, not controls

The `ListView_*`, `TreeView_*`, `TabCtrl_*` convenience functions are standalone
C functions (not WNDPROC messages). They dispatch to the control's WNDPROC via
`SendMessage`. They live in `swell-stubs.cpp` (or eventually `swell-misc.cpp`)
and should **not** be placed in `swell-controls.cpp`.

### Build config note

CMake auto-detects SDL3 via pkg-config. If SDL3 is not found, falls back to
headless backend. GPU errors from Mesa/amdgpu at runtime are harmless — REAPER
uses Skia's software raster backend, not OpenGL.

### swell-types.h is a kept header but may need small edits

The `#if 0` guards on disabled constants are the only expected changes to
kept headers. Always keep the API surface (types, constants, function
signatures) unchanged.
