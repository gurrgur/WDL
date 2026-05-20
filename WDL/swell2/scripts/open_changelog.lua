-- open_changelog.lua
-- Wait for REAPERTrackListWindow, pause, open Help → Changelog,
-- wait for the "About REAPER" window, find its text edit,
-- then continuously scroll it down.
--
-- Usage: SWELL_PROF_SCRIPT=scripts/open_changelog.lua ./reaper
--
-- Config via env:
--   OPEN_TRIGGER_CLASS=REAPERTrackListWindow
--   OPEN_MENU=Help
--   OPEN_ITEM=Changelog
--   OPEN_DELAY_SEC=1
--   SCROLL_PERIOD_MS=50        -- scroll event interval
--   SCROLL_AMP=120              -- wheel delta per notch (signed)

local TRIGGER = os.getenv("OPEN_TRIGGER_CLASS") or "REAPERTrackListWindow"
local MENU    = os.getenv("OPEN_MENU") or "Help"
local ITEM    = os.getenv("OPEN_ITEM") or "Changelog"
local DELAY   = tonumber(os.getenv("OPEN_DELAY_SEC") or 1)
local SCROLL_MS = tonumber(os.getenv("SCROLL_PERIOD_MS") or 8)
local SCROLL_AMP = tonumber(os.getenv("SCROLL_AMP") or -10)

local top_hwnd = nil
local about_hwnd = nil
local edit_hwnd = nil
local phase = "wait"
local t_found = 0
local t_opened = 0
local last_scroll = 0

local function find_class(hwnd, class)
  if hwnd then
    local cls = swell.get_class(hwnd)
    if cls == class then return hwnd end
  end
  for _, ch in ipairs(swell.get_children(hwnd)) do
    local r = find_class(ch, class)
    if r then return r end
  end
  return nil
end

-- Find first HWND with class matching one of the given names
local function find_first_class(hwnd, classes)
  if hwnd then
    local cls = swell.get_class(hwnd) or ""
    for _, c in ipairs(classes) do
      if cls == c then return hwnd end
    end
  end
  for _, ch in ipairs(swell.get_children(hwnd)) do
    local r = find_first_class(ch, classes)
    if r then return r end
  end
  return nil
end

-- Find first window whose title starts with prefix
local function find_title_prefix(hwnd, prefix)
  if hwnd then
    local txt = swell.get_text(hwnd) or ""
    if txt:sub(1, #prefix) == prefix then return hwnd end
  end
  for _, ch in ipairs(swell.get_children(hwnd)) do
    local r = find_title_prefix(ch, prefix)
    if r then return r end
  end
  return nil
end

local function top_level(hwnd)
  local p = hwnd
  while p do
    local par = swell.get_parent(p)
    if not par then return p end
    p = par
  end
end

local function center_of(hwnd)
  local r = swell.get_rect(hwnd)
  if not r then return 0, 0 end
  return r.x + r.w // 2, r.y + r.h // 2
end

local function make_wheel_wp(delta, keys)
  keys = keys or 0
  local d = (delta // 1) & 0xFFFF
  return (d << 16) | (keys & 0xFFFF)
end

function tick()
  local now = swell.now_sec()

  if phase == "wait" then
    local kids = swell.get_children(nil)
    for _, top in ipairs(kids) do
      local h = find_class(top, TRIGGER)
      if h then
        top_hwnd = top_level(h)
        swell.print("found trigger " .. TRIGGER .. ", top=" .. tostring(top_hwnd))
        t_found = now
        phase = "pause"
        break
      end
    end
  elseif phase == "pause" then
    if now - t_found >= DELAY then
      phase = "open"
    end
  elseif phase == "open" then
    local cmd = swell.find_menu_cmd(top_hwnd, MENU, ITEM)
    if cmd then
      swell.print("menu '" .. MENU .. " → " .. ITEM .. "' = cmd " .. tostring(cmd))
      swell.post_message(top_hwnd, swell.WM_COMMAND, cmd, 0)
      swell.print("posted WM_COMMAND " .. tostring(cmd))
      t_opened = now
      phase = "wait_about"
    else
      swell.print("menu item not found: " .. MENU .. " → " .. ITEM)
      swell.exit(1)
    end
  elseif phase == "wait_about" then
    local kids = swell.get_children(nil)
    for _, top in ipairs(kids) do
      about_hwnd = find_title_prefix(top, "About REAPER")
      if about_hwnd then
        swell.print("found About REAPER window: " .. tostring(about_hwnd))
        phase = "find_edit"
        break
      end
    end
  elseif phase == "find_edit" then
    edit_hwnd = find_first_class(about_hwnd, {"SWELL_Edit", "Edit"})
    if edit_hwnd then
      local cls = swell.get_class(edit_hwnd) or "?"
      swell.print("found edit [" .. cls .. "]: " .. tostring(edit_hwnd))
      phase = "scroll"
    end
  elseif phase == "scroll" then
    if now - last_scroll >= SCROLL_MS / 1000.0 then
      local cx, cy = center_of(edit_hwnd)
      swell.set_cursor_pos(cx, cy)
      -- negative = scroll down
      local wp = make_wheel_wp(SCROLL_AMP, 0)
      swell.send_message(edit_hwnd, swell.WM_MOUSEWHEEL, wp, 0)
      last_scroll = now
    end
  end
end
