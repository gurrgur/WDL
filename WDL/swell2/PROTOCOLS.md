# SWELL Interaction Protocols

This document specifies the exact behavioral contracts between components:
wParam/lParam encodings for every message, call-return invariants for every
procedure type, ordering guarantees, and edge-case rules. It is the normative
companion to SPEC.md.

---

## 1. Window Procedure Protocol

### 1.1 WNDPROC

```c
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
```

- Called synchronously from `SendMessage`. Never re-entered for the same HWND
  unless the proc itself calls `SendMessage` recursively.
- Return value meaning is message-specific (see §5).
- If the window has been destroyed during processing (`m_hashaddestroy==2`),
  subsequent `SendMessage` calls to it return 0 without calling the proc.
- A WNDPROC set via `SetWindowLong(GWL_WNDPROC)` replaces the dispatch
  entirely. The old proc is returned and should typically be called via
  `CallWindowProc`.

### 1.2 DLGPROC

```c
INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
```

- Wrapped by `SwellDialogDefaultWindowProc`, which calls it and then performs
  default dialog behavior if it returns 0.
- Return 0 → default processing applies.
- Return non-zero → message handled; default processing skipped.
- Exception: `WM_CTLCOLOR*` — return value is an HBRUSH; returning 0 falls
  through to `SwellDialogDefaultWindowProc`'s default handling.
- Exception: `WM_INITDIALOG` — return value means "set focus to suggested control"
  (non-zero = yes, 0 = app controls focus manually).
- The DLGPROC is **not** called directly by `SendMessage`; the wrapper
  `SwellDialogDefaultWindowProc` is the real WNDPROC stored in `m_wndproc`.

### 1.3 SwellDialogDefaultWindowProc behavior

Called as the actual WNDPROC for dialog windows. In order:

1. If `WM_PAINT`: calls `BeginPaint`, sends `WM_CTLCOLORDLG` to dlgproc, fills
   background, calls `EndPaint`. **Then** continues to call the dlgproc for
   WM_PAINT as well (so dlgproc can draw over the background).
2. Calls the dlgproc with the message.
3. If dlgproc returned non-zero: return that value.
4. If dlgproc returned 0 and message is `WM_KEYDOWN`:
   - Top-level only (no parent):
     - `VK_ESCAPE` → send `WM_CLOSE`; if that returns 0, send
       `WM_COMMAND(IDCANCEL, 0, 0)`.
     - `VK_RETURN` → find first child button with `BS_DEFPUSHBUTTON`, send
       `WM_COMMAND(id, 0, 0)`; or `WM_COMMAND(IDOK, 0, 0)` if no default button.
   - Tab/arrow navigation: advance focused child (see §3.3).
5. If `WM_CTLCOLORSTATIC` returned 0: set text color to `label_text`, return
   default brush (so static controls always get sensible colors).
6. Fall through to `DefWindowProc`.

### 1.4 DefWindowProc behavior

Handles the following messages; all others return 0:

| Message | Action |
|---|---|
| `WM_DESTROY` | Clears `g_menubar_active` if it was this window |
| `WM_NCMOUSEMOVE` | If window has menu bar and is in drag: delegate to submenus |
| `WM_NCCALCSIZE` | If top-level with menu: adjusts `r->top += menubar_height` |
| `WM_NCPAINT` | If top-level with menu: draws the menu bar |
| `WM_NCHITTEST` | Returns `HTMENU` if in menu bar area, else `HTCLIENT` |
| `WM_RBUTTONUP` / `WM_NCRBUTTONUP` | Sends `WM_CONTEXTMENU` to window (if last rbutton down was same window) |
| `WM_NCLBUTTONDOWN` | Opens menu bar submenu on click |
| `WM_NCLBUTTONUP` | Fires menu item command on release |
| `WM_KEYDOWN` / `WM_KEYUP` | If has parent: bubble to parent. If top-level with menu: handle Alt+letter accelerators |
| `WM_CONTEXTMENU` / `WM_MOUSEWHEEL` / `WM_MOUSEHWHEEL` / `WM_SETCURSOR` | Bubble to parent if any, else return 0 |
| `WM_SETFONT` | Stores font in `hwnd->m_font` |
| `WM_GETFONT` | Returns `hwnd->m_font` or default font |
| `WM_DROPFILES` | Bubble to parent if window doesn't have `WS_EX_ACCEPTFILES` |

`WM_KEYDOWN` with no parent: returns 69 (arbitrary non-zero) to indicate
"handled by default". This prevents further bubbling.

---

## 2. Dialog Lifecycle Protocol

### 2.1 Modal dialog (SWELL_DialogBox)

Sequence:

1. `SWELL_CreateDialog` called → window created, `WM_INITDIALOG` fired.
2. All other top-level windows disabled (`EnableWindow(FALSE)`).
3. Window shown (`SW_SHOW` or `SW_SHOWNA` if reusing OS window from spare pool).
4. Run loop: `SWELL_RunMessageLoop()` + `Sleep(10)` until `EndDialog` sets
   `has_ret=true` or window is destroyed.
5. All other top-level windows re-enabled.
6. Return value = `ret` passed to `EndDialog`.

If `WM_INITDIALOG` itself calls `EndDialog` (before the run loop starts),
`SWELL_CreateDialog` still returns NULL (window already destroyed), and the
return value of `SWELL_DialogBox` comes from `s_last_dlgret`.

### 2.2 EndDialog

```
EndDialog(hwnd, ret)
```

1. Marks the modal dialog record `has_ret=true`, stores `ret`.
2. Sends `WM_DESTROY` to the window.
3. Optionally saves the OS window in a spare pool (so next modal dialog reuses
   it without flicker). Spare is kept for 100ms (500ms if app is inactive).
4. Calls `RecurseDestroyWindow`.

### 2.3 Modeless dialog (SWELL_CreateDialog)

Sequence:

1. `HWND__` allocated.
2. Style set based on `windowTypeFlags` (child vs top-level, resizable, etc.).
3. `createFunc` called → controls created via `SWELL_MakeButton` etc.
4. Title set from `SWELL_DialogResourceIndex::title`.
5. `m_dlgproc = dlgproc; m_wndproc = SwellDialogDefaultWindowProc`.
6. First focusable child found, stored as `m_focused_child`.
7. `dlgproc(hwnd, WM_INITDIALOG, (WPARAM)firstFocusChild, param)` called.
8. If WM_INITDIALOG returns non-zero: `SetFocus(firstFocusChild)`.
9. Returns HWND (or NULL if window was destroyed during WM_INITDIALOG).

