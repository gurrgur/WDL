# SWELL Event Loop and OS Backend

This document specifies the message loop architecture, OS window lifecycle,
event translation, WM_PAINT synthesis, and backend integration contracts.

---

## 1. SWELL_RunMessageLoop

The main loop function. Call from any tight loop that needs to process events.
Modal dialog loops call `SWELL_RunMessageLoop()` + `Sleep(10)` continuously.

```c
void SWELL_RunMessageLoop()
{
  SWELL_MessageQueue_Flush();  // step 1: process PostMessage queue
  SWELL_RunEvents();           // step 2: dispatch OS events
                               // step 3: fire due timers (in caller after RunEvents)
  // timer loop: iterate m_timer_list, fire each due timer
}
```

### Step 1: PostMessage queue flush

`SWELL_MessageQueue_Flush()`:
- Takes a snapshot of the current queue size (`n = current_size`).
- Dequeues and calls `SendMessage` for up to `n` messages.
- Thread-safe dequeue; `SendMessage` itself must be on main thread.
- Messages to destroyed windows are silently dropped.

### Step 2: OS event dispatch (SDL3 backend)

`SWELL_RunEvents()` (SDL3):
```c
SDL_Event evt;
while (SDL_PollEvent(&evt))
{
  swell_sdlEventHandler(&evt);
}
```

This processes all pending SDL3 events without blocking.

### Step 3: Timer processing

After `SWELL_RunEvents()` returns, the timer list is scanned:
```c
TimerInfoRec *rec = m_timer_list;
while (rec) {
  if (WDL_TICKS_IN_RANGE_ENDING_AT(rec->lastFire, now - rec->interval, 100000)) {
    rec->lastFire = GetTickCount();
    ++rec->refcnt;
    // fire: tProc(hwnd, WM_TIMER, id, now)  OR  SendMessage(hwnd, WM_TIMER, id, 0)
    --rec->refcnt;
    if (refcnt < 0) free_timer(rec);
  }
  rec = rec->_next;
}
```

Timer fires if `lastFire` is in the range `[now - interval - 100000, now - interval]`.
The 100,000 ms tolerance handles timer starvation (e.g., modal loops with Sleep).

Timer fires are limited to one pass per `SWELL_RunMessageLoop` call (no repeated
firing of the same timer in one pass).

---

## 2. SDL3 Event Translation

`swell_sdlEventHandler` translates raw SDL3 events into SWELL messages:

| SDL3 event | SWELL message |
|---|---|
| `SDL_EVENT_MOUSE_BUTTON_DOWN` (button=LEFT) | `WM_LBUTTONDOWN` |
| `SDL_EVENT_MOUSE_BUTTON_UP` (button=LEFT) | `WM_LBUTTONUP` |
| `SDL_EVENT_MOUSE_BUTTON_DOWN` (button=LEFT, clicks=2) | `WM_LBUTTONDBLCLK` |
| `SDL_EVENT_MOUSE_BUTTON_DOWN` (button=RIGHT) | `WM_RBUTTONDOWN` |
| `SDL_EVENT_MOUSE_BUTTON_UP` (button=RIGHT) | `WM_RBUTTONUP` |
| `SDL_EVENT_MOUSE_BUTTON_DOWN` (button=MIDDLE) | `WM_MBUTTONDOWN` |
| `SDL_EVENT_MOUSE_MOTION` | `WM_MOUSEMOVE` |
| `SDL_EVENT_MOUSE_WHEEL` | `WM_MOUSEWHEEL` or `WM_MOUSEHWHEEL` |
| `SDL_EVENT_KEY_DOWN` | `WM_KEYDOWN` (+ possibly `WM_CHAR`) |
| `SDL_EVENT_KEY_UP` | `WM_KEYUP` |
| `SDL_EVENT_WINDOW_FOCUS_GAINED` | `WM_ACTIVATE`, `WM_SETFOCUS` on focused child |
| `SDL_EVENT_WINDOW_FOCUS_LOST` | `WM_ACTIVATE` (WA_INACTIVE), `WM_KILLFOCUS` |
| `SDL_EVENT_WINDOW_EXPOSED` | triggers `SWELL_internalLICEpaint` |
| `SDL_EVENT_WINDOW_RESIZED` / `SDL_EVENT_WINDOW_MOVED` | `SetWindowPos`-equivalent, `WM_SIZE` / `WM_MOVE` |
| `SDL_EVENT_WINDOW_CLOSE_REQUESTED` | `WM_CLOSE` (→ `DestroyWindow` if unhandled) |

