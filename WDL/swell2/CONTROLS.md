# SWELL Built-in Control Specifications

This document specifies the internal state, rendering, and message handling for
all built-in SWELL control types. Each control is a `HWND__` with a specific
WNDPROC and `m_private_data` pointer.

Text content is stored in `hwnd->m_title` (a `WDL_FastString`). All text access
goes through this field; `GetWindowText`/`SetWindowText` update it.

---

## 1. Button (`buttonWindowProc`)

**classname**: `"Button"`  
**created by**: `SWELL_MakeButton`, `SWELL_MakeCheckBox`, or `SWELL_MakeControl("Button", ...)`

### State

```c
struct buttonWindowState {
  HICON bitmap;       // image set via BM_SETIMAGE (IMAGE_BITMAP or IMAGE_ICON)
  int   bitmap_mode;  // IMAGE_BITMAP or IMAGE_ICON
  int   state;        // check state: BST_UNCHECKED=0, BST_CHECKED=1, BST_INDETERMINATE=2
};
// stored in hwnd->m_private_data, allocated on WM_CREATE, freed on WM_NCDESTROY
```

### Style interpretation

The style bits in `hwnd->m_style` determine behavior:

| Style bits | Behavior |
|---|---|
| `BS_DEFPUSHBUTTON` | default push button (renders with highlight) |
| `BS_PUSHBUTTON` | normal push button |
| `BS_AUTOCHECKBOX` | checkbox; clicking toggles state between 0 and 1 |
| `BS_AUTO3STATE` | 3-state checkbox; clicking cycles 0→1→2→0 |
| `BS_AUTORADIOBUTTON` | radio; clicking sets self to 1, siblings with same group to 0 |
| `BS_OWNERDRAW` | sends WM_DRAWITEM to parent for custom rendering |
| `BS_GROUPBOX` | draws a named box border; no click behavior |
| `BS_BITMAP` | renders stored bitmap instead of label |

### Message handling

| Message | Action |
|---|---|
| `WM_CREATE` | allocate `buttonWindowState`; set `m_wantfocus = true` |
| `WM_NCDESTROY` | free `buttonWindowState` |
| `WM_LBUTTONDOWN` | `SetFocus(hwnd)`; begin press animation |
| `WM_LBUTTONUP` | if still in bounds: toggle state (auto styles); send `WM_COMMAND(BN_CLICKED)` to parent |
| `WM_KEYDOWN` `VK_SPACE`/`VK_RETURN` | same as LButtonUp for push buttons |
| `BM_SETCHECK` | `state->state = wParam` |
| `BM_GETCHECK` | return `state->state` |
| `BM_SETIMAGE` | store in `state->bitmap` / `state->bitmap_mode`; `InvalidateRect` |
| `BM_GETIMAGE` | return `state->bitmap` |
| `WM_PAINT` | see §1.1 |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | `InvalidateRect` (redraw focus ring) |

### Radio button group behavior

On `WM_LBUTTONUP` for `BS_AUTORADIOBUTTON`:
1. Set own state to `BST_CHECKED`.
2. Iterate siblings (same parent, same `WS_GROUP` group — forward until hitting
   another WS_GROUP or end): set each `BS_AUTORADIOBUTTON` sibling's state to
   `BST_UNCHECKED` and `InvalidateRect`.
3. Send `WM_COMMAND(BN_CLICKED)` to parent.

### Rendering (WM_PAINT)

Push button:
- Draw themed button background (pressed/default/normal).
- If `BS_BITMAP` and bitmap set: draw bitmap centered.
- Else: draw label text from `hwnd->m_title`, centered, with WM_CTLCOLORBTN colors.

Checkbox / radio:
- Draw check indicator box (or radio circle) with check/partial mark based on state.
- Draw label text to the right (or left if `BS_LEFTTEXT`).

Group box:
- Draw a rectangle border with the title embedded in the top border.

Disabled: text drawn in `button_text_disabled` theme color.