For resource-ID `resid=NULL` (opaque child window): the passed `dlgproc` is
stored as the WNDPROC directly (not wrapped), and `WM_CREATE` is fired with
`lParam=param` instead of WM_INITDIALOG.

### 2.4 Window destruction sequence

`DestroyWindow(hwnd)`:

1. Guard: if `m_hashaddestroy`, return immediately (no double-destroy).
2. `SendMessage(hwnd, WM_DESTROY, 0, 0)`:
   a. Sets `m_hashaddestroy=1`.
   b. Calls wndproc with WM_DESTROY.
   c. Sends WM_DESTROY to all children (recursively).
   d. Sends WM_DESTROY to all owned windows (except modal dialogs).
   e. Clears pending message queue for this window.
   f. Kills all timers.
   g. Sets `m_wndproc=NULL`, `m_hashaddestroy=2`.
3. `RecurseDestroyWindow(hwnd)`:
   a. Recursively destroys all children and owned windows.
   b. Destroys OS window (`swell_oswindow_destroy`).
   c. Destroys menu.
   d. Removes from parent/global lists.
   e. Clears message queue and timers again (safety).
   f. Calls `hwnd->Release()`.
4. `~HWND__` sends `WM_NCDESTROY` (via the stored wndproc, if any).

---

## 3. Focus Protocol

### 3.1 Focus chain

Focus is tracked in two levels:

- `SWELL_focused_oswindow` — the OS window (top-level) that has OS focus.
- `hwnd->m_focused_child` — within each window, which child has logical focus.

`GetFocus()` walks down from the focused OS window following `m_focused_child`
pointers until it reaches a leaf.

### 3.2 SetFocus(hwnd)

1. Get `oldFoc = GetFocus()`.
2. If `oldFoc != hwnd`: send `WM_KILLFOCUS(wParam=hwnd, lParam=0)` to `oldFoc`.
3. Clear `hwnd->m_focused_child` (focus is on hwnd itself, not a descendant).
4. Walk up parent chain setting `parent->m_focused_child = child` at each level.
5. Focus the OS window containing hwnd.
6. If `hwnd != oldFoc`: send `WM_SETFOCUS(wParam=oldFoc, lParam=0)` to hwnd.

### 3.3 WM_KILLFOCUS / WM_SETFOCUS encoding

```
WM_KILLFOCUS: wParam = (WPARAM)(HWND) newFocus,   lParam = 0
WM_SETFOCUS:  wParam = (WPARAM)(HWND) oldFocus,   lParam = 0
```

### 3.4 Tab navigation (dialog default)

On `WM_KEYDOWN` with VK_TAB (when dlgproc returns 0):

- `lParam & FSHIFT` → navigate backwards.
- Iterates children of the dialog, skipping: invisible, disabled, not wantfocus.
- Wraps around.
- On navigation: calls `SetFocus(ch)` then `SWELL_OnNavigationFocus(ch)`.
- `SWELL_OnNavigationFocus`: if target is an editable Edit or combobox, sends
  `EM_SETSEL(0, -1)` to select all text.

Arrow keys (VK_LEFT/UP → backward, VK_RIGHT/DOWN → forward) also trigger
navigation when `lParam & 0xff == FVIRTKEY`.

---

## 4. Message Encoding Reference

### 4.1 Mouse messages

Applies to: `WM_LBUTTONDOWN`, `WM_LBUTTONUP`, `WM_LBUTTONDBLCLK`,
`WM_RBUTTONDOWN`, `WM_RBUTTONUP`, `WM_RBUTTONDBLCLK`,
`WM_MBUTTONDOWN`, `WM_MBUTTONUP`, `WM_MBUTTONDBLCLK`, `WM_MOUSEMOVE`.

```
wParam = mouse button and modifier key state (MK_* flags)
         NOTE: MK_ flags are NOT reliably set in SWELL; do not depend on them
lParam = MAKELPARAM(x, y) where x,y are client-area coordinates
         Extract with: GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)
         which do (int)(short)LOWORD(lParam) and (int)(short)HIWORD(lParam)
```

Non-client mouse messages (`WM_NC*`):
```
lParam = screen coordinates packed as MAKELPARAM(x, y)
wParam = hit-test code (HTCAPTION, HTHSCROLL, etc.)
```

### 4.2 WM_MOUSEWHEEL / WM_MOUSEHWHEEL

```
wParam = MAKEWPARAM(keyState, delta)
         HIWORD(wParam) = signed wheel delta (units of WHEEL_DELTA=120)
         Extract: (short)HIWORD(wParam)
lParam = screen coordinates: MAKELPARAM(x, y)
```

SWELL usage pattern observed in implementation:
```c
const int amt = ((short)HIWORD(wParam)) / 40;  // scroll by amt items
```

### 4.3 WM_KEYDOWN / WM_KEYUP / WM_CHAR / WM_SYSKEYDOWN / WM_SYSKEYUP

```
wParam = virtual key code (VK_*) for WM_KEYDOWN/UP
         character code for WM_CHAR
lParam = modifier flags (SWELL-specific encoding, NOT Win32 scan-code encoding)
         bit fields: FVIRTKEY | FSHIFT | FCONTROL | FALT | FLWIN
         Also: 0x1000000 for arrow keys, Home/End, numeric keypad Enter
```

**Important**: SWELL's `lParam` for keyboard messages is NOT the Win32
repeat-count / scan-code / extended-key encoding. It is a modifier flag word.

Keyboard messages bubble up to parent if unhandled:
- `DefWindowProc` forwards `WM_KEYDOWN`/`WM_KEYUP` to parent if hwnd has a parent.
- Dialog default proc handles navigation (Tab, Enter, Escape) before bubbling.

### 4.4 WM_COMMAND

```
wParam = MAKEWPARAM(controlID, notificationCode)
         LOWORD(wParam) = control ID
         HIWORD(wParam) = notification code (BN_CLICKED, EN_CHANGE, etc.)
lParam = (LPARAM)(HWND) control handle, or 0 for menu/accelerator commands
```

