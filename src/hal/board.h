// hal/board.h — board hardware abstraction (pins + screen geometry)
//
// Selected at compile time by a PlatformIO build flag:
//   -DPORK_BOARD_CARDPUTER        (M5Cardputer, the original target)
//   -DPORK_BOARD_TEMBED_CC1101    (LilyGo T-Embed CC1101 / CC1101 Plus)
//   -DPORK_BOARD_TDISPLAY_C5      (LilyGO T-Display C5 / ESP32-C5)
//
// This is the single source of truth for board-specific pin numbers and the
// display layout grid. Everything else should derive its geometry from the
// PORK_* macros below rather than hardcoding 240/135.
#pragma once

#if !defined(PORK_BOARD_CARDPUTER) && !defined(PORK_BOARD_TEMBED_CC1101) && \
    !defined(PORK_BOARD_TDISPLAY_C5) && !defined(PORK_BOARD_WAVESHARE_C5_LCD) && \
    !defined(PORK_BOARD_CARDPUTER_ADV)
// Default to the original board so legacy builds are unaffected.
#define PORK_BOARD_CARDPUTER
#endif

// ---------------------------------------------------------------------------
#if defined(PORK_BOARD_CARDPUTER)
// ---------------------------------------------------------------------------
// M5Cardputer (ESP32-S3 StampS3). Pins below document the real hardware; the
// M5Cardputer/M5Unified libraries actually drive them, so we only surface the
// values that the firmware's own code references.

#define PORK_DISPLAY_W      240
#define PORK_DISPLAY_H      135
#define PORK_TOP_BAR_H      14
#define PORK_BOTTOM_BAR_H   14

// RGB LED (single WS2812 / NeoPixel on GPIO21, driven via neopixelWrite()).
#define PORK_LED_PIN        21
#define PORK_LED_COUNT      1

// microSD (FSPI bus, dedicated pins on the Cardputer).
#define PORK_SD_SCK         40
#define PORK_SD_MISO        39
#define PORK_SD_MOSI        14
#define PORK_SD_CS          12

// ---------------------------------------------------------------------------
#elif defined(PORK_BOARD_TEMBED_CC1101)
// ---------------------------------------------------------------------------
// LilyGo T-Embed CC1101 (ESP32-S3, 16MB flash, 8MB OPI PSRAM).
// Display: ST7789V 320x170 IPS, rotary encoder + side button instead of a
// keyboard, 8x WS2812 LEDs, I2S speaker, CC1101 (unused by this firmware).
//
// NOTE: these pin numbers are taken from the LilyGo / Bruce reference designs
// for this board and MUST be confirmed against the schematic of your exact
// unit before trusting on-device SD writes (see plan "open items").

#define PORK_DISPLAY_W      320
#define PORK_DISPLAY_H      170
// Slightly taller bars: the 320x170 panel has the vertical room and the
// default 6px font looks cramped at 14px on the larger panel.
#define PORK_TOP_BAR_H      18
#define PORK_BOTTOM_BAR_H   18

// Display (ST7789) on the shared SPI bus.
#define PORK_TFT_SCLK       11
#define PORK_TFT_MOSI       9
#define PORK_TFT_MISO       10
#define PORK_TFT_CS         41
#define PORK_TFT_DC         16
#define PORK_TFT_RST        40
#define PORK_TFT_BL         21   // backlight (PWM-capable)

// 8x WS2812 addressable LEDs.
#define PORK_LED_PIN        14
#define PORK_LED_COUNT      8

// microSD shares the display SPI bus (SCLK/MOSI/MISO), dedicated CS.
#define PORK_SD_SCK         11
#define PORK_SD_MISO        10
#define PORK_SD_MOSI        9
#define PORK_SD_CS          13

// CC1101 sub-GHz radio shares the SAME SPI bus (SCLK/MOSI/MISO) with a
// dedicated chip-select. This firmware doesn't use the radio, but its CS MUST be
// driven HIGH (deselected) before SD access or the CC1101 holds the shared MISO
// line and the SD mount fails ("physical drive cannot work"). Pins per LilyGo /
// Bruce T_EMBED_1101 reference (CC1101_SS_PIN 12, GDO0 3, GDO2 38).
#define PORK_CC1101_CS      12
#define PORK_CC1101_GDO0    3
#define PORK_CC1101_GDO2    38

// I2S speaker (MAX98357A). Pins per Bruce's lilygo-t-embed-cc1101 reference
// (BCLK 46, word-select 40, data-out 7). NOTE: WS (40) is the same net as the
// display RST — RST is only pulsed once at display init, so the I2S driver is
// installed only for the duration of a tone and uninstalled after, leaving pin
// 40 released (display RST inactive) the rest of the time.
#define PORK_I2S_BCLK       46
#define PORK_I2S_WS         40
#define PORK_I2S_DOUT       7

