// SD card layout + migration helpers
#pragma once

#include <Arduino.h>

namespace SDLayout {
    // Layout state
    bool usingNewLayout();
    void setUseNewLayout(bool enable);

    // Root markers
    const char* newRoot();               // "/m5porkchop"
    const char* migrationMarkerPath();   // "/m5porkchop/meta/.migrated_v1"

    // Directories (resolved to legacy or new layout)
    const char* handshakesDir();
    const char* wardrivingDir();
    const char* modelsDir();
    const char* logsDir();
    const char* crashDir();
    const char* screenshotsDir();
    const char* diagnosticsDir();
    const char* wpaSecDir();
    const char* wigleDir();
    const char* xpDir();
    const char* miscDir();
    const char* configDir();
    const char* metaDir();

    // Files (resolved to legacy or new layout)
    const char* configPathSD();
    const char* personalityPathSD();
    const char* wpasecResultsPath();
    const char* wpasecUploadedPath();
    const char* wpasecSentPath();
    const char* wigleUploadedPath();
    const char* wigleStatsPath();
    const char* xpBackupPath();
    const char* xpAwardedWpaPath();
    const char* xpAwardedWiglePath();
    const char* boarBrosPath();
    const char* heapLogPath();
    const char* heapWatermarksPath();
    const char* wpasecKeyPath();
    const char* wigleKeyPath();

    // Legacy paths (explicit, for fallback imports)
    const char* legacyConfigPath();
    const char* legacyPersonalityPath();
    const char* legacyWpasecKeyPath();
    const char* legacyWigleKeyPath();

    // Filename helpers
    void sanitizeSsid(const char* ssid, char* out, size_t outLen);
    void buildCaptureFilename(char* out, size_t outLen, const char* dir,
                              const char* ssid, const uint8_t bssid[6],
                              const char* suffix);

    // Extract the 12-hex AP BSSID (uppercased, no colons) from a capture
    // filename. Handles the path prefix, the type token before the extension
    // (_pcap / _22000 / _pmkid, and legacy _hs), and both the new
    // "SSID_BSSID.ext" and legacy bare "BSSID.ext" forms. Returns false if no
    // 12-hex BSSID is present.
    bool captureBssid(const char* name, char out[13]);

    // Layout helpers
    bool migrateIfNeeded();     // create backup + move legacy content
    void ensureDirs();          // ensure directories for active layout exist
}