Special cases:
- Menu commands: `wParam = menuItemID`, `lParam = 0`.
- Accelerator: `wParam = MAKEWPARAM(id, 1)`, `lParam = 0`.
- Dialog default buttons: `wParam = IDOK` or `IDCANCEL`, `lParam = 0`.

Notification codes per control type:

| Control | Notification | Condition |
|---|---|---|
| Button | `BN_CLICKED = 0` | click or space/enter press |
| Edit | `EN_SETFOCUS = 0x0100` | focus gained |
| Edit | `EN_KILLFOCUS = 0x0200` | focus lost |
| Edit | `EN_CHANGE = 0x0300` | text changed |
| Static | `STN_CLICKED = 0` | clicked (only if `SS_NOTIFY`) |
| Static | `STN_DBLCLK = 1` | double-clicked (only if `SS_NOTIFY`) |
| Listbox | `LBN_SELCHANGE = 1` | selection changed |
| Listbox | `LBN_DBLCLK = 2` | item double-clicked |
| Combobox | `CBN_SELCHANGE = 1` | selection changed |
| Combobox | `CBN_EDITCHANGE = 5` | edit text changed |
| Combobox | `CBN_DROPDOWN = 7` | dropdown opened |
| Combobox | `CBN_CLOSEUP = 8` | dropdown closed |

macOS note: `SetDlgItemText` on an edit does NOT send `EN_CHANGE`.

### 4.5 WM_NOTIFY

```
wParam = control ID (same as NMHDR::idFrom)
lParam = (LPARAM)(NMHDR *) pointer to notification struct
         Actual struct is typically a superset of NMHDR (NMLISTVIEW, NMTREEVIEW, etc.)
```

The caller must cast `lParam` to the appropriate struct type based on
`((NMHDR*)lParam)->code`.

Common notification structs:

**NM_CLICK / NM_DBLCLK / NM_RCLICK** (ListView/TreeView):
```c
NMLISTVIEW or NMMOUSE at lParam
hdr.hwndFrom = control
hdr.idFrom   = control ID
hdr.code     = NM_CLICK / NM_DBLCLK / NM_RCLICK
iItem        = hit item index (-1 if none)
iSubItem     = hit subitem column index
ptAction     = client coordinates of click
```

**LVN_ITEMCHANGED**:
```c
NMLISTVIEW at lParam
iItem        = item index
iSubItem     = 0
uNewState    = new state bits
uOldState    = old state bits
uChanged     = LVIF_STATE
lParam       = item's LPARAM
```

**LVN_COLUMNCLICK**:
```c
NMLISTVIEW at lParam
iItem        = -1
iSubItem     = column index clicked
```

**LVN_GETDISPINFO** (owner data mode):
```c
NMLVDISPINFO at lParam
item.mask    = what to fill (LVIF_TEXT, LVIF_IMAGE, etc.)
item.iItem   = row index
item.iSubItem= column index
item.pszText = buffer to fill (or set to static string)
item.cchTextMax = buffer size
item.iImage  = image index to fill
```

**TVN_SELCHANGED**:
```c
NMTREEVIEW at lParam
action       = TVC_UNKNOWN / TVC_BYKEYBOARD / TVC_BYMOUSE
itemOld      = previously selected item (mask=TVIF_HANDLE|TVIF_PARAM)
itemNew      = newly selected item
```

**TVN_ITEMEXPANDING**:
```c
NMTREEVIEW at lParam
action       = TVE_EXPAND or TVE_COLLAPSE
itemNew      = item being expanded/collapsed
```
Return non-zero from WM_NOTIFY handler to prevent expand/collapse.

**TVN_BEGINDRAG**:
```c
NMTREEVIEW at lParam
itemNew.hItem = item being dragged
ptDrag        = client coordinates
```
Return value encodes destination (SWELL extension):
- `-1` = drag not possible
- `-2` = destination after last item
- `(HTREEITEM)` = will insert before that item

**NM_CUSTOMDRAW** (ListView):
```c
NMLVCUSTOMDRAW at lParam
nmcd.dwDrawStage = CDDS_PREPAINT or CDDS_ITEMPREPAINT
nmcd.dwItemSpec  = item index (for ITEMPREPAINT)
clrText          = text color to use
clrTextBk        = text background color to use
iSubItem         = subitem index
```
Return `CDRF_NOTIFYITEMDRAW` from CDDS_PREPAINT to get per-item callbacks.

**TCN_SELCHANGE** (Tab control):
```c
NMHDR at lParam
code = TCN_SELCHANGE
```

### 4.6 WM_SIZE

```
wParam = SIZE_RESTORED (0) in SWELL generic (always)
         SIZE_MINIMIZED, SIZE_MAXIMIZED possible on macOS
lParam = MAKELPARAM(new_width, new_height)  [client area size]
         Extract: LOWORD(lParam)=width, HIWORD(lParam)=height
```

Sent by `SetWindowPos` whenever size changes (reposflag & 2).

### 4.7 WM_MOVE

Not explicitly sent by SWELL generic (position changes happen via OS window
manager callback). macOS/GDK send it from their resize callbacks.

### 4.8 WM_ACTIVATE

```
wParam = MAKEWPARAM(WA_ACTIVE/WA_CLICKACTIVE/WA_INACTIVE, minimized)
         LOWORD(wParam) = WA_INACTIVE(0), WA_ACTIVE(1), WA_CLICKACTIVE(2)
         HIWORD(wParam) = minimized flag (bool)
lParam = (LPARAM)(HWND) the other window
```

### 4.9 WM_ACTIVATEAPP

```
wParam = (WPARAM)(BOOL) TRUE if activating, FALSE if deactivating
lParam = thread ID (not meaningful in SWELL, typically 0)
```

Sent by `SWELL_BroadcastMessage`. Can be suppressed by returning non-zero from
`SWELLAppMain(SWELLAPP_ACTIVATE, isActive, 0)`.

### 4.10 WM_SETFOCUS / WM_KILLFOCUS

```
WM_SETFOCUS:  wParam = (WPARAM)(HWND) window losing focus, lParam = 0
WM_KILLFOCUS: wParam = (WPARAM)(HWND) window gaining focus, lParam = 0
```

