# SWELL Inspector — Implementation Plan

## Goal

Build GTK4 Inspector-equivalent for SWELL2 applications (REAPER etc.).
Same-process: inspector = separate SDL3 top-level window that reads SWELL internal state.
All rendering uses SWELL GDI (Skia) — no external UI toolkit dependency.

---

## 1. Module Layout

```
swell2/
  swell-inspector.h               Public API header (init, show, hide, toggle)
  swell-inspector.cpp             Core inspector: window tree, property sheet, message spy
  swell-inspector-paint.cpp       Inspector UI rendering (Skia via SWELL GDI)
  swell-inspector-state.cpp       State accessors — safe wrappers around SWELL internals
  swell-inspector-msgspy.cpp      Message hook/injection layer
  swell-inspector-overlay.cpp     Optional rendering overlay (layout bounds, highlight)
```

`CMakeLists.txt` adds inspector sources when `-DSWELL2_INSPECTOR=ON`.

---

## 2. Public API (`swell-inspector.h`)

```cpp
// One-shot init. Call after SWELL initargs + PostMessage_Init.
// Creates inspector window (hidden).
bool SWELL_Inspector_Init(const char *appName);

// Toggle visibility of inspector window.
void SWELL_Inspector_Show(bool show);
void SWELL_Inspector_Toggle();

// Returns true if inspector window exists and is visible.
bool SWELL_Inspector_IsVisible();

// Keybinding hook — call from SWELLAppMain PROCESSMESSAGE or per-window
// wndproc to handle Ctrl+Shift+I toggle.
bool SWELL_Inspector_HandleKey(int vk, bool ctrl, bool shift);

// Enable/disable message spy for a specific window or globally.
void SWELL_Inspector_SpyWindow(HWND hwnd);  // NULL = global
void SWELL_Inspector_SpyStop();

// Register a custom property provider (for app-specific object inspection).
typedef void (*SWELL_InspectorPropProvider)(HWND hwnd, WDL_FastString *json_out);
void SWELL_Inspector_RegisterPropProvider(SWELL_InspectorPropProvider fn);
```

---

## 3. Architecture

```
                    ┌─────────────────────────┐
                    │   SWELL Target App      │
                    │   (REAPER, etc.)        │
                    │  ┌────┐ ┌────┐ ┌────┐   │
                    │  │wnd1│ │wnd2│ │wnd3│   │
                    │  └────┘ └────┘ └────┘   │
                    └──────────┬──────────────┘
                               │  g_swell_top_level_list
                               │  g_swell_focus/capture
                               │  g_swell_theme/ui_scale
                               │  HWND__->m_children/m_owner/m_owned
                               │  HWND__->m_private_data
                               ▼
                    ┌─────────────────────────┐
                    │  SWELL Inspector         │
                    │  ┌───────────────────┐   │
                    │  │ State Scanner     │   │  Refresh timer (~250ms)
                    │  │ (snapshot SWELL   │   │
                    │  │  internal state)  │   │
                    │  └───────┬───────────┘   │
                    │          ▼               │
                    │  ┌───────────────────┐   │
                    │  │ Inspector Model    │   │  Own data, not SWELL pointers
                    │  │ (UI selection,     │   │  (copy, don't hold)
                    │  │  expanded nodes,   │   │
                    │  │  scroll state)     │   │
                    │  └───────┬───────────┘   │
                    │          ▼               │
                    │  ┌───────────────────┐   │
                    │  │ Renderer          │   │  SWELL GDI on own
                    │  │ (Skia via SWELL)   │   │  m_backingstore
                    │  └───────────────────┘   │
                    │                          │
                    │  ┌───────────────────┐   │
                    │  │ Message Spy       │   │  Optional wndproc hook
                    │  │ (intercepts WM_*)  │   │
                    │  └───────────────────┘   │
                    └─────────────────────────┘
```

**Inspector is a SWELL window itself** — uses same HWND__ / WNDPROC / Skia pipeline as target app. This means it can co-exist in the same event loop trivially.

---

## 4. Window Creation

Inspector creates a dialog-level top-level window via SWELL GDI directly (not `.rc` resource):

```
HWND g_inspector_window = CreateWindow(...)
  WS_OVERLAPPEDWINDOW | WS_VISIBLE
  class: "SWELL_Inspector"
  title: "SWELL Inspector"
  wndproc: inspectorWindowProc
```

Sub-panels are child `HWND__` instances within inspector:
- **TreePanel** — left pane, vertical scroll, expandable tree of all windows
- **PropertyPanel** — right pane, vertical scroll, property name/value list
- **LogPanel** — bottom pane, horizontal scroll, message log
- **Menu** — inspector menu bar: [File] [View] [Tools] [Help]

All sub-panels render in their `WM_PAINT` using Skia directly (via `GetDC`/`ReleaseDC` on the inspector window or using `SWELL_CreateMemContext` + `BitBlt`).

---

## 5. Core Features

### 5.1 Window Tree Browser (left pane)

**Data model**: snapshot tree built on each refresh cycle.

```cpp
struct InspectorNode {
  HWND hwnd;              // but only valid during snapshot
  std::string classname;
  std::string title;
  int id;
  RECT rect;              // screen coords
  bool visible;
  bool enabled;
  bool hasChildren;
  bool hasFocus;
  int depth;
  DWORD style, exstyle;
};
```

**Tree walk**: start at `g_swell_top_level_list`, recurse `m_children` + `m_owned`.
Skip dead windows (`m_hashaddestroy >= 2`).
Include popup windows from `swell-backend-sdl3.cpp`'s `g_sdl_windows` if accessible.

**Rendering**: Indented rows with disclosure triangle, class icon (letter glyph), classname + title.
Focus highlight matches `g_swell_focus`. Selection highlight = accent color.
Scroll via mouse wheel / vertical trackbar (manual scroll state in model).

### 5.2 Property Panel (right pane)

Groups shown for selected window:

| Group | Properties |
|-------|-----------|
| **Identity** | HWND address, classname, control ID, title, wndproc address, dlgproc address |
| **Hierarchy** | parent HWND, owner HWND, child count, owned count, next/prev sibling |
| **Geometry** | m_position (screen/parent-relative), GetClientRect, GetWindowRect, DPI status |
| **Style** | decoded WS_* flags (checkbox rows), decoded WS_EX_* flags |
| **State** | visible, enabled, wantfocus, hasfocus, iscaptured, isforeground, hasdestroy |
| **Focused child** | m_focused_child HWND |

**Control-type-specific groups** (via `m_private_data` cast):

| Class | Extra Properties |
|-------|-----------------|
| `Button` | `buttonWindowState`: state (BST_*), bitmap mode, HICON address |
| `Edit` | `__SWELL_editControlState`: cursor_pos, sel1/sel2, scroll_x/y, multiline cache stats |
| `SysListView32` / `ListBox` | `listViewState`: item count, selitem, scroll, column count, extended style, color overrides, capmode |
| `SysTreeView32` | `treeViewState`: root HTREEITEM, selection, scroll, capmode, colors |
| `ComboBox` | `__SWELL_ComboBoxInternalState`: selidx, item count, dropdown_armed |
| `SysTabControl32` | `tabControlState`: curtab, tab count |
| `msctls_trackbar32` | m_extra[0..3]: min/max/pos/tic |
| `msctls_progress32` | m_extra[0..2]: pos/min/max |

**Rendering**: Two-column layout (property name | value). Section headers with accent underline.
Scrollable vertically. Editable values for bool flags (click to toggle for testing).

### 5.3 Theme Viewer

Read-only display of `g_swell_theme`:

| Group | Fields |
|-------|--------|
| **Mode** | light/dark, g_swell_theme_mode, g_swell_ui_scale (256=1.0x) |
| **Surface colors** | bg_window, bg_surface, bg_input, bg_input_alt, bg_header — each with color swatch + hex |
| **Button colors** | bg_button, *_hover, *_pressed |
| **Accent** | accent, accent_hover, accent_pressed, fg_on_accent |
| **Menu colors** | bg_menu, bg_menu_hover, bg_menubar, bg_menubar_hover |
| **Tab/Scroll/Track/Progress** | all theme colors |
| **Text colors** | fg_text, fg_text_dim, fg_text_disabled |
| **Border/Focus/Shadow** | border, border_strong, focus_ring, shadow, caret |
| **Metrics (logical px)** | corner_radius, border_width, padding values, min heights/widths |

