// tembed-oink — minimal WiFi handshake capture + wpa-sec upload.
// Target: LilyGo T-Embed CC1101 (ESP32-S3, rotary encoder + side button).
#include <vector>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_bt.h>
#include <esp_mac.h>               // distinct BLE base MAC per firmware (GATT-cache fix)
#include <M5Cardputer.h>            // shim -> hal/m5compat.h on this board
#include <WiFi.h>
#include "core/config.h"
#include "core/boot_sync.h"
#include "core/net_link.h"
#include "core/storage.h"
#include "core/sd_layout.h"
#include "web/wpasec.h"
#include "web/ohc.h"
#include "web/relay.h"
#include "web/pwncrack.h"
#include "web/updater.h"
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
#include "switch_targets.h"

RTC_NOINIT_ATTR uint32_t bootSyncMagic;
RTC_NOINIT_ATTR uint32_t bootSyncQueue;
RTC_NOINIT_ATTR char     bootSyncResult[96];
RTC_NOINIT_ATTR uint32_t bootShowMgmt;
RTC_NOINIT_ATTR uint32_t bootBleBridge;
RTC_NOINIT_ATTR uint32_t bootFwFetch;
RTC_NOINIT_ATTR int32_t  bootSwitchIdx;

// Append a short segment to the accumulated bootSyncResult (space-separated, bounded).
static void appendSyncSeg(const char* seg) {
    size_t len = strlen(bootSyncResult);
    if (len && len < sizeof(bootSyncResult) - 1) bootSyncResult[len++] = ' ';
    strncpy(bootSyncResult + len, seg, sizeof(bootSyncResult) - 1 - len);
    bootSyncResult[sizeof(bootSyncResult) - 1] = 0;
}

// PwnCrack upload — technique #3: concatenate EVERY .22000 into one combined
// .hc22000 and upload it in a SINGLE request (one TLS handshake), instead of one
// handshake per file (which fragments the heap and aborts on the C5).
static void pwnUploadAll(char* seg, size_t segLen) {
    // Collect the source basenames first (dir closed before we touch TLS/large buffers).
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
    if (names.empty()) { snprintf(seg, segLen, "pwnUp0"); return; }

    // Merge all WPA* lines (de-duplicated) into one combined file in the handshakes dir.
    const char* combined = "pwn_all_combined.22000";   // .22000 so uploadFile finds it; renamed to .hc22000 on the wire
    char cpath[160]; snprintf(cpath, sizeof(cpath), "%s/%s", SDLayout::handshakesDir(), combined);
    Storage::fs().remove(cpath);
    File out = Storage::fs().open(cpath, FILE_WRITE);
    if (!out) { snprintf(seg, segLen, "pwnUp ERR:tmp open"); return; }
    for (auto& nm : names) {
        char sp[160]; snprintf(sp, sizeof(sp), "%s/%s", SDLayout::handshakesDir(), nm.c_str());
        File in = Storage::fs().open(sp, FILE_READ);
        if (!in) continue;
        while (in.available()) {
            String l = in.readStringUntil('\n'); l.trim();
            if (l.startsWith("WPA*") && l.length() > 20) out.println(l);
        }
        in.close();
    }
    out.close();

    PwnCrack::UploadResult r = PwnCrack::uploadFile(combined);   // ONE handshake for all hashes
    Storage::fs().remove(cpath);
    if (r.success) snprintf(seg, segLen, "pwnUp%u/%uf", r.hashes, (unsigned)names.size());
    else           snprintf(seg, segLen, "pwnUp ERR:%.40s", r.error);
}