### 4.11 WM_SHOWWINDOW

```
wParam = (WPARAM)(BOOL) TRUE if being shown, FALSE if hidden
lParam = 0 (ShowWindow call) or SW_* reason code
```

### 4.12 WM_CLOSE

```
wParam = 0, lParam = 0
```

Sent by `SwellDialogDefaultWindowProc` on VK_ESCAPE before sending
`WM_COMMAND(IDCANCEL)`. If WM_CLOSE handler returns non-zero, IDCANCEL is
not sent.

### 4.13 WM_DESTROY

```
wParam = 0, lParam = 0
```

Sent once. After this, no further messages are dispatched to the window
(`m_hashaddestroy == 2` guard). Sent to children before owned windows.
Message queue for the window is cleared.

### 4.14 WM_NCDESTROY

```
wParam = 0, lParam = 0
```

Sent from `~HWND__()` destructor. This is the very last message. The window
struct may be partially freed at this point. Do not access child windows.

### 4.15 WM_NCCALCSIZE

```
wParam = FALSE (always from SWELL internal GetClientRect call)
lParam = (LPARAM)(NCCALCSIZE_PARAMS *)
         rgrc[0] = proposed client rect (modify to adjust margins)
```

Called during `GetClientRect` and `ClientToScreen`/`ScreenToClient` to
account for non-client area (e.g., menu bar height).

Standard DefWindowProc behavior: if top-level with menu, adjusts
`r->top += menubar_height`.

### 4.16 WM_NCHITTEST

```
wParam = 0
lParam = MAKELPARAM(screenX, screenY)
return: HTCLIENT, HTCAPTION, HTMENU, HTTRANSPARENT, HTVSCROLL, HTHSCROLL,
        HTBOTTOMRIGHT, HTNOWHERE, etc.
```

### 4.17 WM_NCPAINT

```
wParam = 0 (region handle; SWELL always passes 0)
lParam = 0
```

### 4.18 WM_NCMOUSEMOVE / WM_NCLBUTTONDOWN / WM_NCLBUTTONUP / WM_NCRBUTTONDOWN / WM_NCRBUTTONUP

```
wParam = hit-test code
lParam = MAKELPARAM(screenX, screenY)
```

### 4.19 WM_PAINT

```
wParam = 0, lParam = 0
```

Handler must call `BeginPaint`/`EndPaint` pair. `PAINTSTRUCT::rcPaint`
contains the dirty rectangle. Calling `EndPaint` clears the invalidated state.

`SwellDialogDefaultWindowProc` calls `BeginPaint` first, fills background with
`WM_CTLCOLORDLG` result (or `SWELL_FillDialogBackground`), then `EndPaint`,
then calls the dlgproc for WM_PAINT so the app can draw on top.

### 4.20 WM_ERASEBKGND

```
wParam = (WPARAM)(HDC) device context
lParam = 0
return: non-zero if background was erased
```

SWELL generally handles background in WM_PAINT rather than WM_ERASEBKGND.

### 4.21 WM_DRAWITEM

```
wParam = (WPARAM)(UINT) control ID
lParam = (LPARAM)(DRAWITEMSTRUCT *)
         CtlType  = ODT_BUTTON, ODT_LISTBOX, ODT_COMBOBOX, ODT_MENU
         CtlID    = control ID
         itemID   = index of item (or menu item ID)
         itemAction = ODA_DRAWENTIRE, ODA_SELECT, ODA_FOCUS
         itemState  = ODS_SELECTED, etc.
         hwndItem = HWND of control
         hDC      = HDC to draw into
         rcItem   = bounding rect of item (in HDC coordinates)
         itemData = per-item LPARAM
```

Sent to parent for: `BS_OWNERDRAW` buttons, `LBS_OWNERDRAWFIXED` listboxes.

### 4.22 WM_SETTEXT

```
wParam = 0
lParam = (LPARAM)(const char *) new text
```

Sent by `SetDlgItemText`/`SetWindowText`. Controls use this to update their
displayed text. The text is already stored in `hwnd->m_title` before this fires.

### 4.23 WM_GETTEXT / WM_GETTEXTLENGTH

Not consistently used in SWELL generic; text is accessed via `m_title`
directly. Avoid relying on WM_GETTEXT.

### 4.24 WM_SETFONT / WM_GETFONT

```
WM_SETFONT: wParam = (WPARAM)(HFONT) font or 0 for default, lParam = BOOL redraw
            DefWindowProc stores it in hwnd->m_font
WM_GETFONT: wParam = 0, lParam = 0
            DefWindowProc returns hwnd->m_font or SWELL_GetDefaultFont()
```

### 4.25 WM_HSCROLL / WM_VSCROLL

```
wParam = MAKEWPARAM(scrollCode, thumbPos)
         LOWORD(wParam) = SB_* code
         HIWORD(wParam) = thumb position (for SB_THUMBTRACK/SB_THUMBPOSITION)
lParam = (LPARAM)(HWND) scroll bar control (or 0 for window scroll bars)
```

### 4.26 WM_INITMENUPOPUP

```
wParam = (WPARAM)(HMENU) the popup menu about to be shown
lParam = MAKELPARAM(menuIndex, isSystemMenu)
         LOWORD(lParam) = zero-based index of the submenu in the parent menu
         HIWORD(lParam) = TRUE if system menu
```

Sent to the window that owns the menu just before the popup is shown.

### 4.27 WM_CONTEXTMENU

```
wParam = (WPARAM)(HWND) window on which right-click occurred
lParam = MAKELPARAM(screenX, screenY)  (screen coordinates)
         or MAKELPARAM(0xFFFF, 0xFFFF) if invoked via keyboard
```

Generated by `DefWindowProc` on `WM_RBUTTONUP` if the right button was pressed
on the same window. Bubbles to parent if unhandled.

### 4.28 WM_MOUSEACTIVATE

```
wParam = (WPARAM)(HWND) top-level parent window
lParam = MAKELPARAM(hitTestCode, mouseMessage)
         LOWORD(lParam) = HTCLIENT etc.
         HIWORD(lParam) = WM_LBUTTONDOWN etc.
return: MA_ACTIVATE(1), MA_ACTIVATEANDEAT(2), MA_NOACTIVATE(3), MA_NOACTIVATEANDEAT(4)
```

