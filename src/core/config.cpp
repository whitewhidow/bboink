// src/core/config.cpp
// Configuration management implementation

#include "config.h"
#include "sdlog.h"
#include "sd_layout.h"
#include "storage.h"
#include <LittleFS.h>
#include "../hal/board.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <driver/gpio.h>

// ---- Cardputer microSD wiring (explicit, per Cardputer v1.1 schematic) ----
// ESP32-S3FN8:
//   microSD Socket  CS   MOSI  CLK   MISO
//                  G12  G14   G40   G39
//
// (Your previous patch used ESP32 “classic” pins + CS=4, which breaks SD on Cardputer/StampS3.)
// Board-specific SD pins (see hal/board.h). Cardputer uses a dedicated FSPI
// pinmap; the T-Embed CC1101 shares the display SPI bus with a dedicated CS.
static constexpr int SD_CS_PIN   = PORK_SD_CS;
static constexpr int SD_MOSI_PIN = PORK_SD_MOSI;
static constexpr int SD_MISO_PIN = PORK_SD_MISO;
static constexpr int SD_SCK_PIN  = PORK_SD_SCK;

// Dedicated SPI bus instance for SD.
// Cardputer microSD pinmap (from M5 docs): CS=12 MOSI=14 CLK=40 MISO=39.
// In practice, Arduino-ESP32/PlatformIO combos vary; using FSPI with explicit
// pins is the most reliable on Cardputer builds.
static SPIClass sdSPI(FSPI);
static bool sdSpiBegun = false;

// Static member initialization
GPSConfig Config::gpsConfig;
MLConfig Config::mlConfig;
WiFiConfig Config::wifiConfig;
BLEConfig Config::bleConfig;
PersonalityConfig Config::personalityConfig;
bool Config::initialized = false;
static bool sdAvailable = false;
static bool littlefsAvailable = false;

// ---- Binary config blob (zero heap allocation) ----
static constexpr uint32_t CONFIG_MAGIC   = 0x504F524B;  // 'PORK'
static constexpr uint16_t CONFIG_VERSION = 1;
#define CONFIG_BIN_FILE "/porkchop.dat"

static const char* configBinPathSD() {
    return SDLayout::usingNewLayout()
        ? "/m5porkchop/config/porkchop.dat"
        : "/porkchop.dat";
}

struct __attribute__((packed)) ConfigBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t blobSize;

    // GPS
    uint8_t  gpsEnabled;
    uint8_t  gpsSource;
    uint8_t  gpsRxPin;
    uint8_t  gpsTxPin;
    uint32_t gpsBaudRate;
    uint16_t gpsUpdateInterval;
    uint16_t gpsSleepTimeMs;
    uint8_t  gpsPowerSave;
    int8_t   gpsTimezoneOffset;

    // WiFi
    uint16_t channelHopInterval;
    uint16_t spectrumHopInterval;
    uint16_t lockTime;
    uint8_t  enableDeauth;
    uint8_t  randomizeMAC;
    int8_t   spectrumMinRssi;
    int8_t   attackMinRssi;
    uint8_t  deauthBurstCount;
    uint8_t  deauthJitterMax;
    uint8_t  spectrumTopN;
    uint16_t spectrumStaleMs;
    uint8_t  spectrumCollapseSsid;
    uint8_t  spectrumTiltEnabled;
    char     otaSSID[33];
    char     otaPassword[65];
    uint8_t  autoConnect;
    char     wpaSecKey[33];
    char     ohcKey[72];
    char     wigleApiName[65];
    char     wigleApiToken[65];

    // BLE
    uint16_t burstInterval;
    uint16_t advDuration;

    // ML (disabled but preserved for future)
    uint8_t  mlEnabled;
    uint8_t  mlCollectionMode;
    char     mlModelPath[64];
    float    mlConfidenceThreshold;
    float    mlRogueApThreshold;
    float    mlVulnScorerThreshold;
    uint8_t  mlAutoUpdate;
    char     mlUpdateUrl[128];

    // Appended after v-bump; old (shorter) blobs read these as 0 (see extractBlob).
    uint8_t  displayBrightness;
};

static void populateBlob(ConfigBlob& b, const GPSConfig& gps, const WiFiConfig& wifi,
                          const BLEConfig& ble, const MLConfig& ml) {
    memset(&b, 0, sizeof(b));
    b.magic    = CONFIG_MAGIC;
    b.version  = CONFIG_VERSION;
    b.blobSize = sizeof(ConfigBlob);

    b.gpsEnabled        = gps.enabled ? 1 : 0;
    b.gpsSource         = static_cast<uint8_t>(gps.source);
    b.gpsRxPin          = gps.rxPin;
    b.gpsTxPin          = gps.txPin;
    b.gpsBaudRate       = gps.baudRate;
    b.gpsUpdateInterval = gps.updateInterval;
    b.gpsSleepTimeMs    = gps.sleepTimeMs;
    b.gpsPowerSave      = gps.powerSave ? 1 : 0;
    b.gpsTimezoneOffset = gps.timezoneOffset;

    b.channelHopInterval   = wifi.channelHopInterval;
    b.spectrumHopInterval  = wifi.spectrumHopInterval;
    b.lockTime             = wifi.lockTime;
    b.enableDeauth         = wifi.enableDeauth ? 1 : 0;
    b.randomizeMAC         = wifi.randomizeMAC ? 1 : 0;
    b.spectrumMinRssi      = wifi.spectrumMinRssi;
    b.attackMinRssi        = wifi.attackMinRssi;
    b.deauthBurstCount     = wifi.deauthBurstCount;
    b.deauthJitterMax      = wifi.deauthJitterMax;
    b.displayBrightness    = wifi.displayBrightness;
    b.spectrumTopN         = wifi.spectrumTopN;
    b.spectrumStaleMs      = wifi.spectrumStaleMs;
    b.spectrumCollapseSsid = wifi.spectrumCollapseSsid ? 1 : 0;
    b.spectrumTiltEnabled  = wifi.spectrumTiltEnabled ? 1 : 0;
    strncpy(b.otaSSID,       wifi.otaSSID,       sizeof(b.otaSSID) - 1);
    strncpy(b.otaPassword,   wifi.otaPassword,   sizeof(b.otaPassword) - 1);
    b.autoConnect = wifi.autoConnect ? 1 : 0;
    strncpy(b.wpaSecKey,     wifi.wpaSecKey,     sizeof(b.wpaSecKey) - 1);
    strncpy(b.ohcKey,        wifi.ohcKey,        sizeof(b.ohcKey) - 1);
    strncpy(b.wigleApiName,  wifi.wigleApiName,  sizeof(b.wigleApiName) - 1);
    strncpy(b.wigleApiToken, wifi.wigleApiToken, sizeof(b.wigleApiToken) - 1);

    b.burstInterval = ble.burstInterval;
    b.advDuration   = ble.advDuration;

    b.mlEnabled              = ml.enabled ? 1 : 0;
    b.mlCollectionMode       = static_cast<uint8_t>(ml.collectionMode);
    strncpy(b.mlModelPath, ml.modelPath, sizeof(b.mlModelPath) - 1);
    b.mlConfidenceThreshold  = ml.confidenceThreshold;
    b.mlRogueApThreshold     = ml.rogueApThreshold;
    b.mlVulnScorerThreshold  = ml.vulnScorerThreshold;
    b.mlAutoUpdate           = ml.autoUpdate ? 1 : 0;
    strncpy(b.mlUpdateUrl, ml.updateUrl, sizeof(b.mlUpdateUrl) - 1);
}

