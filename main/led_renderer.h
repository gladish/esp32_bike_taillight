#ifndef LED_RENDERER_H
#define LED_RENDERER_H

#include <led_strip_spi.h>

#include <array>
#include <cstdint>
#include <type_traits>
#include <variant>

enum class LedPattern : uint8_t
{
  kSolidRed = 0,
  kChase,
  kPulse,
  kStrobe,
  kTwinkle,
  kRandom,
  kPulseLua,   // same animation as kPulse, but Lua-driven -- see MakePatternRenderer
  kLast = kPulseLua
};


constexpr LedPattern GetNextLedPattern(LedPattern pattern)
{
  using LedPatternBaseType = std::underlying_type_t<LedPattern>;

  constexpr LedPatternBaseType kMin = static_cast<LedPatternBaseType>(LedPattern::kSolidRed);
  constexpr LedPatternBaseType kMax = static_cast<LedPatternBaseType>(LedPattern::kLast);

  LedPatternBaseType value = static_cast<LedPatternBaseType>(pattern);
  value = (value >= kMax) ? kMin : value + 1;

  return static_cast<LedPattern>(value);
}


// Draws a solid color across the whole strip. Kept as a free function
// (rather than folded into SolidColorRenderer) because main.cpp also calls
// it directly to flush an all-off frame on shutdown/deep sleep, outside the
// normal pattern-dispatch path.
esp_err_t RenderSolidColor(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness);


// ---------------------------------------------------------------------------
// Per-pattern renderers.
//
// Each type owns exactly the animation state and config it needs. Previously
// every pattern shared one PatternContext (chase_index, chase_forward,
// strobe_on, ...) whether it used those fields or not, and default color/
// brightness lived in a separate PatternConfig array indexed in parallel.
// That made sense while every pattern was a stateless function; it stopped
// making sense the moment a pattern needs to own a resource with real
// lifetime (see LuaScriptRenderer below) -- a
// lua_State* doesn't fit into a shared POD struct.
//
// Switching patterns reconstructs the active alternative from scratch (see
// MakePatternRenderer), which is why there's no separate Reset(): a fresh
// object *is* the reset, and it matches today's behavior exactly, since
// nothing currently mutates a pattern's config while another pattern is
// active (the old PatternConfig array was populated once at boot from
// hardcoded defaults -- the "TODO: load from NVS" in the previous code was
// never implemented). If per-pattern config ever does need to survive a
// switch away and back (e.g. once NVS loading lands), these renderers will
// need to move out of the variant into something longer-lived -- flagging
// that now so it isn't a surprise later.
// ---------------------------------------------------------------------------

class SolidColorRenderer
{
public:
  SolidColorRenderer();

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  rgb_t   color;
  uint8_t brightness;
};

class ChaseRenderer
{
public:
  ChaseRenderer();

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  rgb_t   color;
  uint8_t brightness;

private:
  int64_t last_update_us_ = 0;
  int     chase_index_ = 0;
  bool    chase_forward_ = true;
};

class PulseRenderer
{
public:
  PulseRenderer();

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  rgb_t   color;
  uint8_t max_brightness;
};

class StrobeRenderer
{
public:
  StrobeRenderer();

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  rgb_t   color;
  uint8_t brightness;

private:
  int64_t last_update_us_ = 0;
  bool    strobe_on_ = false;
};

class TwinkleRenderer
{
public:
  TwinkleRenderer();

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  rgb_t   color;
  uint8_t brightness;

private:
  int64_t last_update_us_ = 0;
};

class RandomRenderer
{
public:
  RandomRenderer();

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  uint8_t brightness;

private:
  std::array<rgb_t, 2> palette_;
  int64_t last_update_us_ = 0;
};


// Opaque; the full definition and the Lua C API live in lua_led_engine.h,
// included only by led_renderer.cpp -- nothing that just uses LuaScriptRenderer
// through this header needs to see the Lua VM's guts.
struct LuaLedEngine;

class LuaScriptRenderer
{
public:
  LuaScriptRenderer();
  ~LuaScriptRenderer();

  // Owns a lua_State* by pointer, so it moves (transfer ownership) instead
  // of copying. These have to be spelled out explicitly: declaring the
  // destructor above suppresses the compiler-generated implicit move ctor/
  // assignment, and without an explicit noexcept move, std::variant would
  // fall back to a more defensive (and non-trivial) assignment strategy
  // when this renderer becomes active or inactive.
  LuaScriptRenderer(LuaScriptRenderer&& other) noexcept;
  LuaScriptRenderer& operator=(LuaScriptRenderer&& other) noexcept;
  LuaScriptRenderer(const LuaScriptRenderer&) = delete;
  LuaScriptRenderer& operator=(const LuaScriptRenderer&) = delete;

  // Compiles and loads a new script, replacing whatever was previously
  // loaded. Returns false on a compile or setup error -- call LastError()
  // for details. A failed load leaves nothing loaded: Render() then leaves
  // the strip completely untouched every frame until a script loads
  // successfully.
  bool LoadScript(const char* source, uint8_t led_count);

  esp_err_t Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us);

  const char* LastError() const;

private:
  LuaLedEngine* engine_ = nullptr;
  bool          loaded_ = false;
};


using PatternRenderer = std::variant<
  SolidColorRenderer,
  ChaseRenderer,
  PulseRenderer,
  StrobeRenderer,
  TwinkleRenderer,
  RandomRenderer,
  LuaScriptRenderer>;

// Builds a freshly-constructed renderer for the given pattern. kPulseLua
// loads a hardcoded script (see led_renderer.cpp) into a fresh
// LuaScriptRenderer -- unlike the other five, it needs led_count up front
// to do that load, which is why this takes one. If the hardcoded load ever
// fails, the returned renderer just has nothing loaded (Render() leaves the
// strip untouched) rather than that being a fatal error -- same fallback
// behavior LuaScriptRenderer already has for a bad script in general.
PatternRenderer MakePatternRenderer(LedPattern pattern, uint8_t led_count);

// Draws one frame with whichever renderer is currently held, then flushes
// the strip. now_us should come from esp_timer_get_time().
esp_err_t RenderLedPattern(
  PatternRenderer&  renderer,
  led_strip_spi_t*  leds,
  uint8_t           n,
  int64_t           now_us);

#endif