### 4.29 WM_GETMINMAXINFO

```
wParam = 0
lParam = (LPARAM)(MINMAXINFO *)
         ptMinTrackSize, ptMaxTrackSize: min/max window size when resizing
         ptMaxSize, ptMaxPosition: size/pos when maximized
```

### 4.30 WM_TIMER

```
wParam = (WPARAM)(UINT_PTR) timer ID
lParam = 0  (not the current time, unlike Win32)
```

If a `TIMERPROC` was supplied to `SetTimer`: the proc is called as
`tProc(hwnd, WM_TIMER, timerID, currentTime)` where `currentTime` is
`GetTickCount()`.

If no `TIMERPROC` (hwnd-only timer): `SendMessage(hwnd, WM_TIMER, timerID, 0)`.

### 4.31 WM_CAPTURECHANGED

```
wParam = 0
lParam = (LPARAM)(HWND) window gaining capture (or 0)
```

Sent to the window losing capture when `SetCapture` or `ReleaseCapture` is called.

### 4.32 WM_DROPFILES

```
wParam = (WPARAM)(HDROP) drop handle
         Use DragQueryFile() to enumerate, DragFinish() when done
lParam = 0
```

`DefWindowProc` bubbles this to the parent if the receiving window doesn't have
`WS_EX_ACCEPTFILES` set. Drop coordinates are adjusted (client→screen→parent client).

### 4.33 WM_COPYDATA

```
wParam = (WPARAM)(HWND) sending window
lParam = (LPARAM)(COPYDATASTRUCT *)
         dwData = application-defined ID
         cbData = size of lpData in bytes
         lpData = pointer to data
```

### 4.34 WM_STYLECHANGED

```
wParam = GWL_STYLE or GWL_EXSTYLE
lParam = (LPARAM)(STYLESTRUCT *)
         styleOld = previous style
         styleNew = new style
```

### 4.35 WM_DISPLAYCHANGE

```
wParam = bits-per-pixel
lParam = MAKELPARAM(newWidth, newHeight) [screen resolution]
```

Sent by `SWELL_BroadcastMessage`. Also triggers `InvalidateRect` on all
top-level windows.

### 4.36 WM_CTLCOLOR* messages

```
WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX / WM_CTLCOLORBTN /
WM_CTLCOLORDLG / WM_CTLCOLORSCROLLBAR / WM_CTLCOLORSTATIC:
  wParam = (WPARAM)(HDC) device context for the control
  lParam = (LPARAM)(HWND) control window handle
  return = (HBRUSH) brush for background, or 0/1 for default
```

The dialog proc may call `SetTextColor`/`SetBkColor` on the HDC before
returning the brush. If dlgproc returns 0 for WM_CTLCOLORSTATIC, the default
handler sets `label_text` color and returns a `_3dface` brush.

### 4.37 WM_INITDIALOG

```
wParam = (WPARAM)(HWND) suggested focus window (first focusable child)
lParam = (LPARAM) initialization parameter passed to DialogBox/CreateDialog
return: non-zero = set focus to wParam control
        zero     = app sets focus manually
```

### 4.38 WM_SYSCOMMAND

```
wParam = SC_* command (SC_CLOSE = 0xF060)
lParam = cursor position (MAKELPARAM(x, y)) for mouse-triggered
```

### 4.39 EM_GETSEL / EM_SETSEL

```
EM_GETSEL:  wParam = 0, lParam = 0
            return = MAKELPARAM(selStart, selEnd)
                     LOWORD = selection start, HIWORD = selection end (or -1 for end of text)

EM_SETSEL:  wParam = selStart, lParam = selEnd
            (-1 for lParam = select to end)
            return = 0
```

### 4.40 BM_SETCHECK / BM_GETCHECK

```
BM_SETCHECK: wParam = BST_UNCHECKED(0), BST_CHECKED(1), BST_INDETERMINATE(2)
             lParam = 0
BM_GETCHECK: wParam = 0, lParam = 0
             return = BST_UNCHECKED / BST_CHECKED / BST_INDETERMINATE
```

### 4.41 BM_SETIMAGE / BM_GETIMAGE

```
BM_SETIMAGE: wParam = IMAGE_BITMAP(0) or IMAGE_ICON(1)
             lParam = (LPARAM)(HBITMAP or HICON)
BM_GETIMAGE: wParam = IMAGE_BITMAP or IMAGE_ICON
             return = (LRESULT)(HBITMAP or HICON)
```

### 4.42 CB_* messages (Combobox)

All return `CB_ERR(-1)` on failure.

```
CB_ADDSTRING:        wParam=0,         lParam=(const char*) → new index
CB_INSERTSTRING:     wParam=pos,       lParam=(const char*) → new index (-1=end)
CB_DELETESTRING:     wParam=index,     lParam=0             → remaining count
CB_RESETCONTENT:     wParam=0,         lParam=0             → 0
CB_GETCOUNT:         wParam=0,         lParam=0             → count
CB_GETCURSEL:        wParam=0,         lParam=0             → index or CB_ERR
CB_SETCURSEL:        wParam=index,     lParam=0             → index or CB_ERR
CB_GETLBTEXT:        wParam=index,     lParam=(char*)buf    → string length
CB_GETLBTEXTLEN:     wParam=index,     lParam=0             → string length
CB_FINDSTRING:       wParam=startAfter,lParam=(const char*) → index or CB_ERR
CB_FINDSTRINGEXACT:  wParam=startAfter,lParam=(const char*) → index or CB_ERR
CB_GETITEMDATA:      wParam=index,     lParam=0             → LPARAM or CB_ERR
CB_SETITEMDATA:      wParam=index,     lParam=data          → data or CB_ERR
CB_INITSTORAGE:      wParam=numItems,  lParam=bytesPerItem  → 0 (no-op)
```

### 4.43 LB_* messages (Listbox)

All return `LB_ERR(-1)` on failure.