static bool writeBlobTo(fs::FS& fs, const char* path, const ConfigBlob& b) {
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[CONFIG] writeBlobTo: failed to open '%s'\n", path);
        return false;
    }
    size_t written = file.write((const uint8_t*)&b, sizeof(b));
    file.close();
    Serial.printf("[CONFIG] writeBlobTo: %u/%u bytes -> '%s'\n",
                  written, sizeof(b), path);
    return written == sizeof(b);
}

static bool readBlobFrom(fs::FS& fs, const char* path, ConfigBlob& b) {
    File file = fs.open(path, FILE_READ);
    if (!file) return false;

    size_t fileSize = file.size();
    if (fileSize < 8) { file.close(); return false; }  // too small for header

    size_t readSize = (fileSize < sizeof(b)) ? fileSize : sizeof(b);
    memset(&b, 0, sizeof(b));
    size_t got = file.read((uint8_t*)&b, readSize);
    file.close();

    if (got < 8 || b.magic != CONFIG_MAGIC) return false;
    Serial.printf("[CONFIG] readBlobFrom: '%s' v%u, %u bytes\n", path, b.version, got);
    return true;
}

static void extractBlob(const ConfigBlob& b, GPSConfig& gps, WiFiConfig& wifi,
                         BLEConfig& ble, MLConfig& ml) {
    gps.enabled        = b.gpsEnabled != 0;
    gps.source         = static_cast<GPSSource>(b.gpsSource);
    gps.rxPin          = b.gpsRxPin;
    gps.txPin          = b.gpsTxPin;
    gps.baudRate       = b.gpsBaudRate;
    gps.updateInterval = b.gpsUpdateInterval;
    gps.sleepTimeMs    = b.gpsSleepTimeMs;
    gps.powerSave      = b.gpsPowerSave != 0;
    gps.timezoneOffset = b.gpsTimezoneOffset;

    // Auto-set pins based on source (same as JSON loader)
    if (gps.source == GPSSource::CAP_LORA) {
        gps.rxPin = 15; gps.txPin = 13;
    } else if (gps.source == GPSSource::GROVE) {
        gps.rxPin = 1;  gps.txPin = 2;
    }

    wifi.channelHopInterval   = b.channelHopInterval;
    wifi.spectrumHopInterval  = b.spectrumHopInterval;
    wifi.lockTime             = b.lockTime;
    wifi.enableDeauth         = b.enableDeauth != 0;
    wifi.randomizeMAC         = b.randomizeMAC != 0;
    wifi.spectrumMinRssi      = b.spectrumMinRssi;
    wifi.attackMinRssi        = b.attackMinRssi;
    wifi.deauthBurstCount     = b.deauthBurstCount ? b.deauthBurstCount : 5;
    wifi.deauthJitterMax      = b.deauthJitterMax;
    wifi.displayBrightness    = b.displayBrightness ? b.displayBrightness : 200;  // 0 = old blob
    wifi.spectrumTopN         = b.spectrumTopN;
    wifi.spectrumStaleMs      = b.spectrumStaleMs;
    wifi.spectrumCollapseSsid = b.spectrumCollapseSsid != 0;
    wifi.spectrumTiltEnabled  = b.spectrumTiltEnabled != 0;
    strncpy(wifi.otaSSID,       b.otaSSID,       sizeof(wifi.otaSSID) - 1);
    wifi.otaSSID[sizeof(wifi.otaSSID) - 1] = '\0';
    strncpy(wifi.otaPassword,   b.otaPassword,   sizeof(wifi.otaPassword) - 1);
    wifi.otaPassword[sizeof(wifi.otaPassword) - 1] = '\0';
    wifi.autoConnect = b.autoConnect != 0;
    strncpy(wifi.wpaSecKey,     b.wpaSecKey,     sizeof(wifi.wpaSecKey) - 1);
    wifi.wpaSecKey[sizeof(wifi.wpaSecKey) - 1] = '\0';
    strncpy(wifi.ohcKey,        b.ohcKey,        sizeof(wifi.ohcKey) - 1);
    wifi.ohcKey[sizeof(wifi.ohcKey) - 1] = '\0';
    strncpy(wifi.wigleApiName,  b.wigleApiName,  sizeof(wifi.wigleApiName) - 1);
    wifi.wigleApiName[sizeof(wifi.wigleApiName) - 1] = '\0';
    strncpy(wifi.wigleApiToken, b.wigleApiToken, sizeof(wifi.wigleApiToken) - 1);
    wifi.wigleApiToken[sizeof(wifi.wigleApiToken) - 1] = '\0';

    ble.burstInterval = b.burstInterval;
    ble.advDuration   = b.advDuration;

    ml.enabled              = b.mlEnabled != 0;
    ml.collectionMode       = static_cast<MLCollectionMode>(b.mlCollectionMode);
    strncpy(ml.modelPath, b.mlModelPath, sizeof(ml.modelPath) - 1);
    ml.modelPath[sizeof(ml.modelPath) - 1] = '\0';
    ml.confidenceThreshold  = b.mlConfidenceThreshold;
    ml.rogueApThreshold     = b.mlRogueApThreshold;
    ml.vulnScorerThreshold  = b.mlVulnScorerThreshold;
    ml.autoUpdate           = b.mlAutoUpdate != 0;
    strncpy(ml.updateUrl, b.mlUpdateUrl, sizeof(ml.updateUrl) - 1);
    ml.updateUrl[sizeof(ml.updateUrl) - 1] = '\0';
}

