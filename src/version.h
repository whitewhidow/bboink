// Firmware version + self-update source. Bump BBOINK_VERSION on each release and
// tag it (git tag vX.Y.Z) so CI publishes the matching .bin to the GitHub release
// that BBOINK_OTA_URL ("latest") points at.
#pragma once

#define BBOINK_VERSION "0.9.22"

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