---

## 2. Edit (`editWindowProc`)

**classname**: `"Edit"`  
**created by**: `SWELL_MakeEditField`

### State

```c
struct __SWELL_editControlState {
  int cursor_pos;          // cursor position in Unicode character units (not bytes)
  int sel1, sel2;          // selection range (-1,-1 = no selection); sel1 < sel2 always
  int cursor_state;        // 0=cursor invisible, 1=cursor visible (toggled by timer)
  int cursor_timer;        // timer ID for cursor blink
  int scroll_x, scroll_y;  // scroll offsets (pixels for single-line, rows for multi)
  int max_height;          // multiline: cached total text height
  int max_width;           // cached maximum line width
  // cache for line-break positions (multiline word-wrap):
  int cache_linelen_w;     // width at which linelen cache was computed
  int cache_linelen_strlen;// strlen at which cache was computed
  WDL_TypedBuf<int> cache_linelen_bytes; // byte length of each wrapped line
  bool m_disable_contextmenu;
};
// stored in hwnd->m_private_data
// text stored in hwnd->m_title (WDL_FastString), UTF-8
```

### Style flags

| Flag | Effect |
|---|---|
| `ES_MULTILINE` | multi-line; Enter inserts newline unless `ES_WANTRETURN` not set |
| `ES_PASSWORD` | text displayed as `*`; EM_GETSEL/EM_SETSEL still work |
| `ES_READONLY` | no editing; cursor still visible |
| `ES_AUTOHSCROLL` | horizontal scroll (single-line) |
| `ES_NUMBER` | accept only digits |
| `ES_WANTRETURN` | Enter inserts newline even in dialog |
| `ES_NOHIDESEL` | keep selection visible when not focused |

### Character model

- Text is stored as UTF-8 in `hwnd->m_title`.
- `cursor_pos`, `sel1`, `sel2` are Unicode character indices (not byte offsets).
- Byte offset conversion: `WDL_utf8_charpos_to_bytepos(text, charpos)`.

### Cursor blinking

On `WM_SETFOCUS`:
- Start `SetTimer(hwnd, cursor_timer, 500, NULL)`.
- Set `cursor_state = 1`.

On `WM_TIMER(cursor_timer)`:
- Toggle `cursor_state ^= 1`; `InvalidateRect`.

On `WM_KILLFOCUS`:
- `KillTimer(hwnd, cursor_timer)`; `cursor_state = 0`; `InvalidateRect`.

### Message handling

| Message | Action |
|---|---|
| `WM_CREATE` | allocate `__SWELL_editControlState`; set `m_wantfocus=true` |
| `WM_NCDESTROY` | free state |
| `WM_SETTEXT` | update `m_title`; reset sel/cursor; `EN_CHANGE` to parent (generic — NOT on macOS) |
| `WM_GETTEXT` | copy `m_title` to lParam buffer, truncated to wParam chars |
| `WM_GETTEXTLENGTH` | return `m_title.GetLength()` (bytes, not chars) |
| `EM_GETSEL` | `MAKELPARAM(sel1==-1?cursor_pos:min(sel1,sel2), sel1==-1?cursor_pos:max(sel1,sel2))` |
| `EM_SETSEL` | set sel1=wParam, sel2=lParam; clamp to text length; scroll to show cursor |
| `EM_SETPASSWORDCHAR` | set/clear ES_PASSWORD style character |
| `EM_REPLACESEL` | replace selected text with (const char*)lParam; send EN_CHANGE |
| `WM_KEYDOWN` | handle navigation/editing; send EN_CHANGE on text modification |
| `WM_CHAR` | insert character (if not control char); send EN_CHANGE |
| `WM_LBUTTONDOWN` | `SetFocus`; set cursor to click position; begin selection |
| `WM_MOUSEMOVE` (with LButton) | extend selection |
| `WM_RBUTTONUP` | show context menu (cut/copy/paste/select all) unless disabled |
| `WM_PAINT` | see §2.1 |
| `WM_SETFOCUS` | start cursor blink; send EN_SETFOCUS to parent |
| `WM_KILLFOCUS` | stop cursor blink; send EN_KILLFOCUS to parent |
| `WM_COPY` (Ctrl+C) | copy selection to clipboard |
| `WM_CUT` (Ctrl+X) | copy selection, delete it |
| `WM_PASTE` (Ctrl+V) | paste clipboard text at cursor |