static uint16_t clampU16(uint32_t value, uint16_t minVal, uint16_t maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return static_cast<uint16_t>(value);
}

static int8_t clampI8(int value, int8_t minVal, int8_t maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return static_cast<int8_t>(value);
}

static void sanitizeWiFiConfig(WiFiConfig& cfg) {
    cfg.channelHopInterval = clampU16(cfg.channelHopInterval, 50, 2000);
    cfg.spectrumHopInterval = clampU16(cfg.spectrumHopInterval, 50, 2000);
    cfg.spectrumMinRssi = clampI8(cfg.spectrumMinRssi, -95, -30);
    cfg.attackMinRssi = clampI8(cfg.attackMinRssi, -90, -50);
    if (cfg.deauthBurstCount < 1) cfg.deauthBurstCount = 1;
    if (cfg.deauthBurstCount > 8) cfg.deauthBurstCount = 8;
    if (cfg.deauthJitterMax > 20) cfg.deauthJitterMax = 20;
    if (cfg.spectrumTopN > 100) cfg.spectrumTopN = 100;
    cfg.spectrumStaleMs = clampU16(cfg.spectrumStaleMs, 1000, 20000);
}

static void ensureSdSpiReady() {
    // Re-init SD SPI bus cleanly
    if (sdSpiBegun) {
        sdSPI.end();
        sdSpiBegun = false;
        delay(20);
    }

    // Make sure CS is a sane GPIO output and deasserted before touching the bus.
    // This prevents random Select Failed errors on some cards.
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

#ifdef PORK_BOARD_TEMBED_CC1101
    // On the T-Embed CC1101 the SD, CC1101 radio AND the ST7789 display all hang
    // off ONE shared SPI bus (SCLK 11 / MOSI 9 / MISO 10). Every other device's
    // chip-select must be HIGH (deselected) before the SD is mounted, or it holds
    // the shared MISO line and the mount fails ("physical drive cannot work").
    // This mirrors LilyGo's factory board_spi_deselect_all(). Our SD mount runs
    // before the display driver claims pin 41, so we must park TFT CS ourselves.
    pinMode(PORK_CC1101_CS, OUTPUT); digitalWrite(PORK_CC1101_CS, HIGH);  // radio
    pinMode(PORK_TFT_CS,    OUTPUT); digitalWrite(PORK_TFT_CS,    HIGH);  // display
#endif

    // SCK, MISO, MOSI, SS/CS
    sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdSpiBegun = true;
    delay(20);
}

bool Config::init() {
    // Initialize SPIFFS first (always available)
    if (!SPIFFS.begin(false)) {
        Serial.println("[CONFIG] SPIFFS mount failed, attempting format...");
        if (!SPIFFS.begin(true)) {
            Serial.println("[CONFIG] SPIFFS format failed! Personality settings will not persist.");
        } else {
            Serial.println("[CONFIG] SPIFFS formatted and mounted OK");
        }
    }

    // Allow buses to stabilize after M5.begin()
    delay(50);

#ifdef PORK_BOARD_TEMBED_CC1101
    // The ST7789 hasn't been initialised yet (display comes up after Config::init)
    // and in its power-on state it loads the shared SPI bus, corrupting SD I/O.
    // Hold the panel in reset (RST low) for the duration of the SD mount so it
    // releases the bus; the display driver re-pulses RST when it initialises.
    pinMode(PORK_TFT_RST, OUTPUT);
    digitalWrite(PORK_TFT_RST, LOW);
    delay(10);
#endif

    // Ensure SD has a proper SPI bus configured
    ensureSdSpiReady();

    // Retry with progressive SPI speeds for reliability
    sdAvailable = false;
    const int maxRetries = 6;
    const uint32_t speeds[] = {
        25000000, // 25 MHz
        20000000, // 20 MHz
        10000000, // 10 MHz
        8000000,  // 8 MHz
        4000000,  // 4 MHz
        1000000   // 1 MHz
    };

    for (int attempt = 0; attempt < maxRetries && !sdAvailable; attempt++) {
        uint32_t speed = speeds[attempt];
        Serial.printf("[CONFIG] SD init attempt %d/%d at %luMHz\n",
                      attempt + 1, maxRetries, speed / 1000000);

        if (attempt > 0) {
            SD.end();     // Clean up previous failed attempt
            delay(80);    // Allow bus to settle
            ensureSdSpiReady();
        }

        // Use explicit CS + dedicated SPI + explicit speed
        if (SD.begin(SD_CS_PIN, sdSPI, speed)) {
            Serial.printf("[CONFIG] SD card mounted at %luMHz\n", speed / 1000000);
            sdAvailable = true;
        }
    }

    // Pick the capture-storage backend: SD card if present, else internal
    // LittleFS (format-on-mount). This is what makes the firmware work without
    // an SD card. Personality/config still fall back to SPIFFS independently.
    if (sdAvailable) {
        Storage::setBackend(&SD, true);
    } else if (LittleFS.begin(true, "/littlefs", 5, "littlefs")) {
        littlefsAvailable = true;
        Storage::setBackend(&LittleFS, false);
        Serial.println("[CONFIG] No SD card - using internal LittleFS for captures");
    } else {
        Serial.println("[CONFIG] No SD and LittleFS mount FAILED - captures disabled");
    }

    if (Storage::available()) {
        SDLayout::migrateIfNeeded();
        SDLayout::ensureDirs();
        SDLog::log("CFG", "Storage ready: %s", Storage::backendName());
    } else {
        SDLayout::setUseNewLayout(false);
    }

    // Load personality from SPIFFS (always available)
    if (!loadPersonality()) {
        Serial.println("[CONFIG] Creating default personality");
        createDefaultPersonality();
        savePersonalityToSPIFFS();
    }

    // Load main config: SD primary, SPIFFS fallback
    Serial.printf("[CONFIG] Pre-load state: sdAvailable=%d, newLayout=%d\n",
                  sdAvailable, SDLayout::usingNewLayout());
    if (!load()) {
        Serial.println("[CONFIG] Creating default config");
        createDefaultConfig();
        save();
    }

    // If a card is mounted but has no config yet (e.g. the values were previously
    // stored only in internal SPIFFS because the SD wasn't mounting), write the
    // current config to the SD. The SD copy is authoritative and survives flashing
    // other firmware, so re-flashing bboink restores your settings from the card.
    if (sdAvailable && !SD.exists(configBinPathSD())) {
        if (save()) Serial.println("[CONFIG] Config persisted to SD (survives reflashing)");
    }

    // No credentials are hardcoded. Configure the WiFi SSID/password, the wpa-sec
    // key and the OnlineHashCrack key on-device via the Options screen, or drop a
    // wpa-sec key file on the SD card (see loadWpaSecKeyFromFile below). Values
    // entered on-device persist in NVS/config and survive reflashing.

    // Try to load keys from files (auto-deletes after import)
    if (loadWpaSecKeyFromFile()) {
        Serial.println("[CONFIG] WPA-SEC key loaded from file");
    }
    if (loadWigleKeyFromFile()) {
        Serial.println("[CONFIG] WiGLE API keys loaded from file");
    }

    // Merge creds from JSON porkchop.conf if present (handles the case where
    // binary config already exists but user dropped a new .conf with creds)
    if (importCredsFromJsonConf()) {
        Serial.println("[CONFIG] Credentials imported from porkchop.conf");
    }

    initialized = true;
    return true;
}

