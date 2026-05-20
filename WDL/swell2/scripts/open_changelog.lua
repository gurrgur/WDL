-- open_changelog.lua
-- Wait for REAPERTrackListWindow, then open Help → Changelog (whatsnew.txt)
-- via menu command ID lookup and WM_COMMAND.
--
-- Usage: SWELL_PROF_SCRIPT=scripts/open_changelog.lua ./reaper

local TRIGGER = os.getenv("OPEN_TRIGGER_CLASS") or "REAPERTrackListWindow"
local MENU    = os.getenv("OPEN_MENU") or "Help"
local ITEM    = os.getenv("OPEN_ITEM") or "Changelog"
local DELAY   = tonumber(os.getenv("OPEN_DELAY_SEC") or 1)

local top_hwnd = nil
local phase = "wait"
local t_found = 0

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

local function top_level(hwnd)
  local p = hwnd
  while p do
    local par = swell.get_parent(p)
    if not par then return p end
    p = par
  end
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
      phase = "done"
    else
      swell.print("menu item not found: " .. MENU .. " → " .. ITEM)
      swell.exit(1)
    end
  elseif phase == "done" then
    -- keep reaper running
  end
end
