// screen_menu.cpp — main menu: Capture / WPA-SEC / HASHCRACK / Sync All / Options / Reboot / Power Off.
#include "app.h"
#include "../core/net_link.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include "../modes/oink.h"
#include "../core/mode_manager.h"
#include "../web/wpasec.h"
#include "../web/ohc.h"
#include "../web/pwncrack.h"
#include "../web/cracks.h"
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace ScreenMenu {

static const char* kItems[] = { "CAPTURE", "CAPTURE TARGETED", "WPASEC SYNC", "OHC SYNC",
                                "PWNCRACK SYNC", "CAPTURES", "STATS", "OPTIONS", "CONFIG AP",
                                "REBOOT", "POWER OFF" };
static constexpr int kCount = sizeof(kItems) / sizeof(kItems[0]);
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

static void bssidHex(uint64_t b, char out[13]) {
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             (uint8_t)(b >> 40), (uint8_t)(b >> 32), (uint8_t)(b >> 24),
             (uint8_t)(b >> 16), (uint8_t)(b >> 8), (uint8_t)b);
}

// Scan + pick an AP to add as a MANUAL never-attack entry.
static void addIgnoreFlow() {
    App::clear(); App::header("ADD IGNORE"); App::centerMsg("scanning...", TFT_CYAN);
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);   // scanning while associated can return 0 networks
    delay(300);
    int n = WiFi.scanNetworks(false, true);
    if (n < 0) { WiFi.scanDelete(); delay(400); n = WiFi.scanNetworks(false, true); }
    if (n <= 0) { App::centerMsg("no networks", TFT_RED); App::footer("back"); waitBack(); WiFi.scanDelete(); return; }
    if (n > 30) n = 30;
    static uint8_t bss[30][6]; static char rb[30][40]; static const char* rp[30];
    for (int i = 0; i < n; i++) {
        memcpy(bss[i], WiFi.BSSID(i), 6);
        String s = WiFi.SSID(i);
        snprintf(rb[i], sizeof(rb[i]), "%-18.18s %d", s.length() ? s.c_str() : "(hidden)", (int)WiFi.RSSI(i));
        rp[i] = rb[i];
    }
    int s = 0, first = 0; constexpr int VIS = 7; bool redraw = true;
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back) { WiFi.scanDelete(); return; }
        if (porkhal::vkey.up)   { s = (s + n - 1) % n; redraw = true; }
        if (porkhal::vkey.down) { s = (s + 1) % n;     redraw = true; }
        if (s < first) first = s;
        if (s >= first + VIS) first = s - VIS + 1;
        if (porkhal::vkey.enter) { OinkMode::excludeNetworkByBSSID(bss[s], WiFi.SSID(s).c_str()); WiFi.scanDelete(); return; }
        if (redraw) { App::clear(); App::header("ADD IGNORE"); App::drawList(rp, n, s, first, VIS, 1); App::footer("click: ignore  back: cancel"); redraw = false; }
        delay(20);
    }
}

// Full-screen WiFi-join QR for a cracked network. Phones parse the standard
// WIFI:S:<ssid>;T:WPA;P:<pass>;; string and offer to join on scan.
static void showWifiQR(const char* ssid, const char* pass) {
    auto esc = [](const char* in, char* out, size_t cap) {   // escape \ ; , : " per spec
        size_t o = 0;
        for (size_t i = 0; in && in[i] && o < cap - 2; i++) {
            char c = in[i];
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out[o++] = '\\';
            out[o++] = c;
        }
        out[o] = '\0';
    };
    char es[80], ep[140], payload[240];
    esc(ssid, es, sizeof(es));
    esc(pass, ep, sizeof(ep));
    snprintf(payload, sizeof(payload), "WIFI:S:%s;T:WPA;P:%s;;", es, ep);

    App::clear(); App::header("WIFI QR");
    const int sz = 134, x = (PORK_DISPLAY_W - sz) / 2, y = 30;
    M5.Display.fillRect(x - 4, y - 4, sz + 8, sz + 8, TFT_WHITE);  // quiet-zone backdrop
    M5.Display.qrcode(payload, x, y, sz, 1, true);                 // v1 auto-grows to fit; margin on
    while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
}

