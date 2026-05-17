#include "sk9822_leds.hpp"

#include <string.h>
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "sk9822";
static spi_device_handle_t s_spi_handle;

// ---------------------------------------------------------------------------
// Driver init
// ---------------------------------------------------------------------------

esp_err_t SK9822_Leds_Init(gpio_num_t clk_pin, gpio_num_t mosi_pin)
{
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = mosi_pin;
    buscfg.sclk_io_num = clk_pin;
    buscfg.max_transfer_sz = 4096;

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 1 * 1000 * 1000; // 1 MHz
    devcfg.mode = 0;
    devcfg.spics_io_num = -1; // SK9822 uses start/end frames instead of CS
    devcfg.queue_size = 1;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
    }

    return err;
}

// ---------------------------------------------------------------------------
// Animation helpers
// ---------------------------------------------------------------------------

void OffPattern::Prepare(SK9822_Led *leds, int count, uint32_t now)
{
    SK9822_Led_PrepareAnimation_Off(leds, count);
}

void SolidRedPattern::Prepare(SK9822_Led *leds, int count, uint32_t now)
{
    for (int i = 0; i < count; i++)
    {
        leds[i].Brightness = brightness_;
        leds[i].Red = 255;
        leds[i].Green = 0;
        leds[i].Blue = 0;
    }
}

void ChasePattern::Prepare(SK9822_Led *leds, int count, uint32_t now)
{
    if (now - last_update_ < interval_)
    {
        return;
    }

    last_update_ = now;

    SK9822_Led_PrepareAnimation_Off(leds, count);

    leds[index_].Brightness = brightness_;
    leds[index_].Red = 255;
    leds[index_].Green = 0;
    leds[index_].Blue = 0;

    if (forward_)
    {
        if (++index_ > count - 1)
        {
            index_ = count - 2;
            forward_ = false;
        }
    }
    else
    {
        if (--index_ < 0)
        {
            index_ = 1;
            forward_ = true;
        }
    }
}

void RainbowPattern::Prepare(SK9822_Led *leds, int count, uint32_t now)
{
    SK9822_Led_PrepareAnimation_Rainbow(leds, count, frame_);
    frame_++;
}

void PulsePattern::Prepare(SK9822_Led *leds, int count, uint32_t now)
{
    SK9822_Led_PrepareAnimation_Pulse(leds, count, frame_);
    frame_++;
}

void SK9822_Led_PrepareAnimation_Off(SK9822_Led *strip, int led_count)
{
    for (int i = 0; i < led_count; i++)
    {
        strip[i].Brightness = 0;
        strip[i].Red = 0;
        strip[i].Green = 0;
        strip[i].Blue = 0;
    }
}

void SK9822_Led_PrepareAnimation_SolidWhite(SK9822_Led *strip, int led_count)
{

}

void SK9822_Led_PrepareAnimation_Rainbow(SK9822_Led *strip, int led_count, uint32_t frame)
{
    for (int i = 0; i < led_count; i++)
    {
        uint8_t hue = (uint8_t)((frame + (uint32_t)i * 256U / (uint32_t)led_count) & 0xFFU);

        strip[i].Brightness = 31;

        if (hue < 85)
        {
            strip[i].Red = 255U - hue * 3U;
            strip[i].Green = hue * 3U;
            strip[i].Blue = 0;
        }
        else if (hue < 170)
        {
            strip[i].Red = 0;
            strip[i].Green = 255U - (hue - 85U) * 3U;
            strip[i].Blue = (hue - 85U) * 3U;
        }
        else
        {
            strip[i].Red = (hue - 170U) * 3U;
            strip[i].Green = 0;
            strip[i].Blue = 255U - (hue - 170U) * 3U;
        }
    }
}

void SK9822_Led_PrepareAnimation_Pulse(SK9822_Led *strip, int led_count, uint32_t frame)
{
    uint8_t brightness = (uint8_t)(frame & 0x1FU);

    if (frame & 0x20U)
    {
        brightness = 31U - brightness;
    }

    for (int i = 0; i < led_count; i++)
    {
        strip[i].Brightness = brightness;
        strip[i].Red = 0;
        strip[i].Green = 0;
        strip[i].Blue = 255;
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void SK9822_Led_Render(const SK9822_Led *strip, uint8_t *tx_buffer, int led_count)
{
    // SK9822 protocol: 4-byte start frame, one 4-byte frame per LED, 4-byte end frame
    int idx = 0;

    tx_buffer[idx++] = 0x00;
    tx_buffer[idx++] = 0x00;
    tx_buffer[idx++] = 0x00;
    tx_buffer[idx++] = 0x00;

    for (int i = 0; i < led_count; i++)
    {
        tx_buffer[idx++] = 0xE0U | (strip[i].Brightness & 0x1FU);
        tx_buffer[idx++] = strip[i].Blue;
        tx_buffer[idx++] = strip[i].Green;
        tx_buffer[idx++] = strip[i].Red;
    }

    tx_buffer[idx++] = 0xFF;
    tx_buffer[idx++] = 0xFF;
    tx_buffer[idx++] = 0xFF;
    tx_buffer[idx++] = 0xFF;

    spi_transaction_t t = {};
    t.length = 8 * (size_t)SK9822_TX_BUFFER_SIZE(led_count);
    t.tx_buffer = tx_buffer;

    spi_device_transmit(s_spi_handle, &t);
}