### Rendering (WM_PAINT)

1. Fill background with `WM_CTLCOLOREDIT` result (or theme `edit_bg`).
2. Draw visible text lines, scrolled by `scroll_x`/`scroll_y`.
3. Draw selection highlight: `edit_bg_sel` / `edit_text_sel` colors.
4. Draw cursor (1px line between characters) if `cursor_state==1` and focused.
5. Multi-line: draw with word-wrap if client width < `max_width`.

---

## 3. Static Label (`labelWindowProc`)

**classname**: `"Static"`  
**created by**: `SWELL_MakeLabel`

### State

No `m_private_data`. Text in `hwnd->m_title`.  
`hwnd->m_wantfocus = false`.

### Style flags

| Flag | Effect |
|---|---|
| `SS_LEFT` (0) | left-aligned text |
| `SS_CENTER` (1) | center-aligned |
| `SS_RIGHT` (2) | right-aligned |
| `SS_BLACKRECT` | filled black rectangle |
| `SS_ETCHEDHORZ` / `SS_ETCHEDVERT` / `SS_ETCHEDFRAME` | etched borders |
| `SS_NOTIFY` | sends `STN_CLICKED` / `STN_DBLCLK` to parent on click |
| `SS_NOPREFIX` | `&` not treated as accelerator underline |

### Message handling

| Message | Action |
|---|---|
| `WM_PAINT` | draw text with `SWELL_DrawText`, alignment from SS_* |
| `WM_SETTEXT` | `m_title = text`; `InvalidateRect` |
| `WM_LBUTTONDOWN` | if SS_NOTIFY: send `WM_COMMAND(STN_CLICKED)` to parent |
| `WM_LBUTTONDBLCLK` | if SS_NOTIFY: send `WM_COMMAND(STN_DBLCLK)` to parent |

### Rendering

- No selection, no focus indicator.
- Colors from `WM_CTLCOLORSTATIC` result (or theme `label_text` / dialog background).
- `DT_*` alignment from SS_* flag.

---

## 4. Listbox (shared with ListView, `listViewState`)

**classname**: `"ListBox"`  
**created by**: `SWELL_MakeListBox`

Internally uses the same `listViewState` as ListView, with `m_is_listbox=true`.
A listbox has no column headers and one implicit column.

### State

```c
// shares listViewState (see §5), constructed with m_is_listbox=true
// m_owner_data_size = -1 (always uses m_data list, never owner-data mode)
```

### Message handling

Listbox handles `LB_*` messages via the ListView WNDPROC, which detects `m_is_listbox`:

| Message | Action |
|---|---|
| `LB_ADDSTRING` | `m_data.Add(new SWELL_ListView_Row(text))` |
| `LB_INSERTSTRING` | insert at position |
| `LB_DELETESTRING` | remove at index; adjust m_selitem |
| `LB_RESETCONTENT` | clear all rows |
| `LB_GETCOUNT` | `m_data.GetSize()` |
| `LB_GETCURSEL` | `m_selitem` (single-sel); -1 if none |
| `LB_SETCURSEL` | set `m_selitem`; `InvalidateRect` |
| `LB_GETSEL` | for multi-sel: test row selection bit |
| `LB_SETSEL` | for multi-sel: set row selection bit |
| `LB_GETTEXT` | copy row text to lParam buffer |
| `LB_GETTEXTLEN` | text length of row |
| `LB_GETITEMDATA` | row's `m_param` |
| `LB_SETITEMDATA` | set row's `m_param` |
| `LB_FINDSTRINGEXACT` | linear search for exact match, case-insensitive |
| `LB_GETSELCOUNT` | count of selected rows (multi-sel) |

