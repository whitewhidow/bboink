// hal/cardputer_adv_lgfx.h — LovyanGFX device for the M5Cardputer ADV.
//
// ST7789, 135x240 native, driven landscape as 240x135 on SPI3_HOST. Pins are the
// Cardputer / CardputerADV values from M5GFX autodetect (IDENTICAL for both — the
// ADV shares the original Cardputer panel): MOSI35 SCLK36 (no MISO) DC34 CS37 RST33
// BL38, panel offset 52/40, invert. The microSD is on a SEPARATE FSPI bus
// (SCK40/MISO39/MOSI14/CS12), so bus_shared=false (no bus juggling with the panel).
//
// Sourced from M5GFX v1 board_M5CardputerADV autodetect (Panel_ST7789, SPI3_HOST,
// freq_write 40MHz, offset_x 52 / offset_y 40, setRotation(1), backlight PWM GPIO38).
//
// UNVERIFIED (first hardware bring-up): confirm rotation/offset/invert on-device;
// adjust if the image is shifted, mirrored or colour-inverted. The app applies the
// landscape rotation (1) at display init, matching M5GFX's setRotation(1).
#pragma once
#if defined(PORK_BOARD_CARDPUTER_ADV)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_CardputerADV : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX_CardputerADV() {
        {   // SPI bus (SPI3_HOST; panel has no MISO; SD lives on its own FSPI bus)
            auto cfg = _bus.config();
            cfg.spi_host    = SPI3_HOST;   // per M5GFX board_M5CardputerADV autodetect
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = 0;   // NO DMA — avoids the display SPI-DMA vs WiFi GDMA race on the S3
                                   // (boot/capture hangs). Slower blocking writes, fine for a status UI.
            cfg.pin_sclk    = PORK_TFT_SCLK;
            cfg.pin_mosi    = PORK_TFT_MOSI;
            cfg.pin_miso    = PORK_TFT_MISO;   // -1 (unwired on the panel)
            cfg.pin_dc      = PORK_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // ST7789 — 135x240 window on a 240x320 controller, landscape (rot 1).
            auto cfg = _panel.config();
            cfg.pin_cs   = PORK_TFT_CS;
            cfg.pin_rst  = PORK_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width     = 135;
            cfg.panel_height    = 240;
            cfg.offset_x        = 52;
            cfg.offset_y        = 40;
            cfg.offset_rotation = 0;
            cfg.readable   = false;      // no MISO on the panel
            cfg.invert     = true;       // Cardputer ST7789 needs inversion
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;      // SD is on a separate FSPI bus
            _panel.config(cfg);
        }
        {   // Backlight PWM on GPIO38.
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

#endif // PORK_BOARD_CARDPUTER_ADV