bool Config::isSDAvailable() {
    // Now means "a capture filesystem is available" (SD card or internal LittleFS).
    return Storage::available();
}

bool Config::isSDCardMounted() {
    return sdAvailable;
}

void Config::prepareSDBus() {
    ensureSdSpiReady();
}

SPIClass& Config::sdSpi() {
    return sdSPI;
}

int Config::sdCsPin() {
    return SD_CS_PIN;
}

void Config::prepareCapLoraGpio() {
    // GPIO 13 is ESP32-S3 default FSPIQ (MISO) via IOMUX. Even though SD remaps
    // FSPI MISO to G39, the default IOMUX linkage on G13 can disrupt the FSPI
    // peripheral when Serial2 reconfigures G13 as UART TX output.
    // gpio_reset_pin() clears IOMUX function, disconnects peripheral signals,
    // and returns the pin to plain GPIO mode.
    gpio_reset_pin(static_cast<gpio_num_t>(CapLoraPins::GPS_TX));   // G13

    // Reset SX1262 LoRa chip to known state. The CapLoRa868 LoRa SPI shares
    // MOSI(G14)/MISO(G39)/SCK(G40) with SD card. After reset the SX1262
    // enters STANDBY_RC with all IOs high-impedance, preventing bus contention.
    pinMode(CapLoraPins::LORA_RESET, OUTPUT);
    digitalWrite(CapLoraPins::LORA_RESET, LOW);   // Assert NRESET (active low)
    delay(10);                                      // SX1262 datasheet: >100us
    digitalWrite(CapLoraPins::LORA_RESET, HIGH);   // Release reset
    delay(10);                                      // Wait for standby entry

    // Deassert LoRa chip select (HIGH = not selected, MISO tri-stated)
    pinMode(CapLoraPins::LORA_CS, OUTPUT);
    digitalWrite(CapLoraPins::LORA_CS, HIGH);

    // Configure control pins as inputs (don't drive)
    pinMode(CapLoraPins::LORA_BUSY, INPUT);
    pinMode(CapLoraPins::LORA_DIO1, INPUT);

    Serial.println("[CONFIG] CapLoRa868: SX1262 reset, CS deasserted, G13 IOMUX cleared");
}

bool Config::reinitSD() {
    // Quick check: SD still accessible? Skip destructive reinit if so.
    if (sdAvailable && SD.exists("/")) {
        Serial.println("[CONFIG] SD still accessible after GPS init, skipping reinit");
        return true;
    }

    Serial.println("[CONFIG] SD access lost, attempting re-initialization...");

    // Save current state to restore on failure
    bool wasSdAvailable = sdAvailable;
    bool wasNewLayout = SDLayout::usingNewLayout();

    // Clean up any existing SD state
    SD.end();
    delay(80);

    // Re-init SD SPI bus explicitly
    ensureSdSpiReady();

    // Retry with progressive SPI speeds
    sdAvailable = false;
    const int maxRetries = 6;
    const uint32_t speeds[] = {
        25000000,
        20000000,
        10000000,
        8000000,
        4000000,
        1000000
    };

    for (int attempt = 0; attempt < maxRetries && !sdAvailable; attempt++) {
        uint32_t speed = speeds[attempt];
        Serial.printf("[CONFIG] SD reinit attempt %d/%d at %luMHz\n",
                      attempt + 1, maxRetries, speed / 1000000);

        if (attempt > 0) {
            SD.end();
            delay(80);
            ensureSdSpiReady();
        }

        if (SD.begin(SD_CS_PIN, sdSPI, speed)) {
            Serial.printf("[CONFIG] SD card mounted at %luMHz\n", speed / 1000000);
            sdAvailable = true;
        }
    }

    if (sdAvailable) {
        // Success: verify layout by checking marker directly (no full migration)
        if (SD.exists(SDLayout::migrationMarkerPath())) {
            SDLayout::setUseNewLayout(true);
        } else if (wasNewLayout) {
            // Marker unreadable but we were using new layout — keep it
            SDLayout::setUseNewLayout(true);
        }
        SDLayout::ensureDirs();
        SDLog::log("CFG", "SD card re-initialized OK");
    } else {
        // FAIL: Restore previous state — don't corrupt flags
        sdAvailable = wasSdAvailable;
        SDLayout::setUseNewLayout(wasNewLayout);
        Serial.println("[CONFIG] SD reinit failed, keeping previous SD state");
    }

    return sdAvailable;
}

bool Config::mountSdAfterDisplay() {
    // On the T-Embed CC1101 the SD, ST7789 display and CC1101 share one SPI bus.
    // The SD frequently won't mount until the display driver has initialised the
    // ST7789 (which leaves the panel in a known state that releases the shared
    // bus). Config::init() runs BEFORE the display (required for the WiFi-before-
    // display workaround), so the boot mount often fails and we fall back to
    // LittleFS. Retry here, after M5/display init; on success route storage to SD.
    if (sdAvailable) return true;              // already mounted at boot
    if (!reinitSD()) return false;             // still no usable card -> keep LittleFS
    Storage::setBackend(&SD, true);            // captures + config now go to the SD
    // Seed the SD with the current config if it doesn't have it yet, so settings
    // entered while on LittleFS get persisted to the (reflash-proof) card.
    if (!SD.exists(configBinPathSD())) save();
    Serial.println("[CONFIG] SD mounted after display init; storage -> SD");
    return true;
}