Selection notification: sends `WM_COMMAND(LBN_SELCHANGE)` to parent on selection change.  
Double-click: sends `WM_COMMAND(LBN_DBLCLK)`.

---

## 5. ListView (`listViewWindowProc`)

**classname**: `"SysListView32"`  
**created by**: `SWELL_MakeControl("SysListView32", ...)`

### State

```c
struct listViewState {
  WDL_PtrList<SWELL_ListView_Row> m_data;   // rows (used unless m_owner_data_size >= 0)
  WDL_TypedBuf<SWELL_ListView_Col> m_cols;  // columns (display order, via col_index mapping)

  int m_owner_data_size;  // -1 = m_data used; >= 0 = owner-data mode with this count
  int m_selitem;          // single-sel: selected row; multi-sel: focused row
  bool m_is_multisel;     // true if LVS_SINGLESEL not set
  bool m_is_listbox;      // true when hosting a ListBox

  int m_scroll_x, m_scroll_y;       // scroll position (pixels)
  int m_last_row_height;             // row height (pixels) — computed from font metrics
  ListViewCapMode m_capmode_state;   // current mouse capture mode
  int m_capmode_data1, m_capmode_data2;

  HIMAGELIST m_status_imagelist;
  int m_status_imagelist_type;       // LVSIL_STATE or LVSIL_SMALL

  int m_extended_style;
  int m_fastclick_mask;              // bitmask of columns that get immediate click notify

  int m_color_bg, m_color_bg_sel, m_color_bg_sel_inactive;
  int m_color_text, m_color_text_sel, m_color_text_sel_inactive;
  int m_color_grid;
  int m_color_extras[4];             // per-focus-state overrides
};
// stored in hwnd->m_private_data
```

```c
class SWELL_ListView_Row {
  WDL_TypedBuf<SWELL_ListView_Rec> m_cols;  // one rec per subitem
  LPARAM m_param;   // user data (LVIF_PARAM)
  int    m_tmp;     // selection mask (bit 1 = selected in multi-sel)
};

struct SWELL_ListView_Rec {
  char *txt;        // heap-allocated subitem text; NULL if not set
  int   image_idx;  // image list index
};

struct SWELL_ListView_Col {
  char *name;        // header text
  int   xwid;        // column width (pixels)
  int   sortindicator; // 0=none, 1=ascending, 2=descending
  int   col_index;   // logical column index (before column reorder)
  int   fmt;         // LVCFMT_LEFT/RIGHT/CENTER
};
```

### Column order vs. logical index

`m_cols` stores columns in **display order** (left to right).  
`col_index` is the **logical** index (stable across reordering).  
`GetColumnByIndex(logicalIdx)` searches for the entry with matching `col_index`.  
`GetColumnIndex(displayIdx)` returns `m_cols[displayIdx].col_index`.

### Owner data mode

When `LVS_OWNERDATA` is set:
- `m_owner_data_size` is the item count; `m_data` is unused.
- To fetch text/state, sends `LVN_GETDISPINFO` (as `WM_NOTIFY`) to parent.
- Multi-selection state stored in `m_owner_multisel_state` (bit array).

### Scrolling

`sanitizeScroll(hwnd)` clamps `m_scroll_x`/`m_scroll_y` to valid ranges:
- Max `m_scroll_x = getTotalWidth() - clientWidth`.
- Max `m_scroll_y = rowHeight * numItems - visibleHeight`.

Column header height = `m_last_row_height + LISTVIEW_HDR_YMARGIN` (2px) when
column headers are visible (LVS_REPORT, not LVS_NOCOLUMNHEADER).

### Selection