```
LB_ADDSTRING:        wParam=0,         lParam=(const char*) → new index
LB_INSERTSTRING:     wParam=pos,       lParam=(const char*) → new index
LB_DELETESTRING:     wParam=index,     lParam=0             → remaining count
LB_RESETCONTENT:     wParam=0,         lParam=0             → 0
LB_GETCOUNT:         wParam=0,         lParam=0             → count
LB_GETCURSEL:        wParam=0,         lParam=0             → index or LB_ERR
LB_SETCURSEL:        wParam=index,     lParam=0             → index or LB_ERR
LB_GETSEL:           wParam=index,     lParam=0             → selection state
LB_SETSEL:           wParam=sel,       lParam=index         → 0 or LB_ERR
LB_GETTEXT:          wParam=index,     lParam=(char*)buf    → char count
LB_GETTEXTLEN:       wParam=index,     lParam=0             → char count
LB_GETSELCOUNT:      wParam=0,         lParam=0             → selected count
LB_GETITEMDATA:      wParam=index,     lParam=0             → LPARAM or LB_ERR
LB_SETITEMDATA:      wParam=index,     lParam=data          → 0 or LB_ERR
LB_FINDSTRINGEXACT:  wParam=startAfter,lParam=(const char*) → index or LB_ERR
```

### 4.44 TBM_* messages (Trackbar)

```
TBM_GETPOS:      wParam=0,    lParam=0    → current position
TBM_SETPOS:      wParam=TRUE, lParam=pos  → 0
TBM_SETRANGE:    wParam=TRUE, lParam=MAKELONG(low, hi) → 0
TBM_SETTIC:      wParam=0,    lParam=pos  → 0 (adds tick mark)
TBM_SETSEL:      wParam=TRUE, lParam=MAKELONG(selStart, selEnd) → 0
```

### 4.45 PBM_* messages (Progress bar)

```
PBM_SETRANGE: wParam=0,   lParam=MAKELONG(low, hi) → 0
PBM_SETPOS:   wParam=pos, lParam=0                 → previous position
PBM_DELTAPOS: wParam=inc, lParam=0                 → new position
```

---

## 5. Return Value Conventions

| Message | WNDPROC return | DLGPROC return |
|---|---|---|
| `WM_CREATE` | 0 = success, -1 = fail (destroy window) | n/a (WNDPROC) |
| `WM_INITDIALOG` | n/a | non-zero = set focus to wParam |
| `WM_PAINT` | 0 | 0 or non-zero (both work) |
| `WM_ERASEBKGND` | non-zero = erased | non-zero = erased |
| `WM_CLOSE` | 0 = proceed to IDCANCEL | non-zero = suppress IDCANCEL |
| `WM_DESTROY` | 0 | 0 |
| `WM_NCDESTROY` | 0 | 0 |
| `WM_NCCALCSIZE` | 0 | 0 |
| `WM_NCHITTEST` | hit-test code | hit-test code |
| `WM_SETCURSOR` | non-zero = cursor was set | non-zero = cursor was set |
| `WM_MOUSEACTIVATE` | MA_* constant | MA_* constant |
| `WM_GETMINMAXINFO` | 0 | 0 |
| `WM_COMMAND` | 0 | 0 or non-zero |
| `WM_NOTIFY` | return value passed to control | return value passed to control |
| `TVN_ITEMEXPANDING` | non-zero = prevent expand/collapse | non-zero = prevent |
| `WM_CTLCOLOR*` | HBRUSH | HBRUSH (0/1 = use default) |
| `WM_KEYDOWN` | 0 = unhandled | 0 = default nav applies |
| `WM_TIMER` | 0 | 0 |
| `WM_SIZE` | 0 | 0 |
| `WM_MOVE` | 0 | 0 |
| `WM_DROPFILES` | 0 | 0 |
| `WM_COPYDATA` | 1 = processed | 1 = processed |
| `BM_GETCHECK` | BST_* | BST_* |
| `EM_GETSEL` | MAKELPARAM(start,end) | — |
| `WM_GETFONT` | HFONT | HFONT |
| Default (unknown) | 0 | 0 |

---

## 6. Paint Protocol

### 6.1 Normal paint cycle

1. Some code calls `InvalidateRect(hwnd, rect, eraseBk)`. This marks the
   window as needing repaint.
2. During the message loop, the OS backend detects the dirty state and
   synthesizes a `WM_PAINT` send.
3. Window proc (or dlgproc wrapper) receives `WM_PAINT`.
4. Handler calls `BeginPaint(hwnd, &ps)`:
   - Returns the HDC to paint into.
   - `ps.rcPaint` = dirty rect (may be entire client area).
   - `ps.fErase` = whether background needs erasing.
5. Handler draws into the HDC.
6. Handler calls `EndPaint(hwnd, &ps)` — clears dirty state.

### 6.2 BeginPaint / EndPaint contract

- `BeginPaint` must always be paired with `EndPaint`.
- The HDC from `BeginPaint` is only valid until `EndPaint`.
- Do not call `GetDC` inside a `BeginPaint`/`EndPaint` pair for the same window.

### 6.3 Dialog background painting

`SwellDialogDefaultWindowProc` handles WM_PAINT for dialogs:

1. `BeginPaint` → get HDC.
2. Send `WM_CTLCOLORDLG(hdc, hwnd)` to dlgproc.
   - If returns non-zero HBRUSH ≠ 1: fill `ps.rcPaint` with that brush.
   - Else: call `SWELL_FillDialogBackground(hdc, rcPaint, 0)`.
3. `EndPaint`.
4. Call dlgproc with WM_PAINT (dlgproc may draw additional content).

---

## 7. Custom Control Protocol

### 7.1 Registration

```c
SWELL_ControlCreatorProc: HWND (*)(HWND parent, const char *cname, int idx,
                                   const char *classname, int style,
                                   int x, int y, int w, int h)
```

- `cname` = human-readable label (from `CONTROL` statement).
- `classname` = Win32 class name (e.g., `"SysListView32"`).
- Return HWND if handled, NULL to pass to next creator.
- Registration order: last-registered wins (searched in reverse order).

### 7.2 Built-in classnames and their behavior

| classname | Created as |
|---|---|
| `SysListView32` | ListView control |
| `SysTreeView32` | TreeView control |
| `SysTabControl32` | Tab control |
| `msctls_trackbar32` | Trackbar |
| `msctls_progress32` | Progress bar |
| `Button` | Button (style bits determine: push/check/radio/group) |
| `Edit` | Edit field |
| `Static` | Static label |
| `ComboBox` | Combobox (style: CBS_DROPDOWNLIST or CBS_DROPDOWN) |
| `ListBox` | Listbox |
| `__SWELL_ICON` | Icon (image display) |

