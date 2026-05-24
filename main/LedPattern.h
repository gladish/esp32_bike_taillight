#ifndef LED_PATTERN_H
#define LED_PATTERN_H

#include <led_strip_spi.h>

#include <cstdint>
#include <type_traits>

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


struct AnimationClock
{
  int64_t last_update_us;
  int64_t now_us;
};


struct PatternContext
{
  AnimationClock clock;
  int chase_index;
  bool chase_forward;
  bool strobe_on;

  PatternContext();

  void reset();
};

struct PatternConfig
{
  rgb_t color;
  uint8_t brightness;
};

struct PatternDefinition;

using PatternRenderFn = esp_err_t (*)(
  led_strip_spi_t* leds,
  uint8_t n,
  PatternContext* ctx,
  PatternDefinition const& definition,
  PatternConfig const& config);


struct PatternDefinition
{

  static void InitializePatternConfigs();

  PatternRenderFn const render;
  rgb_t const* const palette;
  uint8_t const palette_size;
  PatternConfig const default_config;
};


esp_err_t RenderLedPattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  LedPattern        pattern,
  PatternContext* ctx);


esp_err_t RenderSolidColor(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness);


esp_err_t RenderChasePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness,
  PatternContext*   ctx);


esp_err_t RenderPulsePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           max_brightness,
  PatternContext*   ctx);


esp_err_t RenderTwinklePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness,
  PatternContext*   ctx);


esp_err_t RenderStrobePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness,
  PatternContext*   ctx);


esp_err_t RenderRandomPattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t const*      colors,
  uint8_t           color_count,
  uint8_t           brightness,
  PatternContext*   ctx);

#endif