Mouse coordinate translation:
- SDL3 reports window-relative coordinates; converted to HWND client coords via `ScreenToClient`.
- Hit-testing via `ChildWindowFromPoint` routes events to the correct child HWND.

Mouse routing:
- If a window holds mouse capture (`g_swell_capture`), all mouse events route there.
- Otherwise: route to deepest visible child at the mouse position.

---

## 3. OS Window Lifecycle (SDL3 backend)

### 3.1 Data structures

```c
typedef SDL_Window* SWELL_OSWINDOW;

// In HWND__:
SWELL_OSWINDOW m_oswindow;         // the SDL_Window, or NULL for child windows
int m_oswindow_private;            // PRIVATE_NEEDSHOW, PRIVATE_NEEDHIDE, etc.
int m_oswindow_fullscreen;         // saved style flags for fullscreen toggle
```

Only **top-level** windows have an SDL_Window (`m_oswindow`). Child windows (those
with a parent) do not have their own OS window; they paint into the top-level's
SDL_Window via `SWELL_internalLICEpaint`.

### 3.2 swell_oswindow_manage(hwnd, wantFocus)

Creates the SDL_Window for a top-level HWND. Called by `ShowWindow`/`SWELL_CreateDialog`
when a window becomes visible for the first time.

Sequence:
1. Determine window flags from `hwnd->m_style`:
   - `WS_CAPTION | WS_THICKFRAME` → `SDL_WINDOW_RESIZABLE`
   - `WS_CAPTION` → default decorations, no resize
   - neither → `SDL_WINDOW_BORDERLESS`
2. `SDL_CreateWindow(title, w, h, flags)` — creates SDL_Window.
3. `SDL_ShowWindow(hwnd->m_oswindow)` if wantFocus.
4. Registers in the HWND → SDL_Window mapping for hit-testing.

### 3.3 swell_oswindow_destroy(hwnd)

1. Unregisters from the global window map.
2. `SDL_DestroyWindow(hwnd->m_oswindow)`.
3. Clears `hwnd->m_oswindow = NULL`.

Called from `RecurseDestroyWindow` as part of `DestroyWindow`.

### 3.4 swell_oswindow_resize(wnd, reposflag, rect)

Updates SDL_Window position/size to match `HWND__::m_position`:
- `reposflag & 1` (move): `SDL_SetWindowPosition(wnd, x, y)`
- `reposflag & 2` (size): `SDL_SetWindowSize(wnd, w, h)`

Called by `SetWindowPos` after updating `m_position`.

### 3.5 swell_oswindow_update_style(hwnd, oldstyle)

Called when `WS_CAPTION` changes (via `SetWindowLong(GWL_STYLE, ...)`):
- Hides owned windows transiently.
- Calls `SDL_HideWindow`, changes border flags, sets `PRIVATE_NEEDSHOW`.
- The actual show happens on the next event loop iteration.

---

## 4. WM_PAINT Synthesis (LICE backend)

### 4.1 Invalidation

`InvalidateRect(hwnd, rect, eraseBk)`:
1. Sets `hwnd->m_invalidated = true`.
2. Walks up ancestor chain: sets `m_child_invalidated = true` on each parent.
3. Calls `swell_oswindow_invalidate(topLevel, &screenRect)`:
   - SDL3: marks the window dirty; `SDL_EVENT_WINDOW_EXPOSED` is synthesized or
     paint is triggered directly on next event loop pass.

### 4.2 Paint dispatch