bool Config::loadFrom(fs::FS& fs, const char* path) {
    File file = fs.open(path, FILE_READ);
    if (!file) return false;

    size_t fileSize = file.size();
    Serial.printf("[CONFIG] loadFrom(): '%s' size=%u bytes\n", path, fileSize);
    if (fileSize == 0) { file.close(); return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[CONFIG] loadFrom(): JSON error: %s ('%s')\n", err.c_str(), path);
        return false;
    }

    // Populate config from parsed JSON — shared with load()
    return applyJson(doc);
}

bool Config::applyJson(const JsonDocument& doc) {
    // GPS config
    if (doc["gps"].is<JsonObject>()) {
        gpsConfig.enabled = doc["gps"]["enabled"] | true;
        gpsConfig.source = static_cast<GPSSource>(doc["gps"]["gpsSource"] | 0);

        // Auto-set pins based on source, or load custom pins
        if (gpsConfig.source == GPSSource::CAP_LORA) {
            gpsConfig.rxPin = 15;  // Cap LoRa868 GPS RX
            gpsConfig.txPin = 13;  // Cap LoRa868 GPS TX
        } else if (gpsConfig.source == GPSSource::GROVE) {
            gpsConfig.rxPin = 1;   // Grove GPS RX
            gpsConfig.txPin = 2;   // Grove GPS TX
        } else {
            // CUSTOM: load pins from config
            gpsConfig.rxPin = doc["gps"]["rxPin"] | 1;
            gpsConfig.txPin = doc["gps"]["txPin"] | 2;
        }

        gpsConfig.baudRate = doc["gps"]["baudRate"] | 115200;
        gpsConfig.updateInterval = doc["gps"]["updateInterval"] | 5;
        gpsConfig.sleepTimeMs = doc["gps"]["sleepTimeMs"] | 5000;
        gpsConfig.powerSave = doc["gps"]["powerSave"] | true;
        gpsConfig.timezoneOffset = doc["gps"]["timezoneOffset"] | 0;
    }

    // ML config
    if (doc["ml"].is<JsonObject>()) {
        mlConfig.enabled = doc["ml"]["enabled"] | true;
        mlConfig.collectionMode = static_cast<MLCollectionMode>(doc["ml"]["collectionMode"] | 0);
        const char* mp = doc["ml"]["modelPath"] | "/m5porkchop/models/porkchop_model.bin";
        strncpy(mlConfig.modelPath, mp, sizeof(mlConfig.modelPath) - 1);
        mlConfig.modelPath[sizeof(mlConfig.modelPath) - 1] = '\0';
        if (sdAvailable && SDLayout::usingNewLayout()) {
            if (strncmp(mlConfig.modelPath, "/models/", 8) == 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%s%s", SDLayout::modelsDir(), mlConfig.modelPath + 7);
                strncpy(mlConfig.modelPath, tmp, sizeof(mlConfig.modelPath) - 1);
                mlConfig.modelPath[sizeof(mlConfig.modelPath) - 1] = '\0';
            }
        }
        mlConfig.confidenceThreshold = doc["ml"]["confidenceThreshold"] | 0.7f;
        mlConfig.rogueApThreshold = doc["ml"]["rogueApThreshold"] | 0.8f;
        mlConfig.vulnScorerThreshold = doc["ml"]["vulnScorerThreshold"] | 0.6f;
        mlConfig.autoUpdate = doc["ml"]["autoUpdate"] | false;
        const char* uu = doc["ml"]["updateUrl"] | "";
        strncpy(mlConfig.updateUrl, uu, sizeof(mlConfig.updateUrl) - 1);
        mlConfig.updateUrl[sizeof(mlConfig.updateUrl) - 1] = '\0';
    }

    // WiFi config
    if (doc["wifi"].is<JsonObject>()) {
        int hopInterval = doc["wifi"]["channelHopInterval"] | 150;
        wifiConfig.channelHopInterval = clampU16(hopInterval, 50, 2000);
        int spectrumHop = doc["wifi"]["spectrumHopInterval"] | 150;
        wifiConfig.spectrumHopInterval = clampU16(spectrumHop, 50, 2000);
        wifiConfig.lockTime = doc["wifi"]["lockTime"] | 12000;
        wifiConfig.enableDeauth = doc["wifi"]["enableDeauth"] | true;
        wifiConfig.randomizeMAC = doc["wifi"]["randomizeMAC"] | true;
        int attackRssi = doc["wifi"]["attackMinRssi"] | -70;
        wifiConfig.attackMinRssi = clampI8(attackRssi, -90, -50);
        wifiConfig.deauthBurstCount = doc["wifi"]["deauthBurstCount"] | 5;
        wifiConfig.deauthJitterMax = doc["wifi"]["deauthJitterMax"] | 5;
        int minRssi = doc["wifi"]["spectrumMinRssi"] | -95;
        wifiConfig.spectrumMinRssi = clampI8(minRssi, -95, -30);
        int topN = doc["wifi"]["spectrumTopN"] | 0;
        if (topN < 0) topN = 0;
        if (topN > 100) topN = 100;
        wifiConfig.spectrumTopN = static_cast<uint8_t>(topN);
        int staleMs = doc["wifi"]["spectrumStaleMs"] | 5000;
        wifiConfig.spectrumStaleMs = clampU16(staleMs, 1000, 20000);
        wifiConfig.spectrumCollapseSsid = doc["wifi"]["spectrumCollapseSsid"] | false;
        wifiConfig.spectrumTiltEnabled = doc["wifi"]["spectrumTiltEnabled"] | true;
        const char* ssid = doc["wifi"]["otaSSID"] | "";
        strncpy(wifiConfig.otaSSID, ssid, sizeof(wifiConfig.otaSSID) - 1);
        wifiConfig.otaSSID[sizeof(wifiConfig.otaSSID) - 1] = '\0';
        const char* password = doc["wifi"]["otaPassword"] | "";
        strncpy(wifiConfig.otaPassword, password, sizeof(wifiConfig.otaPassword) - 1);
        wifiConfig.otaPassword[sizeof(wifiConfig.otaPassword) - 1] = '\0';
        wifiConfig.autoConnect = doc["wifi"]["autoConnect"] | false;
        const char* key = doc["wifi"]["wpaSecKey"] | "";
        strncpy(wifiConfig.wpaSecKey, key, sizeof(wifiConfig.wpaSecKey) - 1);
        wifiConfig.wpaSecKey[sizeof(wifiConfig.wpaSecKey) - 1] = '\0';
        const char* ohc = doc["wifi"]["ohcKey"] | "";
        strncpy(wifiConfig.ohcKey, ohc, sizeof(wifiConfig.ohcKey) - 1);
        wifiConfig.ohcKey[sizeof(wifiConfig.ohcKey) - 1] = '\0';
        const char* apiName = doc["wifi"]["wigleApiName"] | "";
        strncpy(wifiConfig.wigleApiName, apiName, sizeof(wifiConfig.wigleApiName) - 1);
        wifiConfig.wigleApiName[sizeof(wifiConfig.wigleApiName) - 1] = '\0';
        const char* apiToken = doc["wifi"]["wigleApiToken"] | "";
        strncpy(wifiConfig.wigleApiToken, apiToken, sizeof(wifiConfig.wigleApiToken) - 1);
        wifiConfig.wigleApiToken[sizeof(wifiConfig.wigleApiToken) - 1] = '\0';
    }
    sanitizeWiFiConfig(wifiConfig);

    // BLE config (PIGGY BLUES)
    if (doc["ble"].is<JsonObject>()) {
        bleConfig.burstInterval = doc["ble"]["burstInterval"] | 200;
        bleConfig.advDuration = doc["ble"]["advDuration"] | 100;
    }

    Serial.printf("[CONFIG] Loaded OK: wpaKey=%s, wigleName=%s, wigleToken=%s, otaSSID=%s, deauth=%d, gps=%d\n",
                  strlen(wifiConfig.wpaSecKey) > 0 ? "(SET)" : "(EMPTY)",
                  strlen(wifiConfig.wigleApiName) > 0 ? "(SET)" : "(EMPTY)",
                  strlen(wifiConfig.wigleApiToken) > 0 ? "(SET)" : "(EMPTY)",
                  strlen(wifiConfig.otaSSID) > 0 ? wifiConfig.otaSSID : "(EMPTY)",
                  wifiConfig.enableDeauth,
                  static_cast<int>(gpsConfig.source));
    return true;
}