// Detail view for a registry entry: SSID/BSSID, type, seen-time, password; an
// action selector (show QR if cracked, delete/forget). back returns.
static void captureDetail(int idx) {
    if (idx < 0 || idx >= (int)OinkMode::getExcludedCount()) return;
    OinkMode::BoarBro e = OinkMode::getExcludedList()[idx];   // copy (list shifts on delete)
    char hb[13]; bssidHex(e.bssid, hb);
    bool cracked = Cracks::isCracked(hb);
    const char* pass = cracked ? Cracks::getPassword(hb) : "";
    const char* qrSsid = e.ssid[0] ? e.ssid : WPASec::getSSID(hb);
    const int nActions = cracked ? 2 : 1;   // [SHOW QR], DELETE
    int action = 0; bool redraw = true;

    while (true) {
        if (redraw) {
            App::clear(); App::header("NETWORK");
            M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left); M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            char line[64]; int y = 30;
            snprintf(line, sizeof(line), "%.30s", e.ssid[0] ? e.ssid : "(unknown ssid)"); M5.Display.drawString(line, 6, y); y += 14;
            snprintf(line, sizeof(line), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c", hb[0],hb[1],hb[2],hb[3],hb[4],hb[5],hb[6],hb[7],hb[8],hb[9],hb[10],hb[11]);
            M5.Display.drawString(line, 6, y); y += 14;
            const char* type = (e.flags & OinkMode::BB_CAPTURED)
                             ? ((e.flags & OinkMode::BB_MANUAL) ? "captured + ignored" : "captured")
                             : "manual ignore";
            M5.Display.setTextColor((e.flags & OinkMode::BB_CAPTURED) ? TFT_GREEN : TFT_CYAN, TFT_BLACK);
            M5.Display.drawString(type, 6, y); y += 14;
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            if (e.ts > 1700000000UL) { time_t t = e.ts; struct tm tmv; localtime_r(&t, &tmv); strftime(line, sizeof(line), "seen: %Y-%m-%d %H:%M", &tmv); }
            else strncpy(line, "seen: (time unknown)", sizeof(line));
            M5.Display.drawString(line, 6, y); y += 14;
            snprintf(line, sizeof(line), "up wpa:%s ohc:%s pwn:%s",
                     WPASec::isUploaded(hb) ? "y" : "n", OHC::isUploaded(hb) ? "y" : "n",
                     PwnCrack::isUploaded(hb) ? "y" : "n");
            M5.Display.drawString(line, 6, y); y += 14;
            if (cracked) {
                M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
                snprintf(line, sizeof(line), "PW: %s", (pass && pass[0]) ? pass : "(?)");
                M5.Display.drawString(line, 6, y); y += 14;
            }
            // action selector
            y += 4;
            const char* acts[2]; int ai = 0;
            if (cracked) acts[ai++] = "SHOW WIFI QR";
            acts[ai++] = "DELETE (forget)";
            for (int a = 0; a < nActions; a++) {
                bool seld = (a == action);
                M5.Display.setTextColor(seld ? TFT_BLACK : TFT_WHITE, seld ? TFT_CYAN : TFT_BLACK);
                snprintf(line, sizeof(line), " %s %s ", seld ? ">" : " ", acts[a]);
                M5.Display.drawString(line, 6, y); y += 13;
            }
            App::footer(nActions > 1 ? "turn: pick  click: do  back: return" : "click: delete  back: return");
            redraw = false;
        }
        M5Cardputer.update();
        if (porkhal::vkey.back) return;
        if ((porkhal::vkey.up || porkhal::vkey.down) && nActions > 1) { action = (action + 1) % nActions; redraw = true; }
        if (porkhal::vkey.enter) {
            if (cracked && action == 0) { showWifiQR(qrSsid, pass); redraw = true; continue; }  // SHOW QR
            OinkMode::removeBoarBro(e.bssid);   // DELETE: forget -> re-capturable, no longer listed
            return;
        }
        delay(20);
    }
}

// CAPTURES: the persistent registry — captured networks (survive file deletion)
// plus manual never-attack entries. Row 0 adds a manual ignore; click a network
// for detail + delete. All entries are excluded from capture.
static constexpr int CAP_MAX = 160;
static void capturesFlow() {
    App::clear(); App::header("CAPTURES"); App::centerMsg("loading...", TFT_CYAN);
    OinkMode::loadBoarBros();
    OinkMode::importCapturedFiles();
    static char rb[CAP_MAX][48]; static const char* rp[CAP_MAX];
    int count = 1;
    auto rebuild = [&]() {
        const OinkMode::BoarBro* list = OinkMode::getExcludedList();
        int nreg = OinkMode::getExcludedCount();
        snprintf(rb[0], sizeof(rb[0]), ">> ADD IGNORE (scan)"); rp[0] = rb[0];
        for (int i = 0; i < nreg && i < CAP_MAX - 1; i++) {
            char hb[13]; bssidHex(list[i].bssid, hb);
            char cm[3]; int p = 0;
            if (list[i].flags & OinkMode::BB_CAPTURED) cm[p++] = 'C';
            if (list[i].flags & OinkMode::BB_MANUAL)   cm[p++] = 'M';
            cm[p] = '\0';
            char st[8]; int sp = 0;   // upload/crack status
            if (WPASec::isUploaded(hb))   st[sp++] = 'W';
            if (OHC::isUploaded(hb))      st[sp++] = 'O';
            if (PwnCrack::isUploaded(hb)) st[sp++] = 'P';
            if (Cracks::isCracked(hb))    st[sp++] = 'K';
            st[sp] = '\0';
            // Status tag first so it stays visible; SSID gets the full width (up to 32).
            snprintf(rb[i + 1], sizeof(rb[i + 1]), "%-2s %-3s %.32s", cm, st,
                     list[i].ssid[0] ? list[i].ssid : hb);
            rp[i + 1] = rb[i + 1];
        }
        count = (nreg < CAP_MAX - 1 ? nreg : CAP_MAX - 1) + 1;
    };
    rebuild();
    int sel = 0, first = 0; constexpr int VIS = 7; bool redraw = true;
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back) break;
        if (porkhal::vkey.up)   { sel = (sel + count - 1) % count; redraw = true; }
        if (porkhal::vkey.down) { sel = (sel + 1) % count;         redraw = true; }
        if (sel < first) first = sel;
        if (sel >= first + VIS) first = sel - VIS + 1;
        if (porkhal::vkey.enter) {
            if (sel == 0) addIgnoreFlow();
            else          captureDetail(sel - 1);
            rebuild();
            if (sel >= count) sel = count - 1;
            first = 0; redraw = true;
        }
        if (redraw) {
            App::clear(); App::header("CAPTURES");
            App::drawList(rp, count, sel, first, VIS, 1);
            App::footer("C/M cap/man  W/O/P uploaded  K cracked");
            redraw = false;
        }
        delay(20);
    }
    OinkMode::saveBoarBros();
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
    uint16_t crackedPwn = PwnCrack::getCrackedCount();
    uint64_t total = Storage::totalBytes(), used = Storage::usedBytes();
    uint64_t freeB = total > used ? total - used : 0;

    App::clear(); App::header("STATS");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[48]; int y = 32;
    snprintf(line, sizeof(line), "pcap captures : %d", pcap);  M5.Display.drawString(line, 6, y); y += 16;
    snprintf(line, sizeof(line), ".22000 hashes : %d", h22);   M5.Display.drawString(line, 6, y); y += 16;
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(line, sizeof(line), "cracked wpa:%u pwn:%u", cracked, crackedPwn); M5.Display.drawString(line, 6, y); y += 16;
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(line, sizeof(line), "storage: %s  %s free", Storage::backendName(), App::fmtBytes(freeB));
    M5.Display.drawString(line, 6, y);
    App::footer("back: menu");
    waitBack();
    dirty = true;
}