// Firmware switch (reboot-to-switch): a portal button flagged a switch to the
// sibling firmware (PoC). WiFi is already associated (boot connect, above) and the
// display is up, so download the sibling app bin into the spare OTA slot with
// on-screen progress and boot into it. Mirrors the PoC side exactly.
// Unified reboot-to-fetch screen, drawn to match the PoC's dispCenter EXACTLY:
// a colored "FIRMWARE" title centered near the top, then a grey phase line and the
// target ("-> PoC" / "-> latest") centered below. NO header bar / footer.
static char g_fwTgt[24] = "";
static void fwDraw(const char* phase, uint16_t color) {
    auto& d = M5.Display;
    bool sm = d.width() < 200;                       // small LCD (T-Dongle 160x80)
    int tS = sm ? 2 : 3, bS = sm ? 1 : 2;            // title / body text sizes
    int ty = sm ? 4 : 12, by = sm ? 26 : 60, dy = sm ? 11 : 22;
    int W = d.width();
    d.fillScreen(TFT_BLACK); d.setTextWrap(false); d.setTextDatum(top_center);
    d.setTextColor(color, TFT_BLACK); d.setTextSize(tS); d.drawString("FIRMWARE", W / 2, ty);
    d.setTextColor(d.color565(0xC8, 0xD2, 0xDA), TFT_BLACK); d.setTextSize(bS);
    d.drawString(phase, W / 2, by);
    d.drawString(g_fwTgt, W / 2, by + 2 * dy);       // phase, blank line, target (like the PoC)
}
static void fwFetchProgress(size_t done, size_t total) {
    static int last = -1;
    int p = total ? (int)(done * 100 / total) : 0;
    if (p == last) return; last = p;
    char b[20]; snprintf(b, sizeof(b), "writing %d%%", p); fwDraw(b, M5.Display.color565(0xF7, 0xC9, 0x48));
}
// SELF = update to the latest of THIS firmware; SWITCH = flash the sibling (PoC).
// One reboot-to-fetch path for both (mirrors the PoC side). Two distinct magics.
static void runFwFetchIfQueued() {
    bool self   = (bootFwFetch == FW_FETCH_SELF);
    bool sw     = (bootFwFetch == FW_FETCH_SWITCH);
    if (!self && !sw) return;
    bootFwFetch = 0;
    const char* url = BBOINK_OTA_URL; const char* nm = "latest";
    if (sw) {                                        // switch: pick the chosen sibling
        if (bootSwitchIdx >= 0 && bootSwitchIdx < SWITCH_TARGET_COUNT) {
            url = SWITCH_TARGETS[bootSwitchIdx].url; nm = SWITCH_TARGETS[bootSwitchIdx].name;
        } else return;                               // no valid sibling on this board
    }
    snprintf(g_fwTgt, sizeof(g_fwTgt), "-> %s", nm);
    uint16_t cyan = M5.Display.color565(0x22, 0xD3, 0xE0);
    fwDraw("connecting wifi", cyan);
    if (WiFi.status() != WL_CONNECTED && !NetLink::connectConfigured()) {
        fwDraw("NO WIFI", M5.Display.color565(0xE5, 0x48, 0x4D)); delay(2800); return;
    }
    fwDraw("connecting to github", cyan);
    Updater::Result r = Updater::fetchToFlash(url, fwFetchProgress);
    if (r.ok) { fwDraw("booting", M5.Display.color565(0x3F, 0xB9, 0x50)); delay(1400); ESP.restart(); }
    fwDraw(r.error, M5.Display.color565(0xE5, 0x48, 0x4D)); delay(3200);   // fall through to normal boot
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
    } else if (op == SYNC_RELAY) {
        Relay::SyncResult rr = Relay::sync();
        if (rr.ok) snprintf(seg, sizeof(seg), "relay up%u pc%u crk%u", rr.hashesUp, rr.pcapsUp, rr.cracked);
        else       snprintf(seg, sizeof(seg), "relay ERR:%.40s", rr.error);
    } else if (op == SYNC_RELAY_PING) {
        Relay::PingResult pr = Relay::ping();
        snprintf(seg, sizeof(seg), "relay %s", pr.status);
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
    // Distinct BLE identity per firmware (shared fix with the PoC): derive the base
    // MAC from the chip's factory MAC with a firmware-specific tweak so BBoink and
    // the PoC advertise DIFFERENT BLE addresses on the SAME board. Otherwise a
    // firmware switch keeps the same address and the host's GATT cache (BlueZ on
    // Linux, keyed by MAC) stays stale, so the portal's writes land on dead handles.
    // Must run before any WiFi/BLE init.
    { uint8_t mac[6]; if (esp_efuse_mac_get_default(mac) == ESP_OK) { mac[5] ^= 0xB0; esp_base_mac_addr_set(mac); } }
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
    // Read the BLE-bridge boot flag first: on a NORMAL boot we don't use BLE, so
    // release the BT controller RAM (~40KB) back to the heap — NimBLE reserves it
    // at startup and it otherwise starves OinkMode::init(). Bridge boots keep it.
    bool g_bleBridge = (bootBleBridge == BOOT_SYNC_MAGIC);
    bootBleBridge = 0;
#if defined(PORK_BOARD_WAVESHARE_C5_LCD) || defined(PORK_BOARD_CARDPUTER_ADV) || defined(PORK_BOARD_TDONGLE_S3)
    // Waveshare / Cardputer ADV NimBLE-heap fix: reclaim the BT-controller RAM on
    // non-bridge boots (these boards have no PSRAM, so NimBLE's ~40KB otherwise
    // starves OinkMode::init() and the engine hangs — black screen after display init).
    if (!g_bleBridge) esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
#endif

    // CRITICAL ORDER: connect WiFi BEFORE initialising the display.
    // LovyanGFX's display SPI init grabs a shared resource (GDMA channel) that a
    // *fresh* WiFi association also needs — whoever claims it first wins. If the
    // display inits first, every later WiFi connect fails (AUTH_EXPIRE/reason 2).
    // So we bring WiFi up and associate here, keep it alive, then init the display;
    // the wpa-sec sync reuses this live connection.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Config::init();                       // load creds (SD; fine before display)
    // BLE bridge boot: skip all WiFi (bridge runs radio as BLE only).
    if (g_bleBridge) { WiFi.mode(WIFI_OFF); ModeManager::forceBleBridgeBoot(); }
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
#if defined(DEV_WPASEC_KEY)
    if (DEV_WPASEC_KEY[0]) {
        strncpy(Config::wifi().wpaSecKey, DEV_WPASEC_KEY, sizeof(Config::wifi().wpaSecKey) - 1);
        Config::wifi().wpaSecKey[sizeof(Config::wifi().wpaSecKey) - 1] = '\0';
        Serial.println("[DEV] WPA-SEC key overridden from dev_secrets.h");
    }
#endif
#if defined(DEV_OHC_KEY)
    if (DEV_OHC_KEY[0]) {
        strncpy(Config::wifi().ohcKey, DEV_OHC_KEY, sizeof(Config::wifi().ohcKey) - 1);
        Config::wifi().ohcKey[sizeof(Config::wifi().ohcKey) - 1] = '\0';
        Serial.println("[DEV] OHC key overridden from dev_secrets.h");
    }
#endif
#if defined(DEV_RELAY_URL)
    if (DEV_RELAY_URL[0]) {
        strncpy(Config::wifi().relayUrl,   DEV_RELAY_URL,   sizeof(Config::wifi().relayUrl) - 1);
        strncpy(Config::wifi().relayToken, DEV_RELAY_TOKEN, sizeof(Config::wifi().relayToken) - 1);
        Serial.println("[DEV] relay url/token overridden from dev_secrets.h");
    }
#endif
    // Durable keystore: migrate the just-loaded creds into NVS and restore any that
    // the config file was missing (e.g. after a reflash/app-swap wiped SD/SPIFFS).
    Config::keystoreBoot();
    const char* ssid = Config::wifi().otaSSID;
    if (!g_bleBridge && ssid && ssid[0]) {
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
    bool syncRan = g_bleBridge ? false : runBootSyncIfQueued();

    // Now the display + input + engine (display init no longer disturbs WiFi).
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5.Display.setBrightness(Config::wifi().displayBrightness);

    // Firmware fetch (if flagged by the portal — update or switch): WiFi is up and
    // the display is ready — flash + boot into it (does not return on success).
    runFwFetchIfQueued();


#if !defined(PORK_BOARD_TDISPLAY_C5)
    // The SD shares the SPI bus with the display; retry the mount now that the
    // ST7789 is initialised (it wouldn't mount before the display was up).
    // The T-Display C5 has no SD card (captures live on internal LittleFS).
    Config::mountSdAfterDisplay();
#endif

    // Reclaim the ~60KB the boot WiFi STA holds before the engine inits. A CAPTURE
    // boot re-establishes its own promiscuous radio in NetworkRecon::start() (which
    // needs no association, so no GDMA race), and this headroom matters now that
    // NimBLE's static RAM is linked in. Skip for a MANAGEMENT boot (needs the STA up).
#if defined(PORK_BOARD_WAVESHARE_C5_LCD) || defined(PORK_BOARD_CARDPUTER_ADV) || defined(PORK_BOARD_TDONGLE_S3)
    if (!g_bleBridge && Config::wifi().bootModePolicy != 2) {
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);   // capture uses promiscuous mode only; keeping the STA up
    }                          // races the flash writes (pcap save hangs ~1min in)
#endif
#if defined(PORK_BOARD_CARDPUTER_ADV)
    M5.Display.fillScreen(0x001F); delay(500);   // BLUE
#endif
    NetworkRecon::init();
#if defined(PORK_BOARD_CARDPUTER_ADV)
    M5.Display.fillScreen(0xFFE0); delay(500);   // YELLOW
#endif
    OinkMode::init();
#if defined(PORK_BOARD_CARDPUTER_ADV)
    M5.Display.fillScreen(0xF81F); delay(500);   // MAGENTA
#endif

    {   // graphical boot splash (shared style with the hid-ble-poc firmware)
        auto& d = M5.Display;
        const int W = d.width(), H = d.height();
        const bool big = W >= 240;
        uint32_t cyan = d.color888(0x22, 0xD3, 0xE0), mag = d.color888(0xE8, 0x79, 0xF9);
        uint32_t dim  = d.color888(0x5A, 0x67, 0x72), dark = d.color888(0x0B, 0x2A, 0x2E);
        d.fillScreen(0x000000u);
        for (int y = 0; y < H; y += 4) d.drawFastHLine(0, y, W, d.color888(0x0A, 0x12, 0x16));  // faint scanlines
        int kw = big ? 18 : 10, kh = big ? 14 : 8, gap = big ? 5 : 3;                            // decorative key band
        int kn = (W - 16) / (kw + gap); if (kn > 12) kn = 12; if (kn < 1) kn = 1;
        int startx = (W - (kn * (kw + gap) - gap)) / 2, ky = (int)(H * 0.16);
        for (int i = 0; i < kn; i++) d.fillRoundRect(startx + i * (kw + gap), ky, kw, kh, 2, (i % 5 == 2) ? cyan : dark);
        d.setTextDatum(middle_center);
        d.setTextSize(big ? 5 : 3);
        d.setTextColor(dark); d.drawString("BBoink", W / 2 + 2, H / 2 + 2);   // drop shadow
        d.setTextColor(cyan); d.drawString("BBoink", W / 2, H / 2);
        d.setTextSize(big ? 2 : 1);
        d.setTextColor(mag); d.drawString("handshake hunter", W / 2, H / 2 + (big ? 30 : 14));
        if (big) { int uw = (int)(W * 0.5); d.fillRect((W - uw) / 2, H / 2 + 46, uw, 2, cyan); } // accent underline
        d.setTextSize(1); d.setTextColor(dim);
        d.setTextDatum(bottom_center); d.drawString("v" BBOINK_VERSION, W / 2, H - 3);
        d.setTextDatum(top_left);
        delay(1400);
    }
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
