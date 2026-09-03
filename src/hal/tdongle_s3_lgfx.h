// hal/tdongle_s3_lgfx.h — LovyanGFX device for the LilyGo T-Dongle S3.
//
// ST7735S 80x160 native, driven landscape as 160x80 (rotation set at bring-up).
// Pins/offsets from the LilyGo T-Dongle-S3 pin_config.h (SCLK5 MOSI3 CS4 DC2 RST1
// BL38, offset 26/1, invert). UNVERIFIED until first hardware bring-up — confirm
// the offset/rotation/invert/colour-order on-device and adjust here.
#pragma once
#if defined(PORK_BOARD_TDONGLE_S3)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_TDongleS3 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;

public:
    LGFX_TDongleS3() {
        {   // SPI bus (panel-only; no CC1101/SD sharing on this board)
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 27000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PORK_TFT_SCLK;
            cfg.pin_mosi    = PORK_TFT_MOSI;
            cfg.pin_miso    = PORK_TFT_MISO;   // -1
            cfg.pin_dc      = PORK_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // ST7735S — 80x160, offset 26/1, inverted
            auto cfg = _panel.config();
            cfg.pin_cs   = PORK_TFT_CS;
            cfg.pin_rst  = PORK_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width     = 80;
            cfg.panel_height    = 160;
            cfg.offset_x        = 26;
            cfg.offset_y        = 1;
            cfg.offset_rotation = 0;
            cfg.readable   = false;
            cfg.invert     = true;
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
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

#endif // PORK_BOARD_TDONGLE_S3
