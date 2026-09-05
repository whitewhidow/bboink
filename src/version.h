// Firmware version + self-update source. Bump BBOINK_VERSION on each release and
// tag it (git tag vX.Y.Z) so CI publishes the matching .bin to the GitHub release
// that BBOINK_OTA_URL ("latest") points at.
#pragma once

// A gitignored core/dev_secrets.h may define DEV_OTHER_FW_URL to point the firmware
// switch at a self-hosted test bin (no CI build / no billing while iterating). Pull
// it here so every unit that includes version.h sees the override.
#if defined(__has_include)
#  if __has_include("core/dev_secrets.h")
#    include "core/dev_secrets.h"
#  endif
#endif

#define BBOINK_VERSION "1.1.2"

// App-only image (firmware.bin) published by CI — this is what an SD-card launcher
// loads (NOT the merged 0x0 image). The self-updater downloads this over WiFi and
// writes it to the SD path configured in Options. Each board pulls its OWN asset
// (different pins/LCD/chip -> different binary), matching the CI env name.
#if defined(PORK_BOARD_TDISPLAY_C5)
#define BBOINK_OTA_BIN "bboink-app-tdisplay-c5.bin"
#elif defined(PORK_BOARD_WAVESHARE_C5_LCD)
#define BBOINK_OTA_BIN "bboink-app-waveshare-c5-lcd.bin"
#elif defined(PORK_BOARD_CARDPUTER_ADV)
#define BBOINK_OTA_BIN "bboink-app-cardputer-adv.bin"
#elif defined(PORK_BOARD_TDONGLE_S3)
#define BBOINK_OTA_BIN "bboink-app-tdongle-s3.bin"
#else
#define BBOINK_OTA_BIN "bboink-app-t-embed-cc1101.bin"
#endif
#define BBOINK_OTA_URL \
    "https://github.com/whitewhidow/bboink/releases/latest/download/" BBOINK_OTA_BIN

// "Switch firmware" target — the sibling project's (hid-ble-poc) app bin for THIS
// same physical board. Same ota_0/ota_1 layout, so it boots from the spare OTA
// slot via the normal updater; whichever you boot becomes the A/B default. Only
// the two 16MB S3 boards carry a PoC counterpart. A gitignored dev_secrets.h may
// define DEV_OTHER_FW_URL to point the switch at a self-hosted test bin instead.
#if defined(PORK_BOARD_TEMBED_CC1101) || defined(PORK_BOARD_TDONGLE_S3)
#define BBOINK_OTHER_FW_NAME "PoC"
#if defined(PORK_BOARD_TDONGLE_S3)
#define BBOINK_OTHER_FW_BIN "hid-ble-poc-app-tdongle.bin"
#else
#define BBOINK_OTHER_FW_BIN "hid-ble-poc-app-tembed.bin"
#endif
#if defined(DEV_OTHER_FW_URL)
#define BBOINK_OTHER_FW_URL DEV_OTHER_FW_URL
#else
#define BBOINK_OTHER_FW_URL \
    "https://github.com/whitewhidow/hid-ble-poc/releases/latest/download/" BBOINK_OTHER_FW_BIN
#endif
#endif