Single-sel (`LVS_SINGLESEL`): `m_selitem` = selected row; all others unselected.  
Multi-sel: `m_selitem` = focused row; `m_tmp & 1` on each row = whether selected;
or for owner-data, `m_owner_multisel_state` bit array.

### Mouse capture modes

```c
enum ListViewCapMode {
  LISTVIEW_CAP_NONE,
  LISTVIEW_CAP_XSCROLL,   // scrollbar drag; data1=grab xpos
  LISTVIEW_CAP_YSCROLL,   // scrollbar drag; data1=grab ypos
  LISTVIEW_CAP_DRAG,      // row drag; data1=row, data2=column
  LISTVIEW_CAP_COLRESIZE, // header column resize; data1=col, data2=xoffset
  LISTVIEW_CAP_COLCLICK,  // header click (may become COLREORDER)
  LISTVIEW_CAP_COLREORDER,
};
```

### Rendering (WM_PAINT)

1. Fill background with `m_color_bg`.
2. Draw column headers (if visible): text + sort indicator arrows.
3. For each visible row (from `m_scroll_y / rowHeight` to `(m_scroll_y + height) / rowHeight`):
   - Fill selection background (`m_color_bg_sel` / `m_color_bg_sel_inactive`).
   - Draw state image from `m_status_imagelist` if set.
   - For each column (clipped by `m_scroll_x`): draw subitem text.
   - If `NM_CUSTOMDRAW` enabled: send `WM_NOTIFY(NM_CUSTOMDRAW, CDDS_ITEMPREPAINT)` before each row.
4. Draw grid lines if `LVS_EX_GRIDLINES`.
5. Draw scroll bars if content exceeds view.

Coordinates: all internal row calculations use `m_scroll_y` as pixel offset from top.

---

## 6. TreeView (`treeViewWindowProc`)

**classname**: `"SysTreeView32"`  
**created by**: `SWELL_MakeControl("SysTreeView32", ...)`

### State

```c
struct treeViewState {
  HTREEITEM__ m_root;   // sentinel root node (always expanded); children are the top-level items
  HTREEITEM   m_sel;    // currently selected item (NULL if none)
  int m_last_row_height;
  int m_scroll_x, m_scroll_y;
  int m_capmode;        // 0=none, 1=scrollbar, 2=item drag
};

struct HTREEITEM__ {
  WDL_FastString m_value;    // item text
  LPARAM         m_param;    // user LPARAM
  int            m_state;    // TVIS_* flags (selected, expanded, bold, etc.)
  bool           m_haschildren; // whether to show expand arrow
  int            m_image, m_selimage; // image list indices
  WDL_PtrList<HTREEITEM__> m_children;
  // no parent pointer — parent lookup done by traversal
};
```

### Item traversal

Items are stored in a tree rooted at `m_root`. Traversal is recursive.
`findItem(target, &parOut, &idxOut)`:
- Recursively searches the subtree.
- If found: sets `*parOut = parent` (NULL for top-level), `*idxOut = sibling index`.

Visible-item enumeration (for hit-test, scroll bounds): depth-first, skipping
unexpanded subtrees.

### Rendering (WM_PAINT)

For each visible item (depth-first, respecting expansion):
- Indent by `depth * indent_pixels`.
- Draw expand/collapse arrow if `m_haschildren`.
- Draw state image from imagelist.
- Draw item text with selection highlight.
- Draw connecting lines if style permits.

Scroll: `m_scroll_y` in pixel rows.

### Drag protocol

On item drag (LButtonDown + drag threshold):
1. Send `WM_NOTIFY(TVN_BEGINDRAG)` to parent with `itemNew.hItem = draggedItem`.
2. If parent returns -1: drag not possible.
3. Otherwise parent should call `SetCapture`, then handle `WM_MOUSEMOVE` to show
   drag indicator and determine drop target.
4. On `WM_LBUTTONUP` with capture: parent inserts/moves item, calls `ReleaseCapture`.