Each color row shows a filled rect swatch next to the hex value, using that color.

### 5.4 Message Spy

**Hook mechanism**: Replace `hwnd->m_wndproc` with a spy wrapper that:
1. Logs incoming message (hwnd, msg name, wParam, lParam)
2. Calls original `m_wndproc`
3. Logs return value

```cpp
struct SpyEntry {
  HWND hwnd;
  WNDPROC original_proc;
};

LRESULT spyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  SpyEntry *spy = (SpyEntry *)GetProp(hwnd, "SWELL_INSPECTOR_SPY");
  // Log entry
  inspector_log_msg(hwnd, msg, wParam, lParam, 0, true);
  LRESULT ret = spy->original_proc(hwnd, msg, wParam, lParam);
  // Log exit
  inspector_log_msg(hwnd, msg, wParam, lParam, ret, false);
  return ret;
}
```

**Message log viewer**: Tabular log with columns: timestamp, HWND, message name (decoded), wParam, lParam, return.
Filter by window, message range. Max buffer ~10000 entries (ring).

**Message name decoder**: Function mapping `UINT msg → const char *name` for WM_*, BM_*, EM_*, LB_*, LVM_*, TVM_*, CB_*, TBM_*, PBM_*, TCM_*.

### 5.5 Pick Mode

**Activation**: Ctrl+Shift+P or "Pick Window" button in toolbar.

**Behavior**: Inspector window hides, mouse cursor changes to crosshair.
Click on any window in the target app → inspector reappears with that window selected in tree.

**Implementation**:
1. Set a global SDL event filter (or use `g_swell_capture` override).
2. On click: `WindowFromPoint(mouse_screen_pt)` → find the deepest child.
3. Unhide inspector, expand tree to show selected window, select it.

**Challenge**: SDL3 event handling is cooperative (`swell_sdlDispatchEvent`). Pick mode must be injected into the event dispatch path. Solution: set a global flag checked at top of `swell_sdlEventHandler` that routes mouse-down events to pick handler before normal processing.

```cpp
// In swell-inspector.cpp
bool g_inspector_pick_mode = false;

// In swell-backend-sdl3.cpp, swell_sdlEventHandler, before normal processing:
extern bool g_inspector_pick_mode;
if (g_inspector_pick_mode && evt->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
  SWELL_Inspector_PickCallback(/* screen x,y */);
  return;
}
```

To avoid modifying swell-backend-sdl3.cpp, provide a callback registration point in swell-internal.h:

```cpp
// swell-internal.h addition:
typedef bool (*SWELL_InspectorEventFilter)(SDL_Event *evt);
extern SWELL_InspectorEventFilter g_swell_inspector_event_filter;
```

### 5.6 Rendering Overlay (optional)

During pick mode or persistent overlay mode:
- Draw translucent colored rectangles around window bounds (green = normal, red = focused, blue = captured).
- Show classname label in corner of each window.
- Highlight invalidated rects in yellow.

This uses a separate top-level transparent SDL window that overlays the entire screen and renders on top.

**Simpler approach**: Render overlay into existing windows' paint loop (optional hook in `SWELL_internalSkiaPaint`). Controlled via a global flag `g_inspector_overlay_enabled`.

---

## 6. Refresh Model

Inspector state is a **snapshot** refreshed on a timer (250ms default, configurable).
This avoids holding pointers to SWELL objects that may be freed between refreshes.

```cpp
void inspector_refresh() {
  // 1. Walk window tree → populate InspectorNode[] model
  // 2. If selection valid, collect property data
  // 3. Invalidate inspector panel rects
}
```

The refresh timer:
```cpp
SetTimer(g_inspector_window, INSPECTOR_REFRESH_TIMER_ID, 250, NULL);
// In WM_TIMER handler: inspector_refresh()
```

Selection state persists across refreshes by matching HWND pointer (with alive check via `m_hashaddestroy`).

---

## 7. File Details

### `swell-inspector.h` (~80 lines)
Public API as described in section 2.

