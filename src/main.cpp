// tembed-oink — minimal WiFi handshake capture + wpa-sec upload.
// Target: LilyGo T-Embed CC1101 (ESP32-S3, rotary encoder + side button).
#include <vector>
#include <Arduino.h>
#include <M5Cardputer.h>            // shim -> hal/m5compat.h on this board
#include <WiFi.h>
#include "core/config.h"
#include "core/boot_sync.h"
#include "core/net_link.h"
#include "core/storage.h"
#include "core/sd_layout.h"
#include "web/wpasec.h"
#include "web/ohc.h"
#include "web/pwncrack.h"
#include <FS.h>
#if defined(__has_include)
#  if __has_include("core/dev_secrets.h")
#    include "core/dev_secrets.h"
#  endif
#endif
#include "core/network_recon.h"
#include "modes/oink.h"
#include "app/app.h"
#include "core/mode_manager.h"
#include "version.h"

RTC_NOINIT_ATTR uint32_t bootSyncMagic;
RTC_NOINIT_ATTR uint32_t bootSyncQueue;
RTC_NOINIT_ATTR char     bootSyncResult[96];
RTC_NOINIT_ATTR uint32_t bootShowMgmt;

// Append a short segment to the accumulated bootSyncResult (space-separated, bounded).
static void appendSyncSeg(const char* seg) {
    size_t len = strlen(bootSyncResult);
    if (len && len < sizeof(bootSyncResult) - 1) bootSyncResult[len++] = ' ';
    strncpy(bootSyncResult + len, seg, sizeof(bootSyncResult) - 1 - len);
    bootSyncResult[sizeof(bootSyncResult) - 1] = 0;
}

// PwnCrack upload = one handshake per .22000 file. Collect basenames and CLOSE the
// directory first so no FS handles are held during the heap-hungry TLS handshake.
static void pwnUploadAll(char* seg, size_t segLen) {
    int files = 0, hashes = 0; char uperr[48] = {0};
    std::vector<String> names;
    {
        File d = Storage::fs().open(SDLayout::handshakesDir());
        if (d && d.isDirectory()) {
            for (File f = d.openNextFile(); f; f = d.openNextFile()) {
                if (!f.isDirectory()) {
                    const char* n = f.name(); const char* sl = strrchr(n, '/'); if (sl) n = sl + 1;
                    size_t L = strlen(n);
                    if (L > 6 && strcmp(n + L - 6, ".22000") == 0) names.push_back(n);
                }
                f.close();
            }
            d.close();
        }
    }
    for (auto& nm : names) {
        PwnCrack::UploadResult r = PwnCrack::uploadFile(nm.c_str());
        if (r.success) { files++; hashes += r.hashes; }
        else if (!uperr[0]) strncpy(uperr, r.error, sizeof(uperr) - 1);
    }
    snprintf(seg, segLen, "pwnUp%d %s", files, uperr);
}

// Run ONE queued sync op early in boot (clean heap, one TLS handshake). If more ops
// remain in the queue, reboot to run the next at clean heap (chained). Returns true
// on the final op (so the caller shows the accumulated result on the splash).
static bool runBootSyncIfQueued() {
    if (bootSyncMagic != BOOT_SYNC_MAGIC || bootSyncQueue == 0) return false;

    uint32_t op = bootSyncQueue & (uint32_t)(-(int32_t)bootSyncQueue);  // lowest set bit
    bootSyncQueue &= ~op;                                               // consume it

    if (WiFi.status() != WL_CONNECTED) NetLink::connectConfigured();

    char seg[64] = {0};
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(seg, "uplink?", sizeof(seg) - 1);
    } else if (op == SYNC_WPA_UP) {
        WPASecSyncResult r = WPASec::syncCaptures(nullptr, /*doDownload=*/false);
        if (r.success) snprintf(seg, sizeof(seg), "wpaUp%u sk%u", r.uploaded, r.skipped);
        else           snprintf(seg, sizeof(seg), "wpaUp ERR:%.40s", WPASec::getLastError());
    } else if (op == SYNC_OHC_UP) {
        OHC::UploadResult r = OHC::uploadHashes();
        if (r.success) snprintf(seg, sizeof(seg), "ohcUp%u sk%u", r.accepted, r.skipped);
        else           snprintf(seg, sizeof(seg), "ohcUp ERR:%.40s", r.error);
    } else if (op == SYNC_PWN_UP) {
        pwnUploadAll(seg, sizeof(seg));
    } else if (op == SYNC_WPA_CHK) {
        uint16_t nc = 0;
        if (WPASec::downloadPotfile(nc)) snprintf(seg, sizeof(seg), "wpaCrk+%u", nc);
        else                             snprintf(seg, sizeof(seg), "wpaCrk ERR:%.38s", WPASec::getLastError());
    } else if (op == SYNC_PWN_CHK) {
        char err[48] = {0};
        int c = PwnCrack::syncPotfile(err, sizeof(err));
        if (c >= 0) snprintf(seg, sizeof(seg), "pwnCrk%d", c);
        else        snprintf(seg, sizeof(seg), "pwnCrk ERR:%.38s", err);
    }
    appendSyncSeg(seg);
    Serial.printf("[SYNC] op=0x%x done -> %s | queue left=0x%x\n",
                  (unsigned)op, seg, (unsigned)bootSyncQueue);

    if (bootSyncQueue != 0) {
        delay(200);
        ESP.restart();   // run the next op at clean heap (does not return)
    }
    return true;   // last op — show the accumulated result on the splash
}