// CAPTURE TARGETED: scan, pick one AP, lock the engine to it, enter Capture.
static void captureTargetedFlow() {
    App::clear(); App::header("PICK TARGET"); App::centerMsg("scanning...", TFT_CYAN);
    WiFi.scanDelete();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);   // scanning while associated can return 0 networks
    delay(300);
    int n = WiFi.scanNetworks(false, true);
    if (n < 0) { WiFi.scanDelete(); delay(400); n = WiFi.scanNetworks(false, true); }
    if (n <= 0) { App::centerMsg("no networks", TFT_RED); App::footer("back"); waitBack(); WiFi.scanDelete(); dirty = true; return; }
    if (n > 30) n = 30;
    static uint8_t bss[30][6]; static char rb[30][40]; static const char* rp[30];
    for (int i = 0; i < n; i++) {
        memcpy(bss[i], WiFi.BSSID(i), 6);
        bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        String s = WiFi.SSID(i);
        snprintf(rb[i], sizeof(rb[i]), "%c %-15.15s %d", secured ? '*' : ' ',
                 s.length() ? s.c_str() : "(hidden)", (int)WiFi.RSSI(i));
        rp[i] = rb[i];
    }
    int s = 0, first = 0; constexpr int VIS = 7; bool redraw = true;
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back) { WiFi.scanDelete(); dirty = true; return; }
        if (porkhal::vkey.up)   { s = (s + n - 1) % n; redraw = true; }
        if (porkhal::vkey.down) { s = (s + 1) % n;     redraw = true; }
        if (s < first) first = s;
        if (s >= first + VIS) first = s - VIS + 1;
        if (porkhal::vkey.enter) {
            OinkMode::setTargetLock(bss[s], WiFi.SSID(s).c_str());
            WiFi.scanDelete();
            ModeManager::enterCapture(false);   // capture, keep the lock
            return;
        }
        if (redraw) {
            App::clear(); App::header("PICK TARGET");
            App::drawList(rp, n, s, first, VIS, 1);
            App::footer("*=secured  click: capture it  back: cancel");
            redraw = false;
        }
        delay(20);
    }
}

void tick(const App::Input& in) {
    if (in.up)   { sel = (sel + kCount - 1) % kCount; dirty = true; }
    if (in.down) { sel = (sel + 1) % kCount;          dirty = true; }
    // Keep the selection within the visible window (scrolls for >VISIBLE items).
    if (sel < firstVisible)            firstVisible = sel;
    if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;
    if (in.enter) {
        switch (sel) {
            case 0: ModeManager::enterCapture(); return;   // clears lock, marks ready
            case 1: captureTargetedFlow();          return;
            case 2: App::go(App::Screen::MANAGE);   return;
            case 3: App::go(App::Screen::OHC);      return;
            case 4: App::go(App::Screen::PWNCRACK); return;
            case 5: capturesFlow();                 return;
            case 6: statsFlow();                    return;
            case 7: App::go(App::Screen::OPTIONS);  return;
            case 8: App::go(App::Screen::CONFIGAP); return;
            case 9: reboot();                       return;
            case 10: powerOff();                    return;
        }
    }
    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenMenu
