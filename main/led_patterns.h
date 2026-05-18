#pragma once

#include "sk9822_leds.hpp"

class OffPattern final : public LedDesign
{
public:
  void Apply(SK9822_Led* leds, int count, uint32_t now) override;
};

class SolidColorPattern final : public LedDesign
{
public:
  SolidColorPattern(uint8_t red, uint8_t green, uint8_t blue)
    : red_(red)
    , green_(green)
    , blue_(blue) { }

  void Apply(SK9822_Led* leds, int count, uint32_t now) override;

private:
  uint8_t red_;
  uint8_t green_;
  uint8_t blue_;
};

class ChasePattern final : public LedDesign
{
public:
  void Apply(SK9822_Led* leds, int count, uint32_t now) override;

private:
  bool forward_ = true;
  uint32_t last_update_ = 0;
  uint32_t interval_ = 100;
  int index_ = 0;
};

class PulsePattern final : public LedDesign
{
public:
  void Apply(SK9822_Led* leds, int count, uint32_t now) override;

private:
  uint32_t frame_ = 0;
};

class StrobePattern final : public LedDesign
{
public:
  void Apply(SK9822_Led* leds, int count, uint32_t now) override;

private:
  uint32_t last_update_ = 0;
  bool on_ = false;
  uint32_t on_ms_ = 50;
  uint32_t off_ms_ = 150;
};

class TwinklePattern final : public LedDesign
{
public:
  void Apply(SK9822_Led* leds, int count, uint32_t now) override;

private:
  uint32_t last_update_ = 0;
  uint32_t interval_ = 120;
};

class RandomPattern final : public LedDesign
{
public:
  // colors must outlive this object (caller owns the array).
  RandomPattern(const SK9822_Led* colors, int num_colors, uint32_t interval_ms)
    : colors_(colors)
    , num_colors_(num_colors)
    , interval_(interval_ms)
  {
  }

  void Apply(SK9822_Led* leds, int count, uint32_t now) override;

private:
  const SK9822_Led* colors_;
  int num_colors_;
  uint32_t interval_;
  uint32_t last_update_ = 0;
};