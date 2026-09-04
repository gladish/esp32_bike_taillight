#pragma once

#include <span>

#include <stddef.h>
#include <stdint.h>

// Maximum number of LEDs the engine will bind a `strip` object for.
// (The real board has 8; this is a generous host-testing ceiling.)
#define LUA_LED_MAX_COUNT   8

// Maximum size of a Lua script (source), in bytes. Matches the eventual
// NVS-backed storage budget on the microcontroller.
#define LUA_SCRIPT_MAX_SIZE 4096

// Upper bound for strip.set()'s brightness argument -- matches this
// board's led_strip_spi driver, which takes a   0-100 input and maps it
// down to the SK9822's 5-bit field internally (NOT a raw 0-31 value,
// despite what an earlier version of this comment said).
#define LUA_LED_MAX_BRIGHTNESS 100

// Result of evaluating one pixel.
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t brightness;  // 0-100; passed straight through to the driver,
                       // which does its own 0-100 -> 5-bit mapping
} LuaLedResult;

// Opaque engine handle.
typedef struct LuaLedEngine LuaLedEngine;

/**
 * Allocate and initialize the Lua engine.
 *
 * Strips all dangerous stdlib modules (io, os, package, debug) and the
 * dangerous parts of base (dofile, loadfile, require, load,
 * collectgarbage, raw*). Exposes only: base, math, table, string.
 *
 * Returns NULL on allocation failure.
 */
LuaLedEngine* lua_led_engine_create(void);

/**
 * Free all resources owned by the engine. Safe to call with NULL.
 */
void lua_led_engine_destroy(LuaLedEngine* engine);

/**
 * Compile and run a Lua script from a NUL-terminated source string, then
 * call LedStrip_Setup(led_count) if the script defines it.
 *
 * The script must define a global:
 *
 *   function LedStrip_Tick(strip, now_ms)
 *     strip.set(i, r, g, b [, brightness])  -- i is 0-based
 *   end
 *
 * and may optionally define:
 *
 *   function LedStrip_Setup(led_count)
 *     -- one-time init; called once, right after load
 *   end
 *
 * Returns true on success, false on compile error, a runtime error while
 * executing the chunk, or a missing/non-function LedStrip_Tick. Call
 * lua_led_engine_last_error() for the message.
 */
bool lua_led_engine_load(LuaLedEngine* engine,
                          const char*  source,
                          int          led_count);

/**
 * Evaluate one frame by calling LedStrip_Tick(strip, now_ms).
 *
 * out_leds.size() must match the led_count passed to lua_led_engine_load().
 *
 * On success, writes out_leds.size() results into out_leds and returns
 * true. On error (Lua runtime error, or an out-of-range strip.set()
 * index), out_leds is left completely untouched -- the frame commit is
 * all-or-nothing, so a mid-script error never leaves a half-drawn strip.
 * The caller keeps displaying whatever it already had.
 */
bool lua_led_engine_tick(LuaLedEngine*           engine,
                          double                  now_ms,
                          std::span<LuaLedResult> out_leds);

/**
 * Returns the last error string produced by load or tick.
 * Valid until the next call to the engine. Never NULL.
 */
const char* lua_led_engine_last_error(const LuaLedEngine* engine);

/**
 * Returns current heap usage of the Lua VM in bytes. Useful for tuning
 * on constrained targets.
 */
size_t lua_led_engine_heap_used(const LuaLedEngine* engine);
