#ifndef LED_PATTERN_H
#define LED_PATTERN_H

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
  kLast = kRandom
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
// lifetime (see LuaScriptRenderer, coming in a follow-up change) -- a
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


using PatternRenderer = std::variant<
  SolidColorRenderer,
  ChaseRenderer,
  PulseRenderer,
  StrobeRenderer,
  TwinkleRenderer,
  RandomRenderer>;

// Builds a freshly-constructed renderer (hardcoded defaults, animation
// state zeroed) for the given pattern.
PatternRenderer MakePatternRenderer(LedPattern pattern);

// Draws one frame with whichever renderer is currently held, then flushes
// the strip. now_us should come from esp_timer_get_time().
esp_err_t RenderLedPattern(
  PatternRenderer&  renderer,
  led_strip_spi_t*  leds,
  uint8_t           n,
  int64_t           now_us);

#endif