Return value from TVN_BEGINDRAG (SWELL extension):
- `-1` = drag not possible
- `-2` = drop at end of list
- `(HTREEITEM)` = insert before this item

---

## 7. ComboBox (`comboWindowProc`)

**classname**: `"ComboBox"`  
**created by**: `SWELL_MakeCombo`

### State

```c
class __SWELL_ComboBoxInternalState {
  int selidx;        // selected item index (-1 = none)
  WDL_PtrList_DeleteOnDestroy<__SWELL_ComboBoxInternalState_rec> items;
  __SWELL_editControlState editstate;  // for CBS_DROPDOWN edit portion
};

struct __SWELL_ComboBoxInternalState_rec {
  char   *desc;   // strdup'd item text
  LPARAM  parm;   // per-item data
};
```

### Style variants

| Style | Behavior |
|---|---|
| `CBS_DROPDOWNLIST` | no edit; display selected item text read-only |
| `CBS_DROPDOWN` | editable text field + dropdown button |
| `CBS_SORT` | items sorted alphabetically on insert |

### Rendering

- Draws a text area (selected item text or edit field) + a dropdown button arrow.
- Dropdown arrow width = 16px (constant `buttonwid = 16`).

### Dropdown

On LButtonDown in arrow area or Enter: open dropdown list.  
The dropdown is a separate popup window (`HWND__` with its own WNDPROC) showing
the items list.  
On item select: update `selidx`, call `SetWindowText(hwnd, text)`, send `CBN_SELCHANGE`.

### Message handling

`CB_*` messages routed directly in the WNDPROC; no subclassing needed.

Notable behaviors:
- `CB_ADDSTRING` with `CBS_SORT`: binary-inserts using `strcmp` comparator.
- `CB_SETCURSEL(-1)`: deselects; clears display text.
- `CB_RESETCONTENT`: clears all items; `selidx = -1`.

---

## 8. Tab Control (`tabControlWindowProc`)

**classname**: `"SysTabControl32"`  
**created by**: `SWELL_MakeControl("SysTabControl32", ...)`

### State

```c
struct tabControlState {
  int m_curtab;                 // currently selected tab index
  WDL_PtrList<char> m_tabs;     // tab labels (strdup'd)
};
```

### Tab bar geometry

- Tab bar height: `TABCONTROL_HEIGHT = SWELL_UI_SCALE(20)`.
- Tab width: text width + `xpad` (4px) on each side, with `xdiv` (6px) between tabs.
- The tab bar occupies the top `TABCONTROL_HEIGHT` pixels of the control.

### Message handling

| Message | Action |
|---|---|
| `TCM_INSERTITEM` | add tab label to `m_tabs`; `InvalidateRect` |
| `TCM_DELETEITEM` | remove tab; adjust `m_curtab`; `InvalidateRect` |
| `TCM_GETCURSEL` | return `m_curtab` |
| `TCM_SETCURSEL` | `m_curtab = wParam`; `InvalidateRect`; send `TCN_SELCHANGE` |
| `TCM_GETITEMCOUNT` | return `m_tabs.GetSize()` |
| `TCM_ADJUSTRECT` | subtract/add tab bar height from/to rect |
| `WM_LBUTTONDOWN` | hit-test which tab; call `TCM_SETCURSEL` if different |
| `WM_KEYDOWN` | VK_LEFT/RIGHT: navigate tabs |
| `WM_PAINT` | see §8.1 |

### Rendering (WM_PAINT)

1. Draw tab bar background.
2. For each tab: draw rounded tab shape; selected tab extends below tab bar boundary.
3. Draw tab text.
4. Draw control border below tab bar (client area outline).

---

## 9. Trackbar (`trackbarWindowProc`)

**classname**: `"msctls_trackbar32"`  
**created by**: `SWELL_MakeControl("msctls_trackbar32", ...)`

### State

```c
// m_private_data points to int[3]: {position, rangeMin, rangeMax}
// initialized as: {0, 0, 10}  (no separate struct)
```

