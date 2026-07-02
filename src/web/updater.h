// Self-update over WiFi: download a firmware .bin and write it to the SD card so
// an SD-card launcher (M5Launcher/Bruce) loads the new build on the next launch.
// This never touches flash partitions, so it can't overwrite the launcher or any
// other firmware — each build is just a file.
#pragma once

#include <Arduino.h>

namespace Updater {

struct Result {
    bool   ok;
    size_t bytes;
    char   error[64];
};

// GET `url` (follows redirects — GitHub release assets 302 to a CDN), verify it
// starts with the ESP image magic (0xE9), and write it to `sdPath` on the capture
// FS via a temp file (only swapped in on success). progress(done,total) may be
// null; total is 0 when the server sends no Content-Length. Requires WiFi up.
// For a launcher install (the launcher loads the .bin from SD).
Result fetchToSD(const char* url, const char* sdPath, void (*progress)(size_t, size_t));

// GET `url` and write it straight into the spare OTA app partition via the Update
// library, then mark it bootable. For a STANDALONE install (flashed at 0x0). On
// success the caller should reboot to run the new image. Needs Content-Length.
Result fetchToFlash(const char* url, void (*progress)(size_t, size_t));

} // namespace Updater