bool Config::load() {
    Serial.printf("[CONFIG] load(): sdAvail=%d, newLayout=%d\n",
                  sdAvailable, SDLayout::usingNewLayout());

    ConfigBlob blob;

    // 1. Try binary from SD (current layout path)
    if (sdAvailable && readBlobFrom((fs::FS&)SD, configBinPathSD(), blob)) {
        extractBlob(blob, gpsConfig, wifiConfig, bleConfig, mlConfig);
        sanitizeWiFiConfig(wifiConfig);
        Serial.println("[CONFIG] Loaded binary from SD");
        // Mirror to SPIFFS
        writeBlobTo((fs::FS&)SPIFFS, CONFIG_BIN_FILE, blob);
        return true;
    }

    // 1b. Try legacy binary path on SD (migration may not have moved porkchop.dat)
    if (sdAvailable && SDLayout::usingNewLayout()) {
        if (readBlobFrom((fs::FS&)SD, "/porkchop.dat", blob)) {
            extractBlob(blob, gpsConfig, wifiConfig, bleConfig, mlConfig);
            sanitizeWiFiConfig(wifiConfig);
            Serial.println("[CONFIG] Loaded binary from legacy SD path, migrating...");
            // Move to new location and mirror to SPIFFS
            writeBlobTo((fs::FS&)SD, configBinPathSD(), blob);
            writeBlobTo((fs::FS&)SPIFFS, CONFIG_BIN_FILE, blob);
            SD.remove("/porkchop.dat");
            Serial.println("[CONFIG] Migrated porkchop.dat to new layout path");
            return true;
        }
    }

    // 2. Try binary from SPIFFS
    if (readBlobFrom((fs::FS&)SPIFFS, CONFIG_BIN_FILE, blob)) {
        extractBlob(blob, gpsConfig, wifiConfig, bleConfig, mlConfig);
        sanitizeWiFiConfig(wifiConfig);
        Serial.println("[CONFIG] Loaded binary from SPIFFS");
        return true;
    }

    // 3. JSON migration: try SD paths
    if (sdAvailable) {
        const char* sdPath = SDLayout::configPathSD();
        if (loadFrom((fs::FS&)SD, sdPath)) {
            Serial.printf("[CONFIG] Migrated JSON from SD: '%s'\n", sdPath);
            save();           // write binary to both SD + SPIFFS
            SD.remove(sdPath);  // delete old JSON
            Serial.printf("[CONFIG] Deleted old JSON: '%s'\n", sdPath);
            return true;
        }
        if (SDLayout::usingNewLayout()) {
            const char* legacyPath = SDLayout::legacyConfigPath();
            if (loadFrom((fs::FS&)SD, legacyPath)) {
                Serial.printf("[CONFIG] Migrated JSON from SD legacy: '%s'\n", legacyPath);
                save();
                SD.remove(legacyPath);
                return true;
            }
        }
    }

    // 4. JSON migration: try SPIFFS
    if (loadFrom((fs::FS&)SPIFFS, CONFIG_FILE)) {
        Serial.println("[CONFIG] Migrated JSON from SPIFFS");
        save();                      // write binary
        SPIFFS.remove(CONFIG_FILE);  // delete old JSON
        return true;
    }

    Serial.println("[CONFIG] No config found (binary or JSON)");
    return false;
}

