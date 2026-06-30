// hal/board.h — board hardware abstraction (pins + screen geometry)
//
// Selected at compile time by a PlatformIO build flag:
//   -DPORK_BOARD_CARDPUTER        (M5Cardputer, the original target)
//   -DPORK_BOARD_TEMBED_CC1101    (LilyGo T-Embed CC1101 / CC1101 Plus)
//
// This is the single source of truth for board-specific pin numbers and the
// display layout grid. Everything else should derive its geometry from the
// PORK_* macros below rather than hardcoding 240/135.
#pragma once

#if !defined(PORK_BOARD_CARDPUTER) && !defined(PORK_BOARD_TEMBED_CC1101)
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

#endif
// ---------------------------------------------------------------------------

// Derived main content height — common to all boards.
#define PORK_MAIN_H (PORK_DISPLAY_H - PORK_TOP_BAR_H - PORK_BOTTOM_BAR_H)
