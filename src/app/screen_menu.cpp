// screen_menu.cpp — main menu: Capture / WPA-SEC / HASHCRACK / Sync All / Options / Reboot / Power Off.
#include "app.h"
#include "../core/net_link.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include "../modes/oink.h"
#include "../web/wpasec.h"
#include "../web/ohc.h"
#include <SD.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace ScreenMenu {

static const char* kItems[] = { "CAPTURE", "WPASEC SYNC", "OHC SYNC", "SYNC ALL",
                                "EXCLUDE APS", "STATS", "OPTIONS", "REBOOT", "POWER OFF" };
static constexpr int kCount = 9;
static constexpr int VISIBLE = 5;   // rows that fit on the 170px panel at size 2
static int sel = 0;
static int firstVisible = 0;
static bool dirty = true;

void enter() {
    sel = 0;
    firstVisible = 0;
    dirty = true;
}

static void draw() {
    App::clear();
    App::header("BBoink");
    App::drawList(kItems, kCount, sel, firstVisible, VISIBLE);
    App::footer("turn: move   click: open");
}

static void reboot() {
    App::clear();
    App::centerMsg("REBOOTING", TFT_CYAN);
    delay(700);
    ESP.restart();
}

// Power off = deep sleep with ANY-button wake (wheel GPIO0 or side button GPIO6,
// both active-low). SAFE: a wake source IS configured, so a button press boots it
// back up; and the hardware PWR-button power-cycle remains a guaranteed backup.
// (The earlier strand was deep sleep with NO wake source — fixed here.)
static void powerOff() {
    // Confirm so a stray click can't trigger it.
    App::clear();
    App::header("POWER OFF");
    App::centerMsg("click = confirm", TFT_RED);
    App::footer("back = cancel");
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back)  { dirty = true; return; }
        if (porkhal::vkey.enter) break;
        delay(20);
    }

    App::powerOff();   // shared deep-sleep sequence (also used by long-press BACK)
}

static void waitBack() {
    while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
}

static void syncProgress(const char* status, uint8_t p, uint8_t t) {
    M5.Display.fillRect(0, 28, PORK_DISPLAY_W, PORK_DISPLAY_H - 28 - 18, TFT_BLACK);
    char line[40];
    if (t > 0) snprintf(line, sizeof(line), "%s %u/%u", status, p, t);
    else       snprintf(line, sizeof(line), "%s", status);
    App::centerMsg(line, TFT_CYAN);
}

// SYNC ALL: connect WiFi, run the WPA-SEC sync (upload pcaps + fetch potfile),
// then the OnlineHashCrack upload — back to back — and show a combined result.
static void syncAll() {
    App::clear(); App::header("SYNC ALL"); App::centerMsg("connecting wifi...", TFT_CYAN);
    if (!NetLink::connectConfigured()) {
        App::centerMsg("NO WIFI", TFT_RED); App::footer("back"); waitBack(); dirty = true; return;
    }

    WPASecSyncResult wr = {};
    if (WPASec::hasApiKey()) {
        App::clear(); App::header("WPA-SEC");
        wr = WPASec::syncCaptures(syncProgress);
    }

    OHC::UploadResult orr = {};
    if (OHC::hasApiKey()) {
        App::clear(); App::header("ONLINEHASHCRACK"); App::centerMsg("uploading...", TFT_CYAN);
        orr = OHC::uploadHashes();
        if (orr.success) {   // mark every submitted .22000's BSSID as uploaded
            File d = Storage::fs().open(SDLayout::handshakesDir());
            if (d && d.isDirectory()) {
                for (File f = d.openNextFile(); f; f = d.openNextFile()) {
                    const char* n = f.name(); size_t L = strlen(n);
                    char b[13];
                    if (!f.isDirectory() && L > 6 && !strcmp(n + L - 6, ".22000") &&
                        SDLayout::captureBssid(n, b)) OHC::markUploaded(b);
                    f.close();
                }
                d.close();
            }
        }
    }

    App::clear(); App::header("SYNC ALL DONE");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[48]; int y = 30;
    if (WPASec::hasApiKey()) {
        snprintf(line, sizeof(line), "wpa-sec: up %u  skip %u", wr.uploaded, wr.skipped);
        M5.Display.drawString(line, 6, y); y += 15;
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        snprintf(line, sizeof(line), "cracked %u (+%u)", wr.cracked, wr.newCracked);
        M5.Display.drawString(line, 6, y); y += 17;
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    } else { M5.Display.drawString("wpa-sec: no key", 6, y); y += 15; }
    if (OHC::hasApiKey()) {
        snprintf(line, sizeof(line), "ohc: new %u skip %u rej %u", orr.accepted, orr.skipped, orr.rejected);
        M5.Display.drawString(line, 6, y); y += 15;
    } else { M5.Display.drawString("ohc: no key", 6, y); y += 15; }
    App::footer("back: menu");
    waitBack();
    dirty = true;
}