On SDL3 expose event (`SDL_EVENT_WINDOW_EXPOSED`):
1. SDL3 calls the SWELL expose handler.
2. Handler calls `SWELL_internalLICEpaint(topLevel, bitmap, 0, 0, false)`.
3. After paint, `swell_oswindow_updatetoscreen(topLevel, &dirtyRect)`:
   - Copies the LICE bitmap region to the SDL_Window via `SDL_UpdateTexture`
     and `SDL_RenderPresent`.

### 4.3 SWELL_internalLICEpaint (recursive)

```
SWELL_internalLICEpaint(hwnd, bmout, bmout_xpos, bmout_ypos, forceref):
  if hwnd->m_invalidated: forceref = true
  if forceref or hwnd->m_child_invalidated:
    create swell_gdpLocalContext ctx with:
      ctx.surface = bmout
      ctx.surface_offs = {-bmout_xpos, -bmout_ypos}
      ctx.clipr = {bmout_xpos, bmout_ypos, bmout_xpos+w, bmout_ypos+h}
    hwnd->m_paintctx = &ctx

    // non-client area
    SendMessage(hwnd, WM_NCCALCSIZE, FALSE, &nccalc)  // get NC inset {dx, dy}
    if forceref: SendMessage(hwnd, WM_NCPAINT, 1, 0)
    adjust ctx by NC inset

    // client area
    ctx.ctx.curfont = hwnd->m_font
    if forceref and clip non-empty:
      SendMessage(hwnd, WM_PAINT, (WPARAM)&ctx, 0)

    hwnd->m_paintctx = NULL
    hwnd->m_invalidated = false

  // recurse into children
  for each child (in z-order, first = bottom):
    if child->m_visible and (forceref or child->m_invalidated or child->m_child_invalidated):
      create LICE_SubBitmap for child's area
      SWELL_internalLICEpaint(child, subbm, -xoffset, -yoffset, forceref)

  if no child extends outside clip: hwnd->m_child_invalidated = false
```

Children are painted **after** the parent, so children appear on top.  
Children are painted in m_children list order (first child = bottom of z-order;
last visible child is painted last = on top).

### 4.4 Backing store

Top-level windows maintain a `LICE_MemBitmap *m_backingstore`.  
`GetDC`/`GetWindowDC`/`ReleaseDC` use this backing store for immediate rendering
outside of a WM_PAINT cycle.

---

## 5. Window Creation Without a Dialog Template

SWELL has no `RegisterClass`/`CreateWindowEx`. Windows are created by:

### 5.1 Modal / modeless dialog (resource-based)

```c
SWELL_CreateDialog(head, resid, parent, dlgproc, param)
```
- Looks up `resid` in the `SWELL_DialogResourceIndex` linked list.
- Calls `createFunc(hwnd, 0)` to build controls.
- Sets up `SwellDialogDefaultWindowProc` as the WNDPROC.
- Fires `WM_INITDIALOG`.

### 5.2 Opaque child window (resid == NULL)

```c
HWND child = SWELL_CreateDialog(head, NULL, parent, (DLGPROC)myWndProc, param)
```
- Allocates `HWND__` directly.
- Stores `myWndProc` as `m_wndproc` (NOT wrapped — no dlgproc, no WM_INITDIALOG).
- Fires `WM_CREATE` with `lParam = param`.
- The proc is called directly as a WNDPROC.

This is the only way to create a window with a bare WNDPROC (no dialog wrapper).

### 5.3 Custom controls within a dialog

When `SWELL_MakeControl` encounters a classname not in the built-in list:
1. Walks the registered `SWELL_ControlCreatorProc` chain (last-registered first).
2. First proc that returns non-NULL wins.
3. If none match: creates a plain `HWND__` with no WNDPROC.

Built-in controls created by `SWELL_Make*` functions call `new HWND__()` directly
and set their own WNDPROC:

```c
HWND = new HWND__(parent, id, &tr, label, visible, myControlWindowProc);
hwnd->m_classname = "Edit";
hwnd->m_private_data = (INT_PTR) new __SWELL_editControlState;
hwnd->m_wndproc(hwnd, WM_CREATE, 0, 0);
```

---