void setup() {
#if defined(PORK_BOARD_TEMBED_CC1101)
    // BOARD_PWR_EN (GPIO15): power the peripheral rail (display/backlight).
    // T-Embed-specific; the T-Display C5 has no such rail-enable pin (its LCD
    // power-enable, GPIO25, is driven in bringUpHardware()).
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
#endif

    Serial.begin(115200);
    delay(100);
    Serial.println("\n[OINK] tembed-oink boot");

    // CRITICAL ORDER: connect WiFi BEFORE initialising the display.
    // LovyanGFX's display SPI init grabs a shared resource (GDMA channel) that a
    // *fresh* WiFi association also needs — whoever claims it first wins. If the
    // display inits first, every later WiFi connect fails (AUTH_EXPIRE/reason 2).
    // So we bring WiFi up and associate here, keep it alive, then init the display;
    // the wpa-sec sync reuses this live connection.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Config::init();                       // load creds (SD; fine before display)
#if defined(DEV_WIFI_SSID)
    // DEV: a git-ignored core/dev_secrets.h can pin the uplink creds so they never
    // need re-entering during development (overrides stored config each boot).
    if (DEV_WIFI_SSID[0]) {
        strncpy(Config::wifi().otaSSID,     DEV_WIFI_SSID, sizeof(Config::wifi().otaSSID) - 1);
        strncpy(Config::wifi().otaPassword, DEV_WIFI_PASS, sizeof(Config::wifi().otaPassword) - 1);
        Serial.println("[DEV] uplink creds overridden from dev_secrets.h");
    }
#endif
#if defined(DEV_PWNCRACK_KEY)
    if (DEV_PWNCRACK_KEY[0]) {
        strncpy(Config::wifi().pwncrackKey, DEV_PWNCRACK_KEY, sizeof(Config::wifi().pwncrackKey) - 1);
        Config::wifi().pwncrackKey[sizeof(Config::wifi().pwncrackKey) - 1] = '\0';
        Serial.println("[DEV] PwnCrack key overridden from dev_secrets.h");
    }
#endif
#if defined(DEV_OHC_KEY)
    if (DEV_OHC_KEY[0]) {
        strncpy(Config::wifi().ohcKey, DEV_OHC_KEY, sizeof(Config::wifi().ohcKey) - 1);
        Config::wifi().ohcKey[sizeof(Config::wifi().ohcKey) - 1] = '\0';
        Serial.println("[DEV] OHC key overridden from dev_secrets.h");
    }
#endif
    const char* ssid = Config::wifi().otaSSID;
    if (ssid && ssid[0]) {
#if PORK_LED_COUNT > 0
        neopixelWrite(PORK_LED_PIN, 0, 0, 30);   // blue LED: connecting at boot
#endif
        WiFi.begin(ssid, Config::wifi().otaPassword);
        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) { delay(100); }
#if PORK_LED_COUNT > 0
        neopixelWrite(PORK_LED_PIN, 0, 0, 0);
#endif
        Serial.printf("[OINK] boot wifi: %s\n",
                      WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
        // Best-effort NTP (async) so capture registry timestamps are real.
        if (WiFi.status() == WL_CONNECTED)
            configTime(0, 0, "pool.ntp.org", "time.google.com");
    }

    // Reboot-to-sync: run the queued upload BEFORE the display inits (WiFi/display
    // GDMA + heap). No on-screen feedback here; result shows on the Status page after.
    if (bootSyncMagic != BOOT_SYNC_MAGIC) strncpy(bootSyncResult, "idle", sizeof(bootSyncResult));
    bootShowMgmt = 0;   // (boot-to-management-after-sync reverted — SoftAP-at-boot was unstable;
                        //  boot into CAPTURE as usual, toggle to management to see the result)
    bool syncRan = runBootSyncIfQueued();

    // Now the display + input + engine (display init no longer disturbs WiFi).
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5.Display.setBrightness(Config::wifi().displayBrightness);

#if !defined(PORK_BOARD_TDISPLAY_C5)
    // The SD shares the SPI bus with the display; retry the mount now that the
    // ST7789 is initialised (it wouldn't mount before the display was up).
    // The T-Display C5 has no SD card (captures live on internal LittleFS).
    Config::mountSdAfterDisplay();
#endif

    NetworkRecon::init();
    OinkMode::init();

    App::clear();
    App::centerMsg("BBoink", TFT_CYAN);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_center);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("v" BBOINK_VERSION, PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2 + 20);
    delay(900);
    if (syncRan) {
        App::clear();
        App::centerMsg("SYNC RESULT", TFT_CYAN);
        App::footer(bootSyncResult);
        delay(3000);   // show the upload outcome before dropping into capture
    }
    App::begin();
}

void loop() {
    M5Cardputer.update();
    App::tick();
    delay(5);
}
