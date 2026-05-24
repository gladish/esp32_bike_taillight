#include "LedPattern.h"

#include <esp_log.h>
#include <esp_random.h>

#include <array>

namespace {

const char* TAG = "LedPattern";
constexpr uint32_t kStrobeOnDurationUs = 50 * 1000;
constexpr uint32_t kStrobeOffDurationUs = 150 * 1000;
constexpr uint32_t kTwinkleIntervalUs = 200 * 1000;
constexpr uint32_t kRandomChangeIntervalUs = 500 * 1000;
constexpr uint8_t kDefaultBrightness = 40;
constexpr rgb_t kColorRed = {
    .r = 255,
    .g = 0,
    .b = 0,
};
constexpr std::array<rgb_t, 2> kInstigatorColors = {
    rgb_t{255, 60, 150},
    rgb_t{0, 170, 220},
};

using LedPatternBaseType = std::underlying_type_t<LedPattern>;

constexpr LedPatternBaseType PatternToIndex(LedPattern pattern)
{
  return static_cast<LedPatternBaseType>(pattern);
}

constexpr LedPatternBaseType kLedModeCount = PatternToIndex(LedPattern::kLast) + 1;

std::array<PatternConfig, kLedModeCount> patternConfigs;

PatternConfig const& GetPatternConfig(LedPattern pattern)
{
  uint8_t const index = PatternToIndex(pattern);
  if (index >= kLedModeCount) {
    return patternConfigs[0];
  }

  return patternConfigs[index];
}

esp_err_t RenderSolidColorDelegate(led_strip_spi_t* leds, uint8_t n, PatternContext* __unused(ctx),
  PatternDefinition const& __unused(definition), PatternConfig const& config)
{
  return RenderSolidColor(leds, n, config.color, config.brightness);
}

esp_err_t RenderChaseDelegate(led_strip_spi_t* leds, uint8_t n, PatternContext* ctx,
  PatternDefinition const& __unused(definition), PatternConfig const& config)
{
  return RenderChasePattern(leds, n, config.color, config.brightness, ctx);
}

esp_err_t RenderPulseDelegate(led_strip_spi_t* leds, uint8_t n, PatternContext* ctx,
  PatternDefinition const& __unused(definition), PatternConfig const& config)
{
  return RenderPulsePattern(leds, n, config.color, config.brightness, ctx);
}

esp_err_t RenderStrobeDelegate(led_strip_spi_t* leds, uint8_t n, PatternContext* ctx,
  PatternDefinition const& __unused(definition), PatternConfig const& config)
{
  return RenderStrobePattern(leds, n, config.color, config.brightness, ctx);
}

esp_err_t RenderTwinkleDelegate(led_strip_spi_t* leds, uint8_t n, PatternContext* ctx,
  PatternDefinition const& __unused(definition), PatternConfig const& config)
{
  return RenderTwinklePattern(leds, n, config.color, config.brightness, ctx);
}

esp_err_t RenderRandomDelegate(led_strip_spi_t* leds, uint8_t n, PatternContext* ctx,
  PatternDefinition const& definition, PatternConfig const& config)
{
  return RenderRandomPattern(leds, n, definition.palette, definition.palette_size, config.brightness, ctx);
}

constexpr PatternDefinition kPatternDefinitions[] =
{
  {
    RenderSolidColorDelegate,
    nullptr,
    0,
    {
      kColorRed,
      kDefaultBrightness
    }
  },

  {
    RenderChaseDelegate,
    nullptr,
    0,
    {
      kColorRed,
      kDefaultBrightness
    }
  },

  {
    RenderPulseDelegate,
    nullptr,
    0,
    {
      kColorRed,
      kDefaultBrightness
    }
  },

  {
    RenderStrobeDelegate,
    nullptr,
    0,
    {
      kColorRed,
      kDefaultBrightness
    }
  },

  {
    RenderTwinkleDelegate,
    nullptr,
    0,
    {
      kColorRed,
      kDefaultBrightness
    }
  },

  {
    RenderRandomDelegate,
    &kInstigatorColors[0],
    kInstigatorColors.size(),
    {
      kColorRed,
      30
    }
  },
};

PatternDefinition const& GetPatternDefinition(LedPattern pattern)
{
  uint8_t const index = PatternToIndex(pattern);
  if (index >= kLedModeCount) {
    return kPatternDefinitions[0];
  }

  return kPatternDefinitions[index];
}

}  // namespace

esp_err_t RenderSolidColor(led_strip_spi_t* leds, uint8_t n, rgb_t color, uint8_t brightness)
{
  return led_strip_spi_set_pixels_brightness(leds, 0, n, color, brightness);
}

PatternContext::PatternContext()
{
  reset();
}