// Rotary encoder + buttons (the keyboard replacement).
#define PORK_ENC_A          4
#define PORK_ENC_B          5
#define PORK_ENC_KEY        0    // encoder push (also boot strap pin)
#define PORK_BTN_BACK       6    // dedicated side button

// I2C bus (BQ27220 fuel gauge @ 0x55, BQ25896 charger @ 0x6B, PN532 NFC).
// TODO: confirm SDA/SCL against the schematic of your unit.
#define PORK_I2C_SDA        8
#define PORK_I2C_SCL        18
#define PORK_BQ27220_ADDR   0x55

// ---------------------------------------------------------------------------
#elif defined(PORK_BOARD_TDISPLAY_C5)
// ---------------------------------------------------------------------------
// LilyGO T-Display C5 (ESP32-C5, 16MB flash, 8MB PSRAM). Dual-band WiFi.
// Display: ST7789 170x320 native IPS, driven in landscape (320x170, same
// geometry as the T-Embed). AXP2602 PMU + CST816S capacitive touch on a shared
// I2C bus, plus two buttons. NO microSD, NO WS2812 LED, NO I2S speaker, NO
// CC1101. Pin numbers are from the LilyGO Xinyuan-LilyGO/T-Display-C5 example.
//
// UNVERIFIED (no hardware): the AXP2602 may gate display power — see
// bringUpHardware() in hal/m5compat.cpp. At minimum GPIO25 is driven HIGH.

#define PORK_DISPLAY_W      320
#define PORK_DISPLAY_H      170
#define PORK_TOP_BAR_H      18
#define PORK_BOTTOM_BAR_H   18

// Display (ST7789) on SPI2_HOST. This board has NO MISO wired to the panel.
#define PORK_TFT_SCLK       7
#define PORK_TFT_MOSI       9
#define PORK_TFT_MISO       -1
#define PORK_TFT_CS         26
#define PORK_TFT_DC         8
#define PORK_TFT_RST        23
#define PORK_TFT_BL         25   // LCD_BLK_POWER — backlight/power enable, drive HIGH

// No addressable RGB LED on this board.
#define PORK_LED_COUNT      0

// microSD: NONE on this board. Defined as -1 so the shared SD code in
// core/config.cpp still compiles; the SD probe is skipped for this board and
// captures fall back to internal LittleFS (see Config::init()).
#define PORK_SD_SCK         -1
#define PORK_SD_MISO        -1
#define PORK_SD_MOSI        -1
#define PORK_SD_CS          -1

// Buttons (keyboard replacement): GPIO0 (BOOT strap) + GPIO28 (BOOT2).
// GPIO0  -> short click = ENTER, long press = power-off gesture.
// GPIO28 -> BACK.
#define PORK_ENC_KEY        0    // primary button (also BOOT strap pin)
#define PORK_BTN_BACK       28   // secondary button

// I2C bus shared by the AXP2602 PMU and the CST816S touch controller.
#define PORK_I2C_SDA        2
#define PORK_I2C_SCL        3
#define PORK_AXP_INT        10
// TODO(hardware): confirm AXP2602 I2C address + LDO map before enabling PMU
// reads. Not currently used (PowerFacade stubs 100% / 4.0V on this board).
#define PORK_AXP_ADDR       0x34

// CST816S capacitive touch (primary input).
#define PORK_TP_INT         27
#define PORK_TP_RST         24
#define PORK_TP_ADDR        0x15

#elif defined(PORK_BOARD_WAVESHARE_C5_LCD)
// ---------------------------------------------------------------------------
// Waveshare ESP32-C5-LCD-1.47 (ESP32-C5, 4MB flash). Dual-band WiFi 6.
// Display: ST7789 172x320 native IPS, driven landscape as 320x172. ONE usable
// button (BOOT = GPIO28; the second physical button is RESET = chip-enable, not
// a readable GPIO). Onboard WS2812 (GPIO8) + microSD (shares the LCD SPI bus).
// NO speaker, NO touch, NO PMU/battery. Pins verified from the Waveshare BSP
// (waveshareteam/esp32-c5-lcd-1.47) + board schematic. See
// docs/DESIGN-mode-webui.md (single-button = mode toggle; management via web UI).

#define PORK_DISPLAY_W      320
#define PORK_DISPLAY_H      172
#define PORK_TOP_BAR_H      18
#define PORK_BOTTOM_BAR_H   18