// EXCLUDE APS: scan, list nearby APs with an [X] for excluded ones; click toggles
// membership in the never-attack list (boar bros); back saves and exits.
static void exclusionsFlow() {
    App::clear(); App::header("EXCLUDE APS"); App::centerMsg("scanning...", TFT_CYAN);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);
    OinkMode::loadBoarBros();
    if (n <= 0) {
        App::centerMsg("no networks found", TFT_RED); App::footer("back"); waitBack();
        WiFi.scanDelete(); dirty = true; return;
    }
    static constexpr int MAXN = 30;
    if (n > MAXN) n = MAXN;
    static uint8_t bss[MAXN][6];
    static char rb[MAXN][40];
    static const char* rp[MAXN];
    for (int i = 0; i < n; i++) memcpy(bss[i], WiFi.BSSID(i), 6);
    auto rebuild = [&]() {
        for (int i = 0; i < n; i++) {
            String s = WiFi.SSID(i);
            snprintf(rb[i], sizeof(rb[i]), "%c %-16.16s", OinkMode::isExcluded(bss[i]) ? 'X' : ' ',
                     s.length() ? s.c_str() : "(hidden)");
            rp[i] = rb[i];
        }
    };
    rebuild();
    int s = 0, first = 0; constexpr int VIS = 7; bool redraw = true;
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back) break;
        if (porkhal::vkey.up)   { s = (s + n - 1) % n; redraw = true; }
        if (porkhal::vkey.down) { s = (s + 1) % n;     redraw = true; }
        if (s < first) first = s;
        if (s >= first + VIS) first = s - VIS + 1;
        if (porkhal::vkey.enter) {
            if (OinkMode::isExcluded(bss[s])) {
                uint64_t key = 0; for (int k = 0; k < 6; k++) key = (key << 8) | bss[s][k];
                OinkMode::removeBoarBro(key);
            } else {
                OinkMode::excludeNetworkByBSSID(bss[s], WiFi.SSID(s).c_str());
            }
            rebuild(); redraw = true;
        }
        if (redraw) {
            App::clear(); App::header("EXCLUDE APS");
            App::drawList(rp, n, s, first, VIS, 1);
            App::footer("click: toggle X   back: save");
            redraw = false;
        }
        delay(20);
    }
    OinkMode::saveBoarBros();
    WiFi.scanDelete();
    dirty = true;
}

// STATS: current on-disk inventory + cracked count + free space.
static void statsFlow() {
    int pcap = 0, h22 = 0;
    File d = Storage::fs().open(SDLayout::handshakesDir());
    if (d && d.isDirectory()) {
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            const char* nm = f.name(); size_t L = strlen(nm);
            if (!f.isDirectory()) {
                if (L > 5 && !strcmp(nm + L - 5, ".pcap")) pcap++;
                else if (L > 6 && !strcmp(nm + L - 6, ".22000")) h22++;
            }
            f.close();
        }
        d.close();
    }
    uint16_t cracked = WPASec::getCrackedCount();
    uint64_t total = Storage::totalBytes(), used = Storage::usedBytes();
    uint64_t freeB = total > used ? total - used : 0;

    App::clear(); App::header("STATS");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[48]; int y = 32;
    snprintf(line, sizeof(line), "pcap captures : %d", pcap);  M5.Display.drawString(line, 6, y); y += 16;
    snprintf(line, sizeof(line), ".22000 hashes : %d", h22);   M5.Display.drawString(line, 6, y); y += 16;
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(line, sizeof(line), "cracked (pot) : %u", cracked); M5.Display.drawString(line, 6, y); y += 16;
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(line, sizeof(line), "storage: %s  %s free", Storage::backendName(), App::fmtBytes(freeB));
    M5.Display.drawString(line, 6, y);
    App::footer("back: menu");
    waitBack();
    dirty = true;
}

void tick(const App::Input& in) {
    if (in.up)   { sel = (sel + kCount - 1) % kCount; dirty = true; }
    if (in.down) { sel = (sel + 1) % kCount;          dirty = true; }
    // Keep the selection within the visible window (scrolls for >VISIBLE items).
    if (sel < firstVisible)            firstVisible = sel;
    if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;
    if (in.enter) {
        switch (sel) {
            case 0: App::go(App::Screen::CAPTURE); return;
            case 1: App::go(App::Screen::MANAGE);  return;
            case 2: App::go(App::Screen::OHC);     return;
            case 3: syncAll();                     return;
            case 4: exclusionsFlow();              return;
            case 5: statsFlow();                   return;
            case 6: App::go(App::Screen::OPTIONS); return;
            case 7: reboot();                      return;
            case 8: powerOff();                    return;
        }
    }
    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenMenu
