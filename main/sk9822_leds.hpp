#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// SK9822 single LED pixel
// ---------------------------------------------------------------------------

struct SK9822_Led
{
    uint8_t Brightness; // 0-31 (5 bits; top 3 bits of the wire frame are always 111)
    uint8_t Blue;
    uint8_t Green;
    uint8_t Red;
};

// Byte length of the SPI tx buffer for a strip of N LEDs.
#define SK9822_TX_BUFFER_SIZE(n) (4 + ((n) * 4) + 4)

// ---------------------------------------------------------------------------
// Driver init
// ---------------------------------------------------------------------------

// Initialise the SPI bus and attach the SK9822 device.
// Must be called once before any Render call.
esp_err_t SK9822_Leds_Init(gpio_num_t clk_pin, gpio_num_t mosi_pin);

// ---------------------------------------------------------------------------
// Low-level free functions  (implemented in sk9822_leds.cpp)
// These operate on raw arrays and are the building blocks for SK9822_LedStrip.
// ---------------------------------------------------------------------------

void SK9822_Led_PrepareAnimation_Off(SK9822_Led *leds, int count);
void SK9822_Led_PrepareAnimation_Chase(SK9822_Led *leds, int count, uint32_t frame);
void SK9822_Led_PrepareAnimation_Rainbow(SK9822_Led *leds, int count, uint32_t frame);
void SK9822_Led_PrepareAnimation_Pulse(SK9822_Led *leds, int count, uint32_t frame);
void SK9822_Led_Render(const SK9822_Led *leds, uint8_t *tx_buffer, int count);

// ---------------------------------------------------------------------------
// SK9822_LedStrip<N>
//
// LED count is a compile-time template parameter. The LED array and SPI tx
// buffer are embedded members — no heap allocation, no external pointers.
// ---------------------------------------------------------------------------

class LedPattern
{
public:
    virtual ~LedPattern() = default;
    virtual void Prepare(SK9822_Led *leds, int count, uint32_t now) = 0;

    void SetBrightness(uint8_t brightness)
        { brightness_ = brightness; }

    uint8_t Brightness() const
        { return brightness_; }

protected:
    uint8_t brightness_ = 20;
};

class OffPattern final : public LedPattern
{
public:
    void Prepare(SK9822_Led *leds, int count, uint32_t now) override;
};

class SolidRedPattern final : public LedPattern
{
public:
    void Prepare(SK9822_Led *leds, int count, uint32_t now) override;
private:
};

class ChasePattern final : public LedPattern
{
public:
    void Prepare(SK9822_Led *leds, int count, uint32_t now) override;

private:
    bool forward_ = true;
    uint32_t last_update_ = 0;
    uint32_t interval_ = 100;
    int index_ = 0;
};

class RainbowPattern final : public LedPattern
{
public:
    void Prepare(SK9822_Led *leds, int count, uint32_t now) override;

private:
    uint32_t frame_ = 0;
};

class PulsePattern final : public LedPattern
{
public:
    void Prepare(SK9822_Led *leds, int count, uint32_t now) override;

private:
    uint32_t frame_ = 0;
};

template<int N>
class SK9822_LedStrip
{
public:
    SK9822_LedStrip() = default;

    static constexpr int kLedCount = N;

    void Clear()
        { SK9822_Led_PrepareAnimation_Off(leds_, kLedCount); }

    void Prepare(LedPattern &pattern, uint32_t now)
        { pattern.Prepare(leds_, kLedCount, now); }

    void Render()
        { SK9822_Led_Render(leds_, tx_buffer_, kLedCount); }

private:
    SK9822_Led leds_[kLedCount];
    uint8_t tx_buffer_[SK9822_TX_BUFFER_SIZE(kLedCount)];
};
