#include "LedPattern.h"

#include <esp_log.h>
#include <esp_random.h>

namespace {

const char* TAG = "LedPattern";

constexpr int64_t kChaseIntervalUs = 100 * 1000;
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

}  // namespace

esp_err_t RenderSolidColor(led_strip_spi_t* leds, uint8_t n, rgb_t color, uint8_t brightness)
{
  return led_strip_spi_set_pixels_brightness(leds, 0, n, color, brightness);
}

// ---------------------------------------------------------------------------
// SolidColorRenderer
// ---------------------------------------------------------------------------

SolidColorRenderer::SolidColorRenderer()
  : color(kColorRed), brightness(kDefaultBrightness)
{
}

esp_err_t SolidColorRenderer::Render(led_strip_spi_t* leds, uint8_t n, int64_t /*now_us*/)
{
  return RenderSolidColor(leds, n, color, brightness);
}

// ---------------------------------------------------------------------------
// ChaseRenderer
// ---------------------------------------------------------------------------

ChaseRenderer::ChaseRenderer()
  : color(kColorRed), brightness(kDefaultBrightness)
{
}

esp_err_t ChaseRenderer::Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us)
{
  if (now_us - last_update_us_ >= kChaseIntervalUs) {
    last_update_us_ = now_us;

    for (int i = 0; i < n; i++) {
      if (i == chase_index_) {
        ESP_ERROR_CHECK(led_strip_spi_set_pixel_brightness(leds, i, color, brightness));
      } else {
        ESP_ERROR_CHECK(led_strip_spi_set_pixel_brightness(leds, i, color, 0));
      }
    }

    if (chase_forward_) {
      chase_index_++;
      if (chase_index_ >= n) {
        chase_index_ = n - 2;
        chase_forward_ = false;
      }
    } else {
      chase_index_--;
      if (chase_index_ < 0) {
        chase_index_ = 1;
        chase_forward_ = true;
      }
    }
  }
  return ESP_OK;
}

// ---------------------------------------------------------------------------
// PulseRenderer
// ---------------------------------------------------------------------------

PulseRenderer::PulseRenderer()
  : color(kColorRed), max_brightness(kDefaultBrightness)
{
}

esp_err_t PulseRenderer::Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us)
{
  constexpr int64_t kPulsePeriodUs = 2'000'000;

  int64_t cycle_us = now_us % kPulsePeriodUs;

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

// ---------------------------------------------------------------------------
// StrobeRenderer
// ---------------------------------------------------------------------------

StrobeRenderer::StrobeRenderer()
  : color(kColorRed), brightness(kDefaultBrightness)
{
}

esp_err_t StrobeRenderer::Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us)
{
  if (strobe_on_ && (now_us - last_update_us_ >= kStrobeOnDurationUs)) {
    // turn off
    esp_err_t err = led_strip_spi_set_pixels_brightness(leds, 0, n, color, 0);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
      return err;
    }
    strobe_on_ = false;
    last_update_us_ = now_us;
  } else if (!strobe_on_ && (now_us - last_update_us_ >= kStrobeOffDurationUs)) {
    // turn on
    esp_err_t err = led_strip_spi_set_pixels_brightness(leds, 0, n, color, brightness);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
      return err;
    }
    strobe_on_ = true;
    last_update_us_ = now_us;
  }

  return ESP_OK;
}

// ---------------------------------------------------------------------------
// TwinkleRenderer
// ---------------------------------------------------------------------------

TwinkleRenderer::TwinkleRenderer()
  : color(kColorRed), brightness(kDefaultBrightness)
{
}

esp_err_t TwinkleRenderer::Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us)
{
  if (now_us - last_update_us_ >= kTwinkleIntervalUs) {
    last_update_us_ = now_us;

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

// ---------------------------------------------------------------------------
// RandomRenderer
// ---------------------------------------------------------------------------

RandomRenderer::RandomRenderer()
  : brightness(30), palette_(kInstigatorColors)
{
}

esp_err_t RandomRenderer::Render(led_strip_spi_t* leds, uint8_t n, int64_t now_us)
{
  if (now_us - last_update_us_ >= kRandomChangeIntervalUs) {
    last_update_us_ = now_us;

    for (int i = 0; i < n; i++) {
      rgb_t color = palette_[esp_random() % palette_.size()];
      esp_err_t err = led_strip_spi_set_pixel_brightness(leds, i, color, brightness);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set pixel color: %s", esp_err_to_name(err));
        return err;
      }
    }
  }
  return ESP_OK;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

PatternRenderer MakePatternRenderer(LedPattern pattern)
{
  switch (pattern) {
    case LedPattern::kSolidRed: return SolidColorRenderer{};
    case LedPattern::kChase:    return ChaseRenderer{};
    case LedPattern::kPulse:    return PulseRenderer{};
    case LedPattern::kStrobe:   return StrobeRenderer{};
    case LedPattern::kTwinkle:  return TwinkleRenderer{};
    case LedPattern::kRandom:   return RandomRenderer{};
  }
  return SolidColorRenderer{};
}

esp_err_t RenderLedPattern(PatternRenderer& renderer, led_strip_spi_t* leds, uint8_t n, int64_t now_us)
{
  esp_err_t err = std::visit(
    [&](auto& active_renderer) { return active_renderer.Render(leds, n, now_us); },
    renderer);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to render pattern: %s", esp_err_to_name(err));
    return err;
  }

  return led_strip_spi_flush(leds);
}