## 6. Window Style → SDL3 Window Flags Mapping

| HWND style | SDL3 flags |
|---|---|
| `WS_CAPTION | WS_THICKFRAME` | `SDL_WINDOW_RESIZABLE` (decorated by default) |
| `WS_CAPTION` (no resize) | default decorations, no `SDL_WINDOW_RESIZABLE` |
| neither | `SDL_WINDOW_BORDERLESS` |

---

## 7. Focus Routing (SDL3 backend)

On `SDL_EVENT_WINDOW_FOCUS_GAINED`:
1. Find the HWND for the newly focused SDL_Window.
2. Set `SWELL_focused_oswindow = oswindow`.
3. Send `WM_ACTIVATE(WA_ACTIVE, ...)` to the hwnd.
4. Send `WM_SETFOCUS` to `GetFocus()` (the hwnd's focused child, if any).

On `SDL_EVENT_WINDOW_FOCUS_LOST`:
1. Send `WM_KILLFOCUS` to current `GetFocus()`.
2. Send `WM_ACTIVATE(WA_INACTIVE, ...)`.
3. Clear `SWELL_focused_oswindow`.

---

## 8. Keyboard Routing (SDL3 backend)

On `SDL_EVENT_KEY_DOWN`:
1. Translate SDL_Keycode + SDL_Keymod → `wParam` (VK_*) and `lParam` (modifier flags).
2. Dispatch to `GetFocus()` as `WM_KEYDOWN`.
3. If handled (returns non-zero): done.
4. If not handled: `DefWindowProc` bubbles to parent.
5. For printable characters: also send `WM_CHAR(charcode, lParam)`.

SDL3 → VK mapping: `SDLK_RETURN` → `VK_RETURN`, `SDLK_TAB` → `VK_TAB`, etc.

Modifier flag construction:
```c
lParam = FVIRTKEY;
if (mod & SDL_KMOD_SHIFT) lParam |= FSHIFT;
if (mod & SDL_KMOD_CTRL)  lParam |= FCONTROL;
if (mod & SDL_KMOD_ALT)   lParam |= FALT;
if (mod & SDL_KMOD_GUI)   lParam |= FLWIN;
if (is_extended_key(sym)) lParam |= 0x1000000;
```

---

## 9. Mouse Capture (global state)

```c
static HWND g_swell_capture = NULL;
```

- `SetCapture(hwnd)`: sets `g_swell_capture = hwnd`; sends WM_CAPTURECHANGED to old.
- `ReleaseCapture()`: sends WM_CAPTURECHANGED to `g_swell_capture`; clears it.
- `GetCapture()`: returns `g_swell_capture`.

During event routing: if `g_swell_capture != NULL`, all mouse events route to it
regardless of cursor position. Coordinates are still converted to that window's
client space.

---

## 10. Modal Dialog OS Window Pool (EndDialog)

To reduce OS window flicker in rapid dialog open/close sequences:

After `EndDialog`, the SDL_Window can be saved in a spare pool for 100ms (500ms if
app is inactive). The next `SWELL_DialogBox` call will reuse the pooled window:
- `SW_SHOWNA` (show without focus steal) is used when reusing.
- Pooled window is destroyed by `swell_dlg_destroyspare()` when the TTL expires.

Pool size: 1 window (the most recently closed modal dialog).

---

## 11. App Startup Sequence (generic/SDL3)

```
main()
  SWELL_initargs(&argc, &argv)     // SDL3 init: SDL_Init(SDL_INIT_VIDEO)
  SWELLAppMain(SWELLAPP_ONLOAD, 0, 0)
  SWELLAppMain(SWELLAPP_LOADED, 0, 0)
  while (!quit):
    SWELL_RunMessageLoop()
    Sleep(10)
  SWELLAppMain(SWELLAPP_SHOULDDESTROY, 0, 0)  // return 0 to allow quit
  SWELLAppMain(SWELLAPP_DESTROY, 0, 0)
```

The app creates its main window in `SWELLAPP_LOADED`.

---

*End of SWELL Event Loop and OS Backend*