### 7.3 Minimum messages a custom control must handle

To be functional in a SWELL dialog, a custom control should handle:

```
WM_CREATE or (WM_INITDIALOG if acting as dialog)
WM_PAINT
WM_DESTROY (cleanup)
WM_NCDESTROY (final cleanup)
WM_SETFONT (store font for use in WM_PAINT)
WM_GETFONT (return stored font or 0)
WM_SIZE (adjust layout if needed)
```

To participate in keyboard focus:
```
WM_SETFOCUS   (show focus indicator, start caret)
WM_KILLFOCUS  (hide focus indicator, destroy caret)
WM_KEYDOWN    (handle key, return 0 to let dialog navigate)
```

To notify the parent of user interaction:
```
SendMessage(GetParent(hwnd), WM_COMMAND,
            MAKEWPARAM(hwnd->m_id, notificationCode), (LPARAM)hwnd)
```

---

## 8. Mouse Capture Protocol

### 8.1 Acquire

```c
HWND prev = SetCapture(hwnd);
```

- All subsequent mouse messages route to `hwnd` until capture is released.
- `WM_CAPTURECHANGED(wParam=0, lParam=(LPARAM)hwnd)` sent to previous capture owner.
- Returns previous capture HWND.

### 8.2 Release

```c
ReleaseCapture();
```

- Sends `WM_CAPTURECHANGED(wParam=0, lParam=0)` to the releasing window.

### 8.3 Implicit release

- `DestroyWindow` forces `ReleaseCapture` if the destroyed window holds capture.
- `SWELL_DialogBox` calls `ReleaseCapture()` before entering the modal loop.

### 8.4 Query

```c
HWND cur = GetCapture();  // NULL if no capture
```

---

## 9. Timer Protocol

### 9.1 SetTimer

```c
UINT_PTR id = SetTimer(hwnd, timerid, rateMs, timerProc);
```

Rules:
- If `hwnd != NULL`: `timerid` must be non-zero.
- If `hwnd == NULL`: `timerid` is ignored; the returned `id` is unique.
- If a timer with the same `(hwnd, timerid)` already exists: it is updated
  (not duplicated). Rate and proc are replaced.
- Minimum interval: 1ms (values < 1 clamped to 1).
- The timer fires at the specified rate but is not interrupt-driven; it fires
  during `SWELL_RunMessageLoop()`.
- `lastFire` is reset on creation; the first fire occurs after one interval.

### 9.2 Delivery

During `SWELL_RunMessageLoop`:

- For each timer where `now - lastFire >= interval`:
  - Update `lastFire = now`.
  - If `timerProc != NULL`: call `timerProc(hwnd, WM_TIMER, timerid, now)`.
  - Else: `SendMessage(hwnd, WM_TIMER, timerid, 0)`.

### 9.3 KillTimer

```c
KillTimer(hwnd, timerid);       // kill one specific timer
KillTimer(hwnd, (UINT_PTR)-1); // kill all timers for hwnd
```

A timer currently being processed (`refcnt > 0`) is marked for deletion and
freed after the callback returns. Do not re-use the timer ID immediately after
killing from within a timer callback.

---

## 10. PostMessage Queue Protocol

### 10.1 Enqueue

```c
PostMessage(hwnd, msg, wParam, lParam)
```

- Thread-safe (mutex-protected).
- Max queue depth: 1024 messages. If full: oldest message is discarded.
- Messages to a destroyed window are silently dropped at dequeue time.

### 10.2 Dequeue

`SWELL_MessageQueue_Flush()`:

- Dequeues up to `current_size` messages (snapshot at flush time).
- Calls `SendMessage` for each.
- Must be called from main thread only.
- Called automatically by `SWELL_RunMessageLoop`.

`SWELL_MessageQueue_Clear(hwnd)`:

- Removes all queued messages for `hwnd` (or all messages if hwnd=NULL).
- Called automatically on `WM_DESTROY`.

---

## 11. Menu Protocol

### 11.1 TrackPopupMenu flow

```c
int cmd = TrackPopupMenu(hMenu, flags, x, y, 0, hwnd, NULL);
```

With `TPM_RETURNCMD`:
- Runs the menu loop synchronously (blocks).
- Returns the selected item ID, or 0 if cancelled.
- No WM_COMMAND is sent.

Without `TPM_RETURNCMD`:
- Runs menu loop.
- On selection: `SendMessage(hwnd, WM_COMMAND, itemID, 0)`.
- On no selection: no WM_COMMAND.

With `TPM_NONOTIFY`:
- No WM_COMMAND even without TPM_RETURNCMD.
- Returns the item ID regardless (same as TPM_RETURNCMD effect).

### 11.2 WM_INITMENUPOPUP timing

Sent to the owning window just before each submenu opens. This is the correct
place to enable/disable/check menu items dynamically.

### 11.3 Menu bar interaction

The menu bar is drawn in `WM_NCPAINT`. Height = `g_swell_ctheme.menubar_height`.
Client area is adjusted by `WM_NCCALCSIZE` (top + menubar_height).

Clicking the menu bar area returns `HTMENU` from `WM_NCHITTEST`. `DefWindowProc`
handles `WM_NCLBUTTONDOWN` by calling `runMenuBar`, which runs `TrackPopupMenu`
for each submenu in sequence as the user drags.

### 11.4 HMENU ownership

- `CreatePopupMenu()` → caller owns, must call `DestroyMenu`.
- `SetMenu(hwnd, menu)` → window takes ownership; destroyed with the window.
- `GetSubMenu` returns a reference (not a copy); do not destroy it.
- `SWELL_DuplicateMenu` → caller owns the copy.
- Menu items with `MF_POPUP` own their submenus.

---

## 12. Scroll Bar Protocol

SWELL does not implement standalone scroll bar controls or `SetScrollInfo`/
`GetScrollInfo` as of the audited codebase. Scrolling is implemented per-control
(listviews, editboxes, treeviews each implement their own scroll logic internally).