void PatternContext::reset()
{
  clock.last_update_us = 0;
  clock.now_us = 0;
  chase_index = 0;
  chase_forward = true;
  strobe_on = false;
}

void PatternDefinition::InitializePatternConfigs()
{
  // TODO: Load this from NVS
  for (uint8_t index = 0; index < kLedModeCount; ++index) {
    patternConfigs[index] = kPatternDefinitions[index].default_config;
  }
}

esp_err_t RenderLedPattern(led_strip_spi_t* leds, uint8_t n, LedPattern pattern, PatternContext* ctx)
{
  PatternDefinition const& def = GetPatternDefinition(pattern);
  PatternConfig const& conf = GetPatternConfig(pattern);

  esp_err_t err = def.render(leds, n, ctx, def, conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to render pattern: %s", esp_err_to_name(err));
    return err;
  }

  return led_strip_spi_flush(leds);
}


esp_err_t RenderChasePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness,
  PatternContext*   ctx)
{
  if (ctx->clock.now_us - ctx->clock.last_update_us >= 100 * 1000) {
    ctx->clock.last_update_us = ctx->clock.now_us;

    for (int i = 0; i < n; i++) {
      if (i == ctx->chase_index) {
        ESP_ERROR_CHECK(led_strip_spi_set_pixel_brightness(leds, i, color, brightness));
      } else {
        ESP_ERROR_CHECK(led_strip_spi_set_pixel_brightness(leds, i, color, 0));
      }
    }

    if (ctx->chase_forward) {
      ctx->chase_index++;
      if (ctx->chase_index >= n) {
        ctx->chase_index = n - 2;
        ctx->chase_forward = false;
      }
    } else {
      ctx->chase_index--;
      if (ctx->chase_index < 0) {
        ctx->chase_index = 1;
        ctx->chase_forward = true;
      }
    }
  }
  return ESP_OK;
}

esp_err_t RenderPulsePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           max_brightness,
  PatternContext*   ctx)
{
  constexpr int64_t kPulsePeriodUs = 2'000'000;

  int64_t cycle_us = ctx->clock.now_us % kPulsePeriodUs;

  float phase = (float)cycle_us / (float)kPulsePeriodUs;

  float triangle;
  if (phase < 0.5f)
    triangle = phase * 2.0f;
  else
    triangle = 2.0f - (phase * 2.0f);

  uint8_t brightness = (uint8_t)(triangle * max_brightness);

  esp_err_t err = led_strip_spi_set_pixels_brightness(leds, 0, n, color, brightness);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}

esp_err_t RenderStrobePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness,
  PatternContext*   ctx)
{
  if (ctx->strobe_on && (ctx->clock.now_us - ctx->clock.last_update_us >= kStrobeOnDurationUs)) {
    // turn off
    esp_err_t err = led_strip_spi_set_pixels_brightness(leds, 0, n, color, 0);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
      return err;
    }
    ctx->strobe_on = false;
    ctx->clock.last_update_us = ctx->clock.now_us;
  } else if (!ctx->strobe_on && (ctx->clock.now_us - ctx->clock.last_update_us >= kStrobeOffDurationUs)) {
    // turn on
    esp_err_t err = led_strip_spi_set_pixels_brightness(leds, 0, n, color, brightness);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
      return err;
    }
    ctx->strobe_on = true;
    ctx->clock.last_update_us = ctx->clock.now_us;
  }

  return ESP_OK;
}

esp_err_t RenderTwinklePattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t             color,
  uint8_t           brightness,
  PatternContext*   ctx)
{
  if (ctx->clock.now_us - ctx->clock.last_update_us >= kTwinkleIntervalUs) {
    ctx->clock.last_update_us = ctx->clock.now_us;

    for (int i = 0; i < n; i++) {
      if (esp_random() % 2 == 0) {
        esp_err_t err = led_strip_spi_set_pixel_brightness(leds, i, color, brightness);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
          return err;
        }
      } else {
        esp_err_t err = led_strip_spi_set_pixel_brightness(leds, i, color, 0);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
          return err;
        }
      }
    }
  }

  return ESP_OK;
}

esp_err_t RenderRandomPattern(
  led_strip_spi_t*  leds,
  uint8_t           n,
  rgb_t const*      colors,
  uint8_t           color_count,
  uint8_t           brightness,
  PatternContext*   ctx)
{
  if (ctx->clock.now_us - ctx->clock.last_update_us >= kRandomChangeIntervalUs) {
    ctx->clock.last_update_us = ctx->clock.now_us;

    for (int i = 0; i < n; i++) {
      rgb_t color = colors[esp_random() % color_count];
      esp_err_t err = led_strip_spi_set_pixel_brightness(leds, i, color, brightness);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
        return err;
      }
    }
  }
  return ESP_OK;
}