bool Config::loadPersonality() {
    // Load from SPIFFS (always available)
    File file = SPIFFS.open(PERSONALITY_FILE, FILE_READ);
    if (!file) {
        Serial.println("[CONFIG] Personality file not found in SPIFFS");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[CONFIG] Personality JSON error: %s\n", err.c_str());
        return false;
    }

    const char* name = doc["name"] | "Porkchop";
    strncpy(personalityConfig.name, name, sizeof(personalityConfig.name) - 1);
    personalityConfig.name[sizeof(personalityConfig.name) - 1] = '\0';

    const char* callsign = doc["callsign"] | "";
    strncpy(personalityConfig.callsign, callsign, sizeof(personalityConfig.callsign) - 1);
    personalityConfig.callsign[sizeof(personalityConfig.callsign) - 1] = '\0';

    personalityConfig.mood = doc["mood"] | 50;
    personalityConfig.experience = doc["experience"] | 0;
    personalityConfig.curiosity = doc["curiosity"] | 0.7f;
    personalityConfig.aggression = doc["aggression"] | 0.3f;
    personalityConfig.patience = doc["patience"] | 0.5f;
    personalityConfig.soundEnabled = doc["soundEnabled"] | true;
    personalityConfig.brightness = doc["brightness"] | 80;
    personalityConfig.dimLevel = doc["dimLevel"] | 20;
    personalityConfig.dimTimeout = doc["dimTimeout"] | 30;
    personalityConfig.themeIndex = doc["themeIndex"] | 0;
    uint8_t g0Action = doc["g0Action"] | static_cast<uint8_t>(G0Action::SCREEN_TOGGLE);
    if (g0Action >= G0_ACTION_COUNT) {
        g0Action = static_cast<uint8_t>(G0Action::SCREEN_TOGGLE);
    }
    personalityConfig.g0Action = static_cast<G0Action>(g0Action);
    uint8_t bootMode = doc["bootMode"] | static_cast<uint8_t>(BootMode::IDLE);
    if (bootMode >= BOOT_MODE_COUNT) {
        bootMode = static_cast<uint8_t>(BootMode::IDLE);
    }
    personalityConfig.bootMode = static_cast<BootMode>(bootMode);

    Serial.printf("[CONFIG] Personality: %s (mood: %d, sound: %s, bright: %d%%, dim: %ds, theme: %d)\n",
                  personalityConfig.name,
                  personalityConfig.mood,
                  personalityConfig.soundEnabled ? "ON" : "OFF",
                  personalityConfig.brightness,
                  personalityConfig.dimTimeout,
                  personalityConfig.themeIndex);
    return true;
}

void Config::savePersonalityToSPIFFS() {
    JsonDocument doc;
    doc["name"] = personalityConfig.name;
    doc["callsign"] = personalityConfig.callsign;
    doc["mood"] = personalityConfig.mood;
    doc["experience"] = personalityConfig.experience;
    doc["curiosity"] = personalityConfig.curiosity;
    doc["aggression"] = personalityConfig.aggression;
    doc["patience"] = personalityConfig.patience;
    doc["soundEnabled"] = personalityConfig.soundEnabled;
    doc["brightness"] = personalityConfig.brightness;
    doc["dimLevel"] = personalityConfig.dimLevel;
    doc["dimTimeout"] = personalityConfig.dimTimeout;
    doc["themeIndex"] = personalityConfig.themeIndex;
    doc["g0Action"] = static_cast<uint8_t>(personalityConfig.g0Action);
    doc["bootMode"] = static_cast<uint8_t>(personalityConfig.bootMode);

    File file = SPIFFS.open(PERSONALITY_FILE, FILE_WRITE);
    if (file) {
        serializeJsonPretty(doc, file);
        file.close();
        Serial.printf("[CONFIG] Saved personality to SPIFFS (sound: %s)\n",
                      personalityConfig.soundEnabled ? "ON" : "OFF");
    } else {
        Serial.println("[CONFIG] Failed to save personality to SPIFFS");
    }
}

bool Config::save() {
    ConfigBlob blob;
    populateBlob(blob, gpsConfig, wifiConfig, bleConfig, mlConfig);

    Serial.printf("[CONFIG] save(): sdAvail=%d, wpaKey=%s, wigle=%s\n",
                  sdAvailable,
                  strlen(wifiConfig.wpaSecKey) > 0 ? "(SET)" : "(EMPTY)",
                  strlen(wifiConfig.wigleApiName) > 0 ? "(SET)" : "(EMPTY)");

    bool ok = false;
    if (sdAvailable) {
        ok = writeBlobTo((fs::FS&)SD, configBinPathSD(), blob);
    }

    // Always mirror to SPIFFS
    bool spiffsOk = writeBlobTo((fs::FS&)SPIFFS, CONFIG_BIN_FILE, blob);
    if (!sdAvailable) ok = spiffsOk;

    return ok;
}

bool Config::createDefaultConfig() {
    gpsConfig = GPSConfig();
    mlConfig = MLConfig();
    wifiConfig = WiFiConfig();
    sanitizeWiFiConfig(wifiConfig);
    bleConfig = BLEConfig();
    return true;
}

bool Config::createDefaultPersonality() {
    strncpy(personalityConfig.name, "Porkchop", sizeof(personalityConfig.name) - 1);
    personalityConfig.name[sizeof(personalityConfig.name) - 1] = '\0';
    personalityConfig.mood = 50;
    personalityConfig.experience = 0;
    personalityConfig.curiosity = 0.7f;
    personalityConfig.aggression = 0.3f;
    personalityConfig.patience = 0.5f;
    personalityConfig.soundEnabled = true;
    personalityConfig.g0Action = G0Action::SCREEN_TOGGLE;
    personalityConfig.bootMode = BootMode::IDLE;
    return true;
}

void Config::setGPS(const GPSConfig& cfg) {
    gpsConfig = cfg;
    save();
}

void Config::setML(const MLConfig& cfg) {
    mlConfig = cfg;
    save();
}

void Config::setWiFi(const WiFiConfig& cfg) {
    wifiConfig = cfg;
    sanitizeWiFiConfig(wifiConfig);
    save();
}

void Config::setBLE(const BLEConfig& cfg) {
    bleConfig = cfg;
    save();
}

void Config::setPersonality(const PersonalityConfig& cfg) {
    personalityConfig = cfg;
    savePersonalityToSPIFFS();
}

