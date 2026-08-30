// hal/waveshare_c5_lcd_lgfx.h — LovyanGFX device for the Waveshare ESP32-C5-LCD-1.47.
//
// ST7789, 172x320 native, driven landscape as 320x172, on SPI2_HOST. The SD
// card shares SCLK/MOSI with the panel, so bus_shared=true (LovyanGFX releases
// the bus for SD I/O). Backlight is a plain PWM pin (GPIO10). Pins from board.h,
// verified against the Waveshare BSP (waveshareteam/esp32-c5-lcd-1.47).
//
// UNVERIFIED (first hardware bring-up): offset_x=34 (=(240-172)/2 centering on
// the 240-wide ST7789 controller), invert=true and the landscape rotation are
// best-effort — confirm on-device and adjust offset/rotation/invert if the image
// is shifted, mirrored or colour-inverted.
#pragma once
#if defined(PORK_BOARD_WAVESHARE_C5_LCD)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_WaveshareC5 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX_WaveshareC5() {
        {   // SPI bus (SPI2_HOST; MISO -1 on the panel, SD has its own on GPIO5)
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PORK_TFT_SCLK;
            cfg.pin_mosi    = PORK_TFT_MOSI;
            cfg.pin_miso    = PORK_TFT_MISO;   // -1 (unwired on the panel)
            cfg.pin_dc      = PORK_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // ST7789 panel — 172x320 window on a 240x320 controller, landscape.
            auto cfg = _panel.config();
            cfg.pin_cs   = PORK_TFT_CS;
            cfg.pin_rst  = PORK_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width     = 172;
            cfg.panel_height    = 320;
            cfg.offset_x        = 34;    // (240-172)/2 column centering
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable   = false;      // no MISO on the panel
            cfg.invert     = true;       // ST7789 IPS typically needs inversion
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;       // SD shares this SPI bus
            _panel.config(cfg);
        }
        {   // Backlight PWM on GPIO10.
            auto cfg = _light.config();
            cfg.pin_bl      = PORK_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

#endif // PORK_BOARD_WAVESHARE_C5_LCD
