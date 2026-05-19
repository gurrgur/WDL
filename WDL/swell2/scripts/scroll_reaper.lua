-- scroll_reaper.lua
-- Wait for REAPERTrackListWindow, pause 3s, then send continuous
-- sinusoidal scroll events (zoom via WM_MOUSEWHEEL, hscroll via WM_MOUSEHWHEEL).
--
-- Usage: SWELL_PROF_SCRIPT=scripts/scroll_reaper.lua ./reaper
--
-- Config via env:
--   SCROLL_ZOOM_FREQ=0.5    -- sin frequency (Hz) for vertical scroll
--   SCROLL_H_FREQ=0.7       -- cos frequency (Hz) for horizontal scroll
--   SCROLL_ZOOM_AMP=120     -- wheel delta amplitude
--   SCROLL_H_AMP=120        -- hwheel delta amplitude
--   SCROLL_PERIOD_MS=16     -- event interval (~60fps)
--   SCROLL_DURATION_SEC=0   -- stop after N seconds (0 = run forever)

local CLASS = os.getenv("SCROLL_TARGET_CLASS") or "REAPERTrackListWindow"
local zoom_freq  = tonumber(os.getenv("SCROLL_ZOOM_FREQ") or 0.5)
local h_freq     = tonumber(os.getenv("SCROLL_H_FREQ")  or 0.7)
local zoom_amp   = tonumber(os.getenv("SCROLL_ZOOM_AMP") or 20)
local h_amp      = tonumber(os.getenv("SCROLL_H_AMP")   or 120)
local period_ms  = tonumber(os.getenv("SCROLL_PERIOD_MS") or 8)
local dur_sec    = tonumber(os.getenv("SCROLL_DURATION_SEC") or 0)

local target = nil
local phase = "wait"
local t_found = 0
local last_event = 0

-- Recursive search across full HWND tree
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

-- re-find target window if lost
local function ensure_target()
  if not target then
    local kids = swell.get_children(nil)
    for _, top in ipairs(kids) do
      target = find_class(top, CLASS)
      if target then break end
    end
  end
  return target
end

function tick()
  local now = swell.now_sec()

  if phase == "wait" then
    if ensure_target() then
      swell.print("found " .. CLASS .. " at " .. tostring(target))
      t_found = now
      phase = "pause"
    end
  elseif phase == "pause" then
    if not ensure_target() then
      phase = "wait"
    elseif now - t_found >= 1 then
      swell.print("starting scroll events on " .. tostring(target))
      phase = "scroll"
    end
  elseif phase == "scroll" then
    if dur_sec > 0 and now - (t_found + 1) >= dur_sec then
      swell.print("stopping after " .. dur_sec .. "s")
      swell.exit(0)
    end
    if not ensure_target() then
      swell.print("target window lost, re-waiting")
      phase = "wait"
    elseif now - last_event >= period_ms / 1000.0 then
      local t = now - (t_found + 3)
      local zoom = math.sin(2 * math.pi * zoom_freq * t) * zoom_amp
      local hscr = math.cos(2 * math.pi * h_freq     * t) * h_amp

      -- WM_MOUSEWHEEL wParam = MAKEWPARAM(fwKeys, zDelta)
      -- zDelta (signed short) in HIWORD, modifier keys in LOWORD.
      -- Cast delta to integer and pack: (delta << 16) | keys
      local function make_wheel_wp(delta, keys)
        keys = keys or 0
        local d = (delta // 1) & 0xFFFF  -- truncate to int for bitwise
        return (d << 16) | (keys & 0xFFFF)
      end

      swell.post_message(target, swell.WM_MOUSEWHEEL,
                         make_wheel_wp(zoom, 0), 0)
      -- swell.post_message(target, swell.WM_MOUSEHWHEEL,
      --                    make_wheel_wp(hscr, 0), 1)
      last_event = now
    end
  end
end