The window manager sends `WM_HSCROLL`/`WM_VSCROLL` to the parent window in
response to scroll bar interaction, with `LOWORD(wParam)` = `SB_*` code and
`HIWORD(wParam)` = thumb position (for `SB_THUMBTRACK`/`SB_THUMBPOSITION`).

---

## 13. WM_NCCALCSIZE Protocol for GetClientRect

`GetClientRect` internally sends `WM_NCCALCSIZE(FALSE, &params)`:

```c
NCCALCSIZE_PARAMS tr = {{ {0, 0, width, height} }};
SendMessage(hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&tr);
// result: client width  = tr.rgrc[0].right  - tr.rgrc[0].left
//         client height = tr.rgrc[0].bottom - tr.rgrc[0].top
```

`ScreenToClient`/`ClientToScreen` also call `WM_NCCALCSIZE` on each window in
the parent chain to accumulate coordinate offsets.

---

## 14. Coordinate System Protocols

### 14.1 Generic/GDK

Standard top-left origin. `r.bottom > r.top` always. Client and screen coords
are straightforward.

### 14.2 macOS (Cocoa)

Y-axis is flipped in some contexts. `GetWindowRect` may return `r.bottom < r.top`.
`SetWindowPos` and all SWELL functions accept negative heights (abs value used).

Pattern for safe coordinate conversion:
```c
RECT r;
GetWindowRect(hwnd, &r);
ScreenToClient(other, (LPPOINT)&r);           // convert top-left
ScreenToClient(other, ((LPPOINT)&r) + 1);     // convert bottom-right
// r may have bottom < top on macOS; both SWELL and caller must tolerate
```

### 14.3 ListView/TreeView coordinates (macOS Cocoa)

`ListView_GetItemRect`, `ListView_GetSubItemRect` return absolute (unscrolled)
coordinates, not relative to the visible area. To convert to screen coords:
```c
ClientToScreen(listview, (LPPOINT)&r.left);    // adjusts for scroll
```

---

## 15. Drag and Drop Protocol

### 15.1 WM_DROPFILES delivery

Source calls `SWELL_InitiateDragDrop` or `SWELL_InitiateDragDropOfFileList`.

Receiver gets `WM_DROPFILES(wParam=(HDROP), lParam=0)`:

```c
case WM_DROPFILES:
{
  HDROP hDrop = (HDROP)wParam;
  UINT n = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0); // get count
  for (UINT i = 0; i < n; i++) {
    char path[MAX_PATH];
    DragQueryFile(hDrop, i, path, MAX_PATH);
    // process path
  }
  DragFinish(hDrop);  // no-op but required for Win32 compat
  return 0;
}
```

`DROPFILES` structure in the HDROP:
```c
typedef struct _DROPFILES {
  DWORD pFiles;   // offset from start of struct to the file list
  POINT pt;       // drop point (client coords)
  BOOL fNC;       // TRUE if pt is in screen coords (non-client area)
  BOOL fWide;     // character width flag
} DROPFILES;
// file list starts at ((char*)df + df->pFiles)
// null-separated, double-null terminated UTF-8 paths
```

`DragQueryPoint(hDrop, &pt)` returns `!fNC` (TRUE if in client area) and
fills `pt` with the drop point.

### 15.2 Default bubbling

`DefWindowProc` on `WM_DROPFILES` will bubble to parent if:
- Window does not have `WS_EX_ACCEPTFILES`.
- Coordinates are adjusted (client→screen→parent client) before bubbling.

---

## 16. Threading Contracts

- **All window functions**: main thread only, unless explicitly stated.
- **`PostMessage`**: thread-safe (may be called from any thread).
- **`GetTickCount`**: thread-safe.
- **INI functions**: thread-safe and inter-process-safe via file locking.
- **`GlobalAlloc`/`GlobalLock`/`GlobalUnlock`/`GlobalFree`**: not thread-safe;
  call from main thread only (or under external lock).
- **Timer callbacks**: always on main thread.
- **`SWELL_MessageQueue_Flush`**: main thread only.
- **`SendMessage`**: must only be called from the creating thread of the window.
  Cross-thread use may crash or deadlock.

---

## 17. Window Enabled State Protocol

`EnableWindow(hwnd, FALSE)`:
- Clears `hwnd->m_enabled`.
- If this window is the focused child of its parent, clears parent's
  `m_focused_child`.
- Invalidates the window (forces visual update to show disabled appearance).
- Keyboard input is not routed to disabled windows.

`IsWindowEnabled(hwnd)`:
- Returns TRUE only if hwnd AND all ancestors are enabled.
- Walks up the parent chain.

---

## 18. DLL/Module Entry Protocol

```c
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved);
```

Called with:
- `DLL_PROCESS_ATTACH (1)`: module loaded. `lpvReserved` = SWELLAPI_GetFunc
  function pointer (for `SWELL_PROVIDED_BY_APP` mode).
- `DLL_PROCESS_DETACH (0)`: module unloaded.

In `SWELL_PROVIDED_BY_APP` mode, the app calls:
```c
typedef void *(*SWELLAPI_GetFunc_t)(const char *name);
SWELLAPI_GetFunc_t getfunc = (SWELLAPI_GetFunc_t)lpvReserved;
```
to resolve each SWELL function pointer by name before using the API.

---

## 19. Accelerator Key Encoding in lParam (WM_KEYDOWN context)

SWELL does not use Win32 scan-code lParam for WM_KEYDOWN. Instead, lParam
carries modifier flags:

```
lParam bit fields (SWELL-specific):
  bit 0      = FVIRTKEY (1)       — wParam is a VK_ code, not a character
  bits 1-2   = reserved
  bit 2      = FSHIFT   (0x04)
  bit 3      = FCONTROL (0x08)
  bit 4      = FALT     (0x10)
  bit 5      = FLWIN    (0x20)    — Windows/Super key
  bit 24     = 0x1000000          — arrow keys, Home, End, numpad Enter
```

The dialog default proc checks:
```c
if (wParam == VK_TAB && (lParam & ~FSHIFT) == FVIRTKEY)  // pure Tab or Shift+Tab
if ((lParam & 0xff) == FVIRTKEY)  // bare virtual key (no modifiers except FVIRTKEY)
```

The menu bar keyboard handler checks:
```c
lParam == (FVIRTKEY | FALT)  // Alt+letter for menu access
```

---

*End of SWELL Protocols Specification*
