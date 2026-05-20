-- scroll_reaper.lua
-- Wait for REAPERTrackListWindow + REAPERTCPDisplay, maximize parent,
-- pause 1s, then send continuous sinusoidal vertical scroll
-- (WM_MOUSEWHEEL) to both at 120Hz. Moves cursor to each target
-- before posting so reaper routes the wheel event correctly.
--
-- Usage: SWELL_PROF_SCRIPT=scripts/scroll_reaper.lua ./reaper
--
-- Config via env:
--   SCROLL_ZOOM_FREQ=0.5    -- sin frequency (Hz)
--   SCROLL_ZOOM_AMP=20      -- wheel delta amplitude
--   SCROLL_PERIOD_MS=8      -- event interval (~120Hz)
--   SCROLL_DURATION_SEC=0   -- stop after N seconds (0 = run forever)

local TARGETS = {
  {class = "REAPERTrackListWindow", hwnd = nil},
  {class = "REAPERTCPDisplay",      hwnd = nil},
}
local zoom_freq  = tonumber(os.getenv("SCROLL_ZOOM_FREQ") or 0.5)
local zoom_amp   = tonumber(os.getenv("SCROLL_ZOOM_AMP")  or 20)
local period_ms  = tonumber(os.getenv("SCROLL_PERIOD_MS") or 8)
local dur_sec    = tonumber(os.getenv("SCROLL_DURATION_SEC") or 0)

local phase = "wait"
local t_found = 0
local last_event = 0
local maximized = false

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

local function all_found()
  for _, t in ipairs(TARGETS) do
    if not t.hwnd then
      local kids = swell.get_children(nil)
      for _, top in ipairs(kids) do
        t.hwnd = find_class(top, t.class)
        if t.hwnd then
          swell.print("found " .. t.class .. " at " .. tostring(t.hwnd))
          break
        end
      end
    end
    if not t.hwnd then return false end
  end
  return true
end

function tick()
  local now = swell.now_sec()

  if phase == "wait" then
    if all_found() then
      -- maximize once
      if not maximized then
        local tl = top_level(TARGETS[1].hwnd)
        if tl then
          swell.show_window(tl, swell.SW_SHOWMAXIMIZED)
          swell.print("maximized top-level " .. tostring(tl))
          -- -- fallback: force fullscreen if maximize ignored
          -- swell.fullscreen(tl, true)
          maximized = true
        end
      end
      t_found = now
      phase = "pause"
    end
  elseif phase == "pause" then
    if now - t_found >= 1 then
      swell.print("starting scroll events")
      phase = "scroll"
    end
  elseif phase == "scroll" then
    if dur_sec > 0 and now - (t_found + 1) >= dur_sec then
      swell.print("stopping after " .. dur_sec .. "s")
      swell.exit(0)
    end
    if not all_found() then
      swell.print("target window lost, re-waiting")
      phase = "wait"
    elseif now - last_event >= period_ms / 1000.0 then
      local t = now - (t_found + 1)
      local zoom = math.sin(2 * math.pi * zoom_freq * t) * zoom_amp
      local wp = make_wheel_wp(zoom, 0)
      for _, target in ipairs(TARGETS) do
        local cx, cy = center_of(target.hwnd)
        swell.set_cursor_pos(cx, cy)
        swell.send_message(target.hwnd, swell.WM_MOUSEWHEEL, wp, 0)
      end
      last_event = now
    end
  end
end
