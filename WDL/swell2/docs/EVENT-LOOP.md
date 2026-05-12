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

### Step 2: OS event dispatch (GDK backend)

`SWELL_RunEvents()` (GDK):
```c
GMainContext *ctx = g_main_context_default();
while (g_main_context_iteration(ctx, FALSE))
{
  GdkEvent *evt;
  while (gdk_events_pending() && (evt = gdk_event_get()))
  {
    swell_gdkEventHandler(evt, (gpointer)1);
    gdk_event_free(evt);
  }
}
```

This processes all pending GDK events without blocking.

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

## 2. GDK Event Translation

`swell_gdkEventHandler` translates raw GDK events into SWELL messages:

| GDK event | SWELL message |
|---|---|
| `GDK_BUTTON_PRESS` (button=1) | `WM_LBUTTONDOWN` |
| `GDK_BUTTON_RELEASE` (button=1) | `WM_LBUTTONUP` |
| `GDK_2BUTTON_PRESS` (button=1) | `WM_LBUTTONDBLCLK` |
| `GDK_BUTTON_PRESS` (button=3) | `WM_RBUTTONDOWN` |
| `GDK_BUTTON_RELEASE` (button=3) | `WM_RBUTTONUP` |
| `GDK_BUTTON_PRESS` (button=2) | `WM_MBUTTONDOWN` |
| `GDK_MOTION_NOTIFY` | `WM_MOUSEMOVE` |
| `GDK_SCROLL` | `WM_MOUSEWHEEL` or `WM_MOUSEHWHEEL` |
| `GDK_KEY_PRESS` | `WM_KEYDOWN` (+ possibly `WM_CHAR`) |
| `GDK_KEY_RELEASE` | `WM_KEYUP` |
| `GDK_FOCUS_CHANGE` (in) | `WM_ACTIVATE`, `WM_SETFOCUS` on focused child |
| `GDK_FOCUS_CHANGE` (out) | `WM_ACTIVATE` (WA_INACTIVE), `WM_KILLFOCUS` |
| `GDK_EXPOSE` / `GDK_DAMAGE` | triggers `SWELL_internalLICEpaint` |
| `GDK_CONFIGURE` | `SetWindowPos`-equivalent, `WM_SIZE` / `WM_MOVE` |
| `GDK_DELETE` | `WM_CLOSE` (→ `DestroyWindow` if unhandled) |

Mouse coordinate translation:
- GDK reports screen coordinates; converted to HWND client coords via `ScreenToClient`.
- Hit-testing via `ChildWindowFromPoint` routes events to the correct child HWND.

Mouse routing:
- If a window holds mouse capture (`g_swell_capture`), all mouse events route there.
- Otherwise: route to deepest visible child at the mouse position.

---

## 3. OS Window Lifecycle (GDK backend)

### 3.1 Data structures

```c
typedef GdkWindow* SWELL_OSWINDOW;

// In HWND__:
SWELL_OSWINDOW m_oswindow;         // the GDkWindow, or NULL for child windows
int m_oswindow_private;            // PRIVATE_NEEDSHOW, PRIVATE_NEEDHIDE, etc.
int m_oswindow_fullscreen;         // saved style flags for fullscreen toggle
```

Only **top-level** windows have a GdkWindow (`m_oswindow`). Child windows (those
with a parent) do not have their own OS window; they paint into the top-level's
GdkWindow via `SWELL_internalLICEpaint`.

### 3.2 swell_oswindow_manage(hwnd, wantFocus)

Creates the GdkWindow for a top-level HWND. Called by `ShowWindow`/`SWELL_CreateDialog`
when a window becomes visible for the first time.

Sequence:
1. Determine window attributes from `hwnd->m_style`:
   - `WS_CAPTION | WS_THICKFRAME` → decorated with resize
   - `WS_CAPTION` → decorated, fixed size
   - neither → undecorated (tool window)
2. `gdk_window_new(parent, &attr, mask)` — creates GdkWindow.
3. Connect GDK event handlers (expose, configure, key, button, motion, etc.).
4. `gdk_window_show(hwnd->m_oswindow)` if wantFocus.
5. Registers in the HWND → GdkWindow mapping for hit-testing.

### 3.3 swell_oswindow_destroy(hwnd)

1. Unregisters from the global window map.
2. `gdk_window_destroy(hwnd->m_oswindow)`.
3. Clears `hwnd->m_oswindow = NULL`.