### `swell-inspector.cpp` (~1200 lines)
- `inspectorWindowProc()` — WM_CREATE, WM_PAINT, WM_SIZE, WM_TIMER, WM_COMMAND, WM_KEYDOWN, WM_DESTROY
- `inspector_init()` — create window & sub-panels, start refresh timer
- `inspector_paint()` — layout: menu bar, tree pane (left ~40%), property pane (right), log pane (bottom ~30%)
- `inspector_refresh()` — tree snapshot, property collection
- `inspector_handle_click()` — tree node expand/collapse, property edit, pick mode toggle
- `inspector_key_handler()` — Ctrl+Shift+I toggle, escape to close, navigation keys

### `swell-inspector-paint.cpp` (~600 lines)
- `inspector_draw_tree_node()` — indented row with expand triangle, class icon, name
- `inspector_draw_property_row()` — name: value pair, section headers
- `inspector_draw_color_swatch()` — small filled rect for theme colors
- `inspector_draw_log_entry()` — tabular message log row
- `inspector_draw_tab()` — tab headers for switching between Properties/Theme/Log tabs

### `swell-inspector-state.cpp` (~400 lines)
- `inspector_snapshot_tree()` — walks `g_swell_top_level_list` → recursive tree
- `inspector_get_properties()` — fills property key/value list for selected HWND
- `inspector_get_control_state()` — type-specific private_data dump
- `inspector_get_theme()` — copy of `g_swell_theme` into inspector-friendly struct
- `inspector_get_global_state()` — focus, capture, foreground, timer count, PMQ count
- `inspector_msg_name()` — WM_* → string table

### `swell-inspector-msgspy.cpp` (~200 lines)
- `inspector_spy_install()` — hook wndproc
- `inspector_spy_remove()` — restore original wndproc
- `spyWndProc()` — wrapper that logs and forwards
- `inspector_log_add()` — append to ring buffer
- `inspector_log_clear()`

### `swell-inspector-overlay.cpp` (~150 lines)
- `inspector_overlay_enable()` / `inspector_overlay_disable()`
- `inspector_overlay_paint()` — called from `SWELL_internalSkiaPaint` hook
- Draws colored bounds rectangles + labels on each window

### CMakeLists.txt modification
```cmake
option(SWELL2_INSPECTOR "Build SWELL Inspector tool" ON)
if(SWELL2_INSPECTOR)
  list(APPEND INSPECTOR_SRC
    swell-inspector.cpp
    swell-inspector-paint.cpp
    swell-inspector-state.cpp
    swell-inspector-msgspy.cpp
    swell-inspector-overlay.cpp
  )
  target_sources(swell2 PRIVATE ${INSPECTOR_SRC})
  target_compile_definitions(swell2 PRIVATE SWELL2_INSPECTOR)
endif()
```

---

## 8. Integration Points (SWELL internals touched)

| Internal | Purpose |
|----------|---------|
| `g_swell_top_level_list` | Root of window tree walk |
| `g_swell_focus/capture/foreground` | Global state display |
| `g_swell_theme` | Theme viewer |
| `g_swell_theme_mode` / `g_swell_ui_scale` | Mode + DPI display |
| `g_timer_list` + `g_timer_mutex` | Timer count (lock briefly) |
| `g_pmq_head/tail` + `g_pmq_mutex` / `g_pmq_count` | PMQ stats |
| `HWND__::m_children` | Child walk |
| `HWND__::m_owner` / `m_owned` | Owner/owned walk |
| `HWND__::m_private_data` | Control state cast |
| `HWND__::m_hashaddestroy` | Liveness check |
| `HWND__::m_backingstore` | Surface info display |
| `HWND__::m_oswindow` | SDL window handle display |
| `HWND__::m_menu` | Menu tree display |
| `swell_oswindow_from_hwnd()` | SDL WindowEntry lookup |
| `SDL_WindowEntry` (swell-backend-sdl3.cpp) | Window ID, renderer, texture info |

**New internal declarations needed** (added to `swell-internal.h`):
```cpp
extern bool g_swell_inspector_overlay_active;
extern bool (*g_swell_inspector_paint_hook)(HWND hwnd, HDC hdc);
#ifdef SWELL_TARGET_SDL3
typedef bool (*SWELL_InspectorEventFilter)(SDL_Event *evt);
extern SWELL_InspectorEventFilter g_swell_inspector_event_filter;
#endif
```

