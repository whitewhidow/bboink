// hal/tembed_lgfx.h — LovyanGFX device definition for the LilyGo T-Embed CC1101.
//
// Only compiled for the T-Embed build. Configures the ST7789V (320x170) panel
// on the board's shared SPI bus, plus the backlight on GPIO21. Pin numbers come
// from hal/board.h. Modelled on the LilyGo / Bruce reference setup.
#pragma once
#if defined(PORK_BOARD_TEMBED_CC1101)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_TEmbed : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

public:
    LGFX_TEmbed() {
        {   // SPI bus
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;   // FSPI on ESP32-S3
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = PORK_TFT_SCLK;
            cfg.pin_mosi   = PORK_TFT_MOSI;
            cfg.pin_miso   = PORK_TFT_MISO;
            cfg.pin_dc     = PORK_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // ST7789 panel
            auto cfg = _panel.config();
            cfg.pin_cs   = PORK_TFT_CS;
            cfg.pin_rst  = PORK_TFT_RST;
            cfg.pin_busy = -1;

            // 320x170 visible window on a 240x320 ST7789 controller, landscape.
            cfg.panel_width    = 170;
            cfg.panel_height   = 320;
            cfg.offset_x       = 35;   // (240-170)/2 centering offset
            cfg.offset_y       = 0;
            cfg.offset_rotation = 0;
            cfg.readable   = false;
            cfg.invert     = true;     // ST7789 IPS needs inversion
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;     // SD shares this bus
            _panel.config(cfg);
        }
        {   // Backlight
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

#endif // PORK_BOARD_TEMBED_CC1101
