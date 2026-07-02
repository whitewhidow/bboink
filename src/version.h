// Firmware version + self-update source. Bump BBOINK_VERSION on each release and
// tag it (git tag vX.Y.Z) so CI publishes the matching .bin to the GitHub release
// that BBOINK_OTA_URL ("latest") points at.
#pragma once

#define BBOINK_VERSION "0.9.1"

// App-only image (firmware.bin) published by CI — this is what an SD-card launcher
// loads (NOT the merged 0x0 image). The self-updater downloads this over WiFi and
// writes it to the SD path configured in Options.
#define BBOINK_OTA_URL \
    "https://github.com/whitewhidow/bboink/releases/latest/download/bboink-app-t-embed-cc1101.bin"