// Display (ST7789) on SPI2_HOST. No MISO wired to the panel; the SD's MISO
// (GPIO5) lives on the same bus with its own CS.
#define PORK_TFT_SCLK       7
#define PORK_TFT_MOSI       6
#define PORK_TFT_MISO       -1
#define PORK_TFT_CS         23
#define PORK_TFT_DC         24
#define PORK_TFT_RST        26
#define PORK_TFT_BL         10   // backlight (PWM), driven by LovyanGFX Light_PWM

// Onboard WS2812 status LED (single).
#define PORK_LED_PIN        8
#define PORK_LED_COUNT      1

// microSD shares the display SPI bus (SCLK 7 / MOSI 6) with its own MISO + CS.
// bus_shared=true in the LGFX profile so LovyanGFX releases the bus for SD I/O.
#define PORK_SD_SCK         7
#define PORK_SD_MISO        5
#define PORK_SD_MOSI        6
#define PORK_SD_CS          4

// Single usable button: BOOT on GPIO28. Tap = CAPTURE<->MANAGEMENT toggle,
// long-press = power off (see hal/m5compat.cpp Waveshare input backend).
#define PORK_ENC_KEY        28   // the one button (also the C5 boot strap pin)
#define PORK_BTN_BACK       28   // same physical button (no second input)

// ---------------------------------------------------------------------------
#elif defined(PORK_BOARD_CARDPUTER_ADV)
// ---------------------------------------------------------------------------
// M5Cardputer ADV (ESP32-S3). Brought up like the Waveshare single-button profile:
// LovyanGFX drives the ST7789 DIRECTLY (NOT M5GFX/M5Unified), on-device menus off,
// management over the BLE console. Only the top "G0" button (GPIO0) is read — the
// ADV's 56-key TCA8418 I2C keyboard is intentionally IGNORED for v1 (add it in a v2
// for on-device menus). Display pins are the Cardputer/CardputerADV values from
// M5GFX autodetect (identical for both boards). UNVERIFIED until hardware bring-up.
//
// Display: ST7789, 135x240 native, driven landscape as 240x135 (rotation 1).
#define PORK_DISPLAY_W      240
#define PORK_DISPLAY_H      135
#define PORK_TOP_BAR_H      14
#define PORK_BOTTOM_BAR_H   14

// Display (ST7789) on SPI3_HOST. No MISO on the panel. SD is a SEPARATE FSPI bus.
#define PORK_TFT_SCLK       36
#define PORK_TFT_MOSI       35
#define PORK_TFT_MISO       -1
#define PORK_TFT_CS         37
#define PORK_TFT_DC         34
#define PORK_TFT_RST        33
#define PORK_TFT_BL         38   // backlight (PWM), driven by LovyanGFX Light_PWM

// Onboard WS2812 status LED (single, GPIO21).
#define PORK_LED_PIN        21
#define PORK_LED_COUNT      1

// microSD on its own FSPI bus (NOT shared with the display).
#define PORK_SD_SCK         40
#define PORK_SD_MISO        39
#define PORK_SD_MOSI        14
#define PORK_SD_CS          12

// Single usable input for v1: the top "G0" button on GPIO0 (BOOT strap + user
// button). Tap = CAPTURE<->MANAGEMENT toggle, like the Waveshare. Held at reset =
// download mode (strap pin), so only runtime presses are usable.
#define PORK_ENC_KEY        0
#define PORK_BTN_BACK       0

#endif
// ---------------------------------------------------------------------------

// Both C5 boards (T-Display C5 + Waveshare C5-LCD) are the dual-band ESP32-C5 and
// share the same capture path (band-switching, promiscuous filter mask, dual-band
// channel hop list, dwell floor). Chip-level code gates on this, NOT on a single
// board macro — otherwise a new C5 board silently runs the S3 capture path and
// never captures DATA/EAPOL frames.
#if defined(PORK_BOARD_TDISPLAY_C5) || defined(PORK_BOARD_WAVESHARE_C5_LCD)
#define PORK_CHIP_ESP32C5
#endif
// C5 capture path is 2.4 GHz for all C5 boards. 5 GHz (set_band to 5G + the UNII
// hop channels) needs regulatory/country config for reception and is currently
// only enabled on the verified T-Display C5 — the Waveshare stays 2.4 GHz until
// 5 GHz reception (esp_wifi_set_country) and the 5 GHz-hop reboot are sorted.
// Both C5 boards now set the regulatory country (esp_wifi_set_country_code) in the
// capture path, which is what the 5 GHz channels need to not fault at ch36.
#if defined(PORK_BOARD_TDISPLAY_C5) || defined(PORK_BOARD_WAVESHARE_C5_LCD)
#define PORK_C5_5GHZ
#endif

// Derived main content height — common to all boards.
#define PORK_MAIN_H (PORK_DISPLAY_H - PORK_TOP_BAR_H - PORK_BOTTOM_BAR_H)
