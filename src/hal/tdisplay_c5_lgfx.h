// hal/tdisplay_c5_lgfx.h — LovyanGFX device definition for the LilyGO T-Display C5.
//
// Only compiled for the T-Display C5 build. Configures the ST7789 (170x320
// native, driven landscape as 320x170) on SPI2_HOST, plus the backlight/power
// enable on GPIO25. Pin numbers come from hal/board.h. Modelled on hal/
// tembed_lgfx.h and the LilyGO Xinyuan-LilyGO/T-Display-C5 factory example.
//
// UNVERIFIED (no hardware): offset_x=35 / offset_y=0 / invert=true and the
// landscape rotation are taken from the LilyGO example + the T-Embed profile;
// confirm on-device. This board has NO MISO and NO shared SD, so the SPI bus is
// not shared (bus_shared=false).
#pragma once
#if defined(PORK_BOARD_TDISPLAY_C5)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_C5 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

public:
    LGFX_C5() {
        {   // SPI bus (SPI2_HOST, no MISO on this board)
            auto cfg = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = PORK_TFT_SCLK;
            cfg.pin_mosi   = PORK_TFT_MOSI;
            cfg.pin_miso   = PORK_TFT_MISO;   // -1 (unwired)
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
            cfg.offset_x       = 35;   // (240-170)/2 centering offset (LilyGO example)
            cfg.offset_y       = 0;
            cfg.offset_rotation = 0;
            cfg.readable   = false;    // no MISO
            cfg.invert     = true;     // ST7789 IPS needs inversion (LilyGO example)
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;    // no SD/CC1101 sharing this bus
            _panel.config(cfg);
        }
        {   // Backlight / power-enable on GPIO25. Light_PWM gives brightness
            // control; the pin is a simple enable on this board but PWM on it is
            // harmless. bringUpHardware() also drives it HIGH before init().
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

#endif // PORK_BOARD_TDISPLAY_C5
