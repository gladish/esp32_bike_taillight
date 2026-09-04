/**
 * demo_scripts.cpp
 *
 * One Lua script per built-in C++ renderer in led_renderer.cpp, each one a
 * line-for-line port of that renderer's own math into the sandboxed script
 * API (see lua_led_engine.h for the sandboxing rules and strip.set()'s
 * parameter list). Two purposes:
 *
 *  - Worked examples of the scripting API for anyone writing their own
 *    script -- these cover the common patterns: a static color, a timed
 *    step with persistent state, a whole-strip brightness curve, and
 *    per-pixel randomization.
 *  - A way to exercise the Lua pipeline against known-good behavior --
 *    each script's output is already known (it's the matching C++
 *    renderer's math), so any visible difference points at a bug in the
 *    Lua plumbing rather than in the animation itself.
 *
 * kDemoScript is what LedPattern::kLuaDemo actually loads (see
 * MakePatternRenderer in led_renderer.cpp) -- point it at whichever one of
 * these you want to try. This all goes away once scripts load from NVS
 * instead of being picked at compile time.
 */

#include "led_renderer.h"

namespace {

// Solid red, no animation -- the smallest possible script. Matches
// SolidColorRenderer's default color and brightness.
constexpr const char* kSolidRedScript = R"lua(
local led_count

function LedStrip_Setup(n)
  led_count = n
end

function LedStrip_Tick(strip, now_ms)
  for i = 0, led_count - 1 do
    strip.set(i, 255, 255, 0, 40)
  end
end
)lua";

// A single lit pixel bouncing end to end every 100ms, matching
// ChaseRenderer -- including how it turns around: on reaching either end
// it reflects to the *next* pixel in (n-2, or 1) rather than to the end
// pixel again, so an endpoint is rendered once per pass, never twice in
// a row.
constexpr const char* kChaseScript = R"lua(
local led_count
local index = 0
local forward = true
local last_step_ms = -1000

function LedStrip_Setup(n)
  led_count = n
end

function LedStrip_Tick(strip, now_ms)
  if now_ms - last_step_ms >= 100 then
    last_step_ms = now_ms
    if forward then
      index = index + 1
      if index >= led_count then
        index = led_count - 2
        forward = false
      end
    else
      index = index - 1
      if index < 0 then
        index = 1
        forward = true
      end
    end
  end

  for i = 0, led_count - 1 do
    if i == index then
      strip.set(i, 255, 0, 0, 40)
    else
      strip.set(i, 255, 0, 0, 0)
    end
  end
end
)lua";

// Whole-strip breathing brightness on a 2000ms triangle wave, matching
// PulseRenderer's timing and peak brightness -- green instead of red so
// it's trivially distinguishable by eye from the real kPulse pattern.
constexpr const char* kPulseScript = R"lua(
local led_count

function LedStrip_Setup(n)
  led_count = n
end

function LedStrip_Tick(strip, now_ms)
  local period_ms = 2000.0
  local cycle_ms = now_ms % period_ms
  local phase = cycle_ms / period_ms

  local triangle
  if phase < 0.5 then
    triangle = phase * 2.0
  else
    triangle = 2.0 - phase * 2.0
  end

  local brightness = math.floor(triangle * 40)
  for i = 0, led_count - 1 do
    strip.set(i, 0, 255, 0, brightness)
  end
end
)lua";

// Whole strip on for 50ms, off for 150ms, matching StrobeRenderer.
constexpr const char* kStrobeScript = R"lua(
local led_count
local on = false
local last_change_ms = 0

function LedStrip_Setup(n)
  led_count = n
end

function LedStrip_Tick(strip, now_ms)
  local elapsed = now_ms - last_change_ms
  if on and elapsed >= 50 then
    on = false
    last_change_ms = now_ms
  elseif (not on) and elapsed >= 150 then
    on = true
    last_change_ms = now_ms
  end

  local brightness = on and 40 or 0
  for i = 0, led_count - 1 do
    strip.set(i, 255, 0, 0, brightness)
  end
end
)lua";

// Every pixel independently re-rolls on/off every 200ms, matching
// TwinkleRenderer's coin-flip-per-pixel update. The per-pixel state has
// to be held in a table between rolls -- recomputing it every tick would
// flicker on every frame instead of every 200ms.
constexpr const char* kTwinkleScript = R"lua(
local led_count
local last_update_ms = -1000
local lit = {}

function LedStrip_Setup(n)
  led_count = n
  for i = 0, n - 1 do
    lit[i] = false
  end
end

function LedStrip_Tick(strip, now_ms)
  if now_ms - last_update_ms >= 200 then
    last_update_ms = now_ms
    for i = 0, led_count - 1 do
      lit[i] = (math.random(0, 1) == 0)
    end
  end

  for i = 0, led_count - 1 do
    strip.set(i, 255, 0, 0, lit[i] and 40 or 0)
  end
end
)lua";

// Every pixel independently repicks a color from a 2-color palette every
// 500ms, matching RandomRenderer. Same per-pixel-state-in-a-table pattern
// as the twinkle script above, just with a color per pixel instead of a
// bool.
constexpr const char* kRandomScript = R"lua(
local led_count
local last_update_ms = -1000
local palette = {
  {255, 60, 150},
  {0, 170, 220},
}
local pixel_r, pixel_g, pixel_b = {}, {}, {}

function LedStrip_Setup(n)
  led_count = n
  for i = 0, n - 1 do
    pixel_r[i], pixel_g[i], pixel_b[i] = 0, 0, 0
  end
end

function LedStrip_Tick(strip, now_ms)
  if now_ms - last_update_ms >= 500 then
    last_update_ms = now_ms
    for i = 0, led_count - 1 do
      local c = palette[math.random(1, #palette)]
      pixel_r[i], pixel_g[i], pixel_b[i] = c[1], c[2], c[3]
    end
  end

  for i = 0, led_count - 1 do
    strip.set(i, pixel_r[i], pixel_g[i], pixel_b[i], 30)
  end
end
)lua";

}  // namespace

// Which of the scripts above LedPattern::kLuaDemo actually loads. Point
// this at whichever one you want to try; defaults to kPulseScript, which
// is what LedPattern::kLuaDemo's predecessor (kPulseLua) always loaded.
const char* const kDemoScript = kSolidRedScript;