After WM_CREATE: `hwnd->m_private_data = (INT_PTR)calloc(3, sizeof(int))`.

### Message handling

| Message | Action |
|---|---|
| `TBM_SETPOS` | `p[0] = lParam`; `InvalidateRect` |
| `TBM_GETPOS` | return `p[0]` |
| `TBM_SETRANGE` | `p[1]=LOWORD(lParam)`, `p[2]=HIWORD(lParam)`; `InvalidateRect` |
| `TBM_SETTIC` | add tick mark (visual only; tick positions are tracked) |
| `TBM_SETSEL` | set selection range (visual highlight on slider track) |
| `WM_LBUTTONDOWN` | set capture; compute position from click X; send `WM_HSCROLL(SB_THUMBTRACK)` |
| `WM_MOUSEMOVE` (captured) | update position; `WM_HSCROLL(SB_THUMBTRACK)` |
| `WM_LBUTTONUP` | release capture; `WM_HSCROLL(SB_ENDSCROLL)` |
| `WM_KEYDOWN` | arrow keys adjust position by 1; PgUp/PgDn by 10; send `WM_HSCROLL` |
| `WM_PAINT` | draw track + thumb |

Position clamped to `[p[1], p[2]]`. All changes send `WM_HSCROLL(SB_THUMBTRACK,
newpos, hwnd)` to parent (not `SB_THUMBPOSITION` until mouse release).

### Rendering (WM_PAINT)

1. Draw horizontal track groove (thin raised line).
2. Draw tick marks.
3. Draw selection highlight if set.
4. Draw thumb (diamond or rect shape) at position scaled along track.

---

## 10. Progress Bar (`progressWindowProc`)

**classname**: `"msctls_progress32"`  
**created by**: `SWELL_MakeControl("msctls_progress32", ...)`

### State

```c
// m_private_data points to int[3]: {position, rangeMin, rangeMax}
// initialized as: {0, 0, 100}
```

### Message handling

| Message | Action |
|---|---|
| `PBM_SETRANGE` | `p[1]=LOWORD(lParam)`, `p[2]=HIWORD(lParam)`; `InvalidateRect` |
| `PBM_SETPOS` | `p[0] = wParam`; `InvalidateRect`; return old pos |
| `PBM_DELTAPOS` | `p[0] += wParam`; `InvalidateRect`; return new pos |

### Rendering (WM_PAINT)

1. Fill background with theme `progress` color (dark).
2. Fill foreground bar proportional to `(pos - min) / (max - min)` of client width.
3. Border.

No user interaction. `m_wantfocus = false`.

---

## 11. Common Behaviors Across All Controls

### Focus eligibility

Controls set `hwnd->m_wantfocus`:
- `true` (default): can receive keyboard focus via Tab navigation.
- `false`: skipped in Tab navigation.

`Button`, `Edit`, `ListBox`, `ComboBox`, `ListView`, `TreeView`, `Trackbar`: `true`.  
`Static`, `GroupBox`, `ProgressBar`: `false`.

### WM_SETFONT / WM_GETFONT

Handled by `DefWindowProc`: stores to `hwnd->m_font`, triggers `InvalidateRect`.
All controls use `hwnd->m_font` when painting (or default font if NULL).

### WM_ERASEBKGND

Default: return 1 (background is handled in WM_PAINT). Controls do not rely on
the OS to erase background separately.

### Parent notification pattern

All controls notify the parent via:
```c
SendMessage(GetParent(hwnd), WM_COMMAND,
            MAKEWPARAM(hwnd->m_id, notificationCode), (LPARAM)hwnd);
```

### Disabled appearance

Controls check `IsWindowEnabled(hwnd)` in WM_PAINT and draw in muted colors when
disabled. Input messages (mouse/keyboard) are dropped for disabled windows.

---

*End of SWELL Built-in Control Specifications*