Called from `RecurseDestroyWindow` as part of `DestroyWindow`.

### 3.4 swell_oswindow_resize(wnd, reposflag, rect)

Updates GdkWindow position/size to match `HWND__::m_position`:
- `reposflag & 1` (move): `gdk_window_move(wnd, x, y)`
- `reposflag & 2` (size): `gdk_window_resize(wnd, w, h)`

Called by `SetWindowPos` after updating `m_position`.

### 3.5 swell_oswindow_update_style(hwnd, oldstyle)

Called when `WS_CAPTION` changes (via `SetWindowLong(GWL_STYLE, ...)`):
- Hides owned windows transiently.
- Calls `gdk_window_hide`, changes decorations, sets `PRIVATE_NEEDSHOW`.
- The actual show happens on the next event loop iteration.

---

## 4. WM_PAINT Synthesis (LICE backend)

### 4.1 Invalidation

`InvalidateRect(hwnd, rect, eraseBk)`:
1. Sets `hwnd->m_invalidated = true`.
2. Walks up ancestor chain: sets `m_child_invalidated = true` on each parent.
3. Calls `swell_oswindow_invalidate(topLevel, &screenRect)`:
   - GDK: `gdk_window_invalidate_rect(oswindow, &gdkRect, FALSE)` — schedules
     an expose event.

### 4.2 Paint dispatch

On GDK expose event (`GDK_EXPOSE`):
1. GDK calls the SWELL expose handler.
2. Handler calls `SWELL_internalLICEpaint(topLevel, bitmap, 0, 0, false)`.
3. After paint, `swell_oswindow_updatetoscreen(topLevel, &dirtyRect)`:
   - Copies the LICE bitmap region to the GdkWindow via `gdk_draw_rgb_32_image`
     or Cairo surface.

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

## 6. Window Style → GDK Decorations Mapping

| HWND style | GDK decorations |
|---|---|
| `WS_CAPTION | WS_THICKFRAME` | `GDK_DECOR_ALL | GDK_DECOR_MENU` |
| `WS_CAPTION` (no resize) | `GDK_DECOR_BORDER | GDK_DECOR_TITLE | GDK_DECOR_MINIMIZE` |
| neither | `GdkWMDecoration(0)` (undecorated) |

---

## 7. Focus Routing (GDK backend)

On `GDK_FOCUS_CHANGE (in)`:
1. Find the HWND for the newly focused GdkWindow.
2. Set `SWELL_focused_oswindow = oswindow`.
3. Send `WM_ACTIVATE(WA_ACTIVE, ...)` to the hwnd.
4. Send `WM_SETFOCUS` to `GetFocus()` (the hwnd's focused child, if any).

On `GDK_FOCUS_CHANGE (out)`:
1. Send `WM_KILLFOCUS` to current `GetFocus()`.
2. Send `WM_ACTIVATE(WA_INACTIVE, ...)`.
3. Clear `SWELL_focused_oswindow`.

---

## 8. Keyboard Routing (GDK backend)

On `GDK_KEY_PRESS`:
1. Translate GDK keyval + modifiers → `wParam` (VK_*) and `lParam` (modifier flags).
2. Dispatch to `GetFocus()` as `WM_KEYDOWN`.
3. If handled (returns non-zero): done.
4. If not handled: `DefWindowProc` bubbles to parent.
5. For printable characters: also send `WM_CHAR(charcode, lParam)`.

GDK → VK mapping: `GDK_KEY_Return` → `VK_RETURN`, `GDK_KEY_Tab` → `VK_TAB`, etc.

Modifier flag construction:
```c
lParam = FVIRTKEY;
if (shift)   lParam |= FSHIFT;
if (ctrl)    lParam |= FCONTROL;
if (alt)     lParam |= FALT;
if (super)   lParam |= FLWIN;
if (is_extended_key(keyval)) lParam |= 0x1000000;
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

After `EndDialog`, the GdkWindow can be saved in a spare pool for 100ms (500ms if
app is inactive). The next `SWELL_DialogBox` call will reuse the pooled window:
- `SW_SHOWNA` (show without focus steal) is used when reusing.
- Pooled window is destroyed by `swell_dlg_destroyspare()` when the TTL expires.

Pool size: 1 window (the most recently closed modal dialog).

---

## 11. App Startup Sequence (generic/GDK)

```
main()
  SWELL_initargs(&argc, &argv)     // GDK init: gdk_init()
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