Minimal hook in `SWELL_internalSkiaPaint` (after child paint loop):
```cpp
// swell-gdi.cpp, end of SWELL_internalSkiaPaint
if (g_swell_inspector_overlay_active) {
  swell_gdpLocalContext ctx2;
  // setup context, call overlay paint
}
```

---

## 9. Phased Implementation

### Phase 1: Scaffold + Tree Browser
- `swell-inspector.h`, `swell-inspector.cpp` (init, window, paint skeleton)
- Window tree snapshot + left pane rendering
- Toggle via `SWELL_Inspector_Toggle()`
- Ctrl+Shift+I keybinding

### Phase 2: Property Panel
- Static properties (identity, hierarchy, geometry, style flags)
- Style flag decoder (WS_* bitfield → named flag list)
- Editable bool for style testing

### Phase 3: Control State Inspector
- Type-specific property extraction (button, edit, listview, treeview, combo, tab, trackbar, progress)
- Color swatch rendering
- Scrollable property panel

### Phase 4: Theme Viewer
- Tab switching between Properties / Theme / Global State
- Full theme color swatch display
- Metric display in logical and physical pixels

### Phase 5: Message Spy
- Wndproc hook install/remove
- Log display in bottom pane
- Filter controls
- Message name decoder

### Phase 6: Pick Mode + Overlay
- SDL event filter registration
- Window picker (crosshair, click to select)
- Optional layout bounds overlay

### Phase 7: Polish
- Window resize handling (panel proportion dragging)
- Search/filter in tree and log
- Copy property values to clipboard
- Export window tree as text/JSON
- Menu actions

---

## 10. Rendering Notes

Inspector uses SWELL GDI for its own UI. This is intentional:
- No external UI library dependency
- Dogfoods the rendering layer it inspects
- Works in REAPER's build environment (which already has Skia + SWELL)

Drawing primitives used:
- `FillRect` — panel backgrounds, selection highlight, color swatches
- `DrawText` — all text rendering
- `Rectangle` / `RoundRect` — borders, buttons, tab headers
- `LineTo` — grid lines, tree connector lines
- `Ellipse` — radio buttons, disclosure triangle dots
- `Polygon` — disclosure triangles (expand/collapse arrows)
- `BitBlt` — icon rendering

Font: use `g_swell_default_font` (or `SWELL_GetDefaultFont()`) for consistency.
Colors: use `g_swell_theme` colors so inspector matches target app's theme.

Caching: Cache SkFont measurements during paint to avoid redundant `GetTextMetrics` calls per frame. Cache tree item heights for scroll calculation.

---

## 11. Safety Considerations

1. **Never hold HWND pointers across refresh cycles.** All state copied to InspectorNode.
2. **Check `m_hashaddestroy < 2`** before accessing any HWND__ field.
3. **Lock `g_pmq_mutex` / `g_timer_mutex`** briefly when reading PMQ/timer state.
4. **Message spy recursion guard**: spy wndproc must never spy on the inspector window itself.
5. **Pick mode must not steal focus/capture from REAPER permanently.** Release capture after pick completes.
6. **Compile behind `#ifdef SWELL2_INSPECTOR`** so it can be stripped for production builds.
7. **All inspector code in `swell2/` namespace convention** — follow existing coding patterns (NOMINMAX, SkFont wrappers, etc.).

---

## 12. Testing

- **Bench app integration**: Modify `bench_swell_window_app` to include inspector init → verify toggle works.
- **Manual test checklist**:
  1. Ctrl+Shift+I opens inspector
  2. Window tree shows all open windows (bench app + inspector itself)
  3. Selecting a node shows properties
  4. Edit control shows cursor/selection state
  5. ListView shows item count, columns, selection
  6. Theme viewer shows correct colors matching visual appearance
  7. Message spy logs WM_PAINT, WM_MOUSEMOVE etc.
  8. Pick mode selects correct window on click
  9. Overlay draws colored bounds around windows
  10. Inspector closes cleanly without crashes or leaks
- **Headless build**: Ensure inspector compiles but is a no-op when SDL3 backend not present.