bool Config::loadWpaSecKeyFromFile() {
    const char* keyFile = SDLayout::wpasecKeyPath();
    const char* legacyKeyFile = SDLayout::legacyWpasecKeyPath();

    if (!sdAvailable) {
        return false;
    }
    if (!SD.exists(keyFile) && SD.exists(legacyKeyFile)) {
        keyFile = legacyKeyFile;
    }
    if (!SD.exists(keyFile)) {
        return false;
    }

    File f = SD.open(keyFile, FILE_READ);
    if (!f) {
        Serial.println("[CONFIG] Failed to open WPA-SEC key file");
        return false;
    }

    char key[64];
    size_t keyLen = f.readBytesUntil('\n', key, sizeof(key) - 1);
    key[keyLen] = '\0';
    f.close();
    // Trim trailing whitespace
    while (keyLen > 0 && (key[keyLen - 1] == '\r' || key[keyLen - 1] == ' ')) {
        key[--keyLen] = '\0';
    }

    if (keyLen != 32) {
        Serial.printf("[CONFIG] Invalid WPA-SEC key length: %d (expected 32)\n", (int)keyLen);
        return false;
    }

    for (int i = 0; i < 32; i++) {
        if (!isxdigit(key[i])) {
            Serial.printf("[CONFIG] Invalid hex char in WPA-SEC key at position %d\n", i);
            return false;
        }
    }

    strncpy(wifiConfig.wpaSecKey, key, sizeof(wifiConfig.wpaSecKey) - 1);
    wifiConfig.wpaSecKey[sizeof(wifiConfig.wpaSecKey) - 1] = '\0';
    save();

    if (SD.remove(keyFile)) {
        Serial.println("[CONFIG] Deleted WPA-SEC key file after import");
        SDLog::log("CFG", "WPA-SEC key imported from file");
    } else {
        Serial.println("[CONFIG] Warning: Could not delete WPA-SEC key file");
    }

    return true;
}

bool Config::loadWigleKeyFromFile() {
    const char* keyFile = SDLayout::wigleKeyPath();
    const char* legacyKeyFile = SDLayout::legacyWigleKeyPath();

    if (!sdAvailable) {
        return false;
    }
    if (!SD.exists(keyFile) && SD.exists(legacyKeyFile)) {
        keyFile = legacyKeyFile;
    }
    if (!SD.exists(keyFile)) {
        return false;
    }

    File f = SD.open(keyFile, FILE_READ);
    if (!f) {
        Serial.println("[CONFIG] Failed to open WiGLE key file");
        return false;
    }

    char content[160];
    size_t cLen = f.readBytesUntil('\n', content, sizeof(content) - 1);
    content[cLen] = '\0';
    f.close();
    // Trim trailing whitespace
    while (cLen > 0 && (content[cLen - 1] == '\r' || content[cLen - 1] == ' ')) {
        content[--cLen] = '\0';
    }

    char* colonPos = strchr(content, ':');
    if (!colonPos || colonPos == content) {
        Serial.println("[CONFIG] Invalid WiGLE key format (expected name:token)");
        return false;
    }

    *colonPos = '\0';  // Split into two strings
    const char* apiName = content;
    const char* apiToken = colonPos + 1;
    // Trim leading spaces from token
    while (*apiToken == ' ') apiToken++;

    if (apiName[0] == '\0' || apiToken[0] == '\0') {
        Serial.println("[CONFIG] WiGLE API name or token is empty");
        return false;
    }

    // Use strncpy to safely copy strings to char arrays
    strncpy(wifiConfig.wigleApiName, apiName, sizeof(wifiConfig.wigleApiName) - 1);
    wifiConfig.wigleApiName[sizeof(wifiConfig.wigleApiName) - 1] = '\0';

    strncpy(wifiConfig.wigleApiToken, apiToken, sizeof(wifiConfig.wigleApiToken) - 1);
    wifiConfig.wigleApiToken[sizeof(wifiConfig.wigleApiToken) - 1] = '\0';
    save();

    if (SD.remove(keyFile)) {
        Serial.println("[CONFIG] Deleted WiGLE key file after import");
        SDLog::log("CFG", "WiGLE API keys imported from file");
    } else {
        Serial.println("[CONFIG] Warning: Could not delete WiGLE key file");
    }

    return true;
}

bool Config::importCredsFromJsonConf() {
    if (!sdAvailable) return false;

    // Check both new and legacy JSON config paths
    const char* confPath = SDLayout::configPathSD();
    if (!SD.exists(confPath)) {
        if (SDLayout::usingNewLayout()) {
            confPath = SDLayout::legacyConfigPath();
            if (!SD.exists(confPath)) return false;
        } else {
            return false;
        }
    }

    File file = SD.open(confPath, FILE_READ);
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[CONFIG] importCreds: JSON parse error: %s ('%s')\n", err.c_str(), confPath);
        return false;
    }

    if (!doc["wifi"].is<JsonObject>()) {
        Serial.printf("[CONFIG] importCreds: no 'wifi' object in '%s'\n", confPath);
        SD.remove(confPath);
        return false;
    }

    bool merged = false;

    // Import WPA-SEC key
    const char* key = doc["wifi"]["wpaSecKey"] | "";
    if (key[0] != '\0') {
        strncpy(wifiConfig.wpaSecKey, key, sizeof(wifiConfig.wpaSecKey) - 1);
        wifiConfig.wpaSecKey[sizeof(wifiConfig.wpaSecKey) - 1] = '\0';
        Serial.println("[CONFIG] importCreds: WPA-SEC key merged from porkchop.conf");
        merged = true;
    }

    // Import WiGLE API name
    const char* apiName = doc["wifi"]["wigleApiName"] | "";
    if (apiName[0] != '\0') {
        strncpy(wifiConfig.wigleApiName, apiName, sizeof(wifiConfig.wigleApiName) - 1);
        wifiConfig.wigleApiName[sizeof(wifiConfig.wigleApiName) - 1] = '\0';
        Serial.println("[CONFIG] importCreds: WiGLE API name merged from porkchop.conf");
        merged = true;
    }

    // Import WiGLE API token
    const char* apiToken = doc["wifi"]["wigleApiToken"] | "";
    if (apiToken[0] != '\0') {
        strncpy(wifiConfig.wigleApiToken, apiToken, sizeof(wifiConfig.wigleApiToken) - 1);
        wifiConfig.wigleApiToken[sizeof(wifiConfig.wigleApiToken) - 1] = '\0';
        Serial.println("[CONFIG] importCreds: WiGLE API token merged from porkchop.conf");
        merged = true;
    }

    if (merged) {
        save();
        SDLog::log("CFG", "Credentials imported from porkchop.conf");
    }

    // Delete the JSON conf after import (same pattern as key files)
    if (SD.remove(confPath)) {
        Serial.printf("[CONFIG] importCreds: deleted '%s' after import\n", confPath);
    }

    return merged;
}
