// screen_manage.cpp — list captures on SD and sync them to wpa-sec.
#include "app.h"
#include "../core/sd_layout.h"
#include "../core/config.h"
#include "../web/wpasec.h"
#include "testcap.h"
#include "../core/net_link.h"
#include <SD.h>
#include "../core/storage.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_random.h>

namespace ScreenManage {

static constexpr int  MAX_FILES = 80;
// Must hold the full basename: SSID(<=20) + '_' + 12-hex BSSID + "_22000.22000"
// (~45 chars). 40 truncated long names so delete (remove by name) silently failed.
static constexpr int  NAME_LEN  = 64;
static char  files[MAX_FILES][NAME_LEN];
static uint8_t fileStatus[MAX_FILES];   // 0=local, 1=uploaded, 2=cracked
static int   fileCount = 0;
static constexpr int ACTIONS = 3;  // 0=SYNC, 1=MAKE TEST CAP, 2=WIFI SCAN

static int   sel = 0;            // 0..ACTIONS-1 = actions, then files
static int   firstVisible = 0;
static constexpr int VISIBLE = 8;   // small font -> more rows fit
static bool  dirty = true;

// Row buffers handed to App::drawList (action rows + truncated filenames).
static const char* rowPtrs[MAX_FILES + ACTIONS];
static char        rowBuf[MAX_FILES + ACTIONS][NAME_LEN + 4];

static void scan() {
    fileCount = 0;
    if (!Config::isSDAvailable()) return;
    WPASec::loadCache();   // for isUploaded()/isCracked() status (offline)
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    File f = d.openNextFile();
    while (f && fileCount < MAX_FILES) {
        if (!f.isDirectory()) {
            const char* n = f.name();
            const char* slash = strrchr(n, '/');
            if (slash) n = slash + 1;
            size_t len = strlen(n);
            // wpa-sec uploads packet captures only -> list .pcap only.
            bool cap = (len > 5 && strcmp(n + len - 5, ".pcap") == 0);
            if (cap) {
                strncpy(files[fileCount], n, NAME_LEN - 1);
                files[fileCount][NAME_LEN - 1] = '\0';
                char bssid[13]; uint8_t st = 0;
                if (SDLayout::captureBssid(n, bssid)) {
                    if (WPASec::isCracked(bssid)) st = 2;
                    else if (WPASec::isUploaded(bssid)) st = 1;
                }
                fileStatus[fileCount] = st;
                fileCount++;
            }
        }
        f.close();
        f = d.openNextFile();
    }
    d.close();
}

static void rebuildRows() {
    snprintf(rowBuf[0], sizeof(rowBuf[0]), ">> SYNC TO WPA-SEC");
    snprintf(rowBuf[1], sizeof(rowBuf[1]), ">> MAKE TEST CAP");
    snprintf(rowBuf[2], sizeof(rowBuf[2]), ">> WIFI SCAN");
    rowPtrs[0] = rowBuf[0];
    rowPtrs[1] = rowBuf[1];
    rowPtrs[2] = rowBuf[2];
    for (int i = 0; i < fileCount; i++) {
        const char* tag = fileStatus[i] == 2 ? "CRK" : fileStatus[i] == 1 ? "UP " : "-  ";
        snprintf(rowBuf[i + ACTIONS], sizeof(rowBuf[i + ACTIONS]), "%s %.59s", tag, files[i]);
        rowPtrs[i + ACTIONS] = rowBuf[i + ACTIONS];
    }
}

static int rowCount() { return fileCount + ACTIONS; }

void enter() {
    scan();
    rebuildRows();
    sel = 0;
    firstVisible = 0;
    dirty = true;
}

static void draw() {
    App::clear();
    int up = 0, crk = 0;
    for (int i = 0; i < fileCount; i++) { if (fileStatus[i] == 2) crk++; else if (fileStatus[i] == 1) up++; }
    char title[28];
    snprintf(title, sizeof(title), "WPA-SEC %d U%d C%d", fileCount, up, crk);
    App::header(title);
    App::drawList(rowPtrs, rowCount(), sel, firstVisible, VISIBLE, 1);
    uint64_t freeB = Storage::totalBytes() > Storage::usedBytes()
                   ? Storage::totalBytes() - Storage::usedBytes() : 0;
    char foot[48];
    snprintf(foot, sizeof(foot), "click=del  CRK/UP/-   %s free", App::fmtBytes(freeB));
    App::footer(foot);
}

// --- wpa-sec sync ---------------------------------------------------------

static void syncProgress(const char* status, uint8_t progress, uint8_t total) {
    M5.Display.fillRect(0, 28, PORK_DISPLAY_W, PORK_DISPLAY_H - 28 - 18, TFT_BLACK);
    char line[40];
    if (total > 0) snprintf(line, sizeof(line), "%s %u/%u", status, progress, total);
    else           snprintf(line, sizeof(line), "%s", status);
    App::centerMsg(line, TFT_CYAN);
}

static int g_wifiStatus = 0;
static volatile int g_discReason = 0;   // last STA disconnect reason code
static int g_pwLen = 0;

static bool connectWiFi() {
    const char* ssid = Config::wifi().otaSSID;
    const char* pass = Config::wifi().otaPassword;
    g_pwLen = pass ? (int)strlen(pass) : 0;
    if (!ssid || ssid[0] == '\0') { g_wifiStatus = -1; return false; }
    g_discReason = 0;
    static bool evtReg = false;
    if (!evtReg) {
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info){
            g_discReason = info.wifi_sta_disconnected.reason;
        }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        evtReg = true;
    }
    // Reuse the boot-held association or reconnect on the running driver
    // (see NetLink::connectConfigured). After a Capture run the radio is in
    // promiscuous mode; if reconnect here fails, reboot so WiFi re-associates
    // before the display claims the shared GDMA resource.
    bool ok = NetLink::connectConfigured();
    g_wifiStatus = WiFi.status();
    return ok;
}

// On connect failure, show why: status code + whether the SSID is visible in a
// scan (rules out 5GHz-only / wrong name / out of range vs wrong password).
static void showWifiDiag() {
    const char* ssid = Config::wifi().otaSSID;
    App::clear(); App::header("WIFI FAILED");
    M5.Display.setTextSize(2); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[48]; int y = 30;
    snprintf(line, sizeof(line), "ssid:%.16s", (ssid && ssid[0]) ? ssid : "(empty!)");
    M5.Display.drawString(line, 6, y); y += 22;
    const char* st = "?";
    switch (g_wifiStatus) {
        case -1: st="no ssid set"; break;
        case WL_NO_SSID_AVAIL: st="not found"; break;
        case WL_CONNECT_FAILED: st="auth/pw fail"; break;
        case WL_CONNECTION_LOST: st="conn lost"; break;
        case WL_DISCONNECTED: st="disconnected"; break;
        case WL_IDLE_STATUS: st="idle/timeout"; break;
    }
    snprintf(line, sizeof(line), "status:%d %s", g_wifiStatus, st);
    M5.Display.drawString(line, 6, y); y += 22;
    // Disconnect reason is the most telling: 15=wrong pw (4way timeout),
    // 201=no AP, 2/23=auth/PMF, 205=conn fail.
    const char* rs = "";
    switch (g_discReason) {
        case 15: rs="(wrong pw)"; break;
        case 201: rs="(no ap)"; break;
        case 2: case 23: rs="(auth)"; break;
        case 205: rs="(conn fail)"; break;
    }
    snprintf(line, sizeof(line), "reason:%d %s pw:%d", g_discReason, rs, g_pwLen);
    M5.Display.drawString(line, 6, y); y += 22;
    // Scan for the SSID to distinguish 5GHz/typo/range from wrong password.
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("scanning...", 6, y);
    int n = WiFi.scanNetworks();
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (ssid && WiFi.SSID(i) == ssid) { found = i; break; }
    }
    M5.Display.setTextColor(found >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    if (found >= 0) snprintf(line, sizeof(line), "seen: ch%d rssi%d", WiFi.channel(found), (int)WiFi.RSSI(found));
    else            snprintf(line, sizeof(line), "NOT in %d-AP scan", n);
    M5.Display.drawString(line, 6, y);
    WiFi.scanDelete();
    App::footer("back: exit");
}

static const char* authStr(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        default:                        return "?";
    }
}

// Clean, standalone WiFi scan to gauge RF sensitivity: lists AP count and the
// first several APs (name/ch/rssi/auth), flagging whether the SSID is seen.
static void wifiScanDiag() {
    const char* want = Config::wifi().otaSSID;
    App::clear(); App::header("WIFI SCAN");
    App::centerMsg("scanning...", TFT_CYAN);
    WiFi.mode(WIFI_OFF); delay(150);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    delay(250);
    int n = WiFi.scanNetworks(false, true);     // sync, include hidden

    App::clear(); App::header("WIFI SCAN");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    char line[64];
    int y = 30; bool found = false;
    snprintf(line, sizeof(line), "%d APs found", n < 0 ? 0 : n);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK); M5.Display.drawString(line, 6, y); y += 14;
    const char* tgtAuth = "";
    for (int i = 0; i < n && i < 9; i++) {
        bool me = want && WiFi.SSID(i) == want;
        if (me) { found = true; tgtAuth = authStr(WiFi.encryptionType(i)); }
        snprintf(line, sizeof(line), "%-13.13s c%-2d %d %s",
                 WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "(hidden)",
                 WiFi.channel(i), (int)WiFi.RSSI(i), authStr(WiFi.encryptionType(i)));
        M5.Display.setTextColor(me ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
        M5.Display.drawString(line, 6, y); y += 14;
    }
    M5.Display.setTextColor(found ? TFT_GREEN : TFT_RED, TFT_BLACK);
    snprintf(line, sizeof(line), found ? "target SEEN auth=%s" : "target NOT seen", tgtAuth);
    M5.Display.drawString(line, 6, PORK_DISPLAY_H - 16);
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    App::footer("");
    while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
    dirty = true;
}

static void doSync() {
    App::clear();
    App::header("SYNC");
    if (!WPASec::hasApiKey()) {
        App::centerMsg("NO WPA-SEC KEY", TFT_RED);
        App::footer("set key in OPTIONS   back: exit");
        delay(1500);
        dirty = true;
        return;
    }
    App::centerMsg("connecting wifi...", TFT_CYAN);
    if (!connectWiFi()) {
        showWifiDiag();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        // Hold the diagnostic until the user presses back.
        while (true) {
            M5Cardputer.update();
            if (porkhal::vkey.back || porkhal::vkey.enter) break;
            delay(20);
        }
        dirty = true;
        return;
    }

    WPASecSyncResult r = WPASec::syncCaptures(syncProgress);

    // Keep WiFi connected — do NOT turn it off. The connection is held from boot
    // (claimed before the display) and reused; turning it off here would drop it
    // and a fresh reconnect after the display is up fails to associate.

    App::clear();
    App::header("SYNC DONE");
    char line[40];
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 36;
    snprintf(line, sizeof(line), "up %u  fail %u", r.uploaded, r.failed);
    M5.Display.drawString(line, 8, y); y += 24;
    snprintf(line, sizeof(line), "skip %u", r.skipped);
    M5.Display.drawString(line, 8, y); y += 24;
    snprintf(line, sizeof(line), "cracked %u (+%u)", r.cracked, r.newCracked);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(line, 8, y);
    if (!r.success && r.error[0]) {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString(r.error, 8, PORK_DISPLAY_H - 30);
    }
    App::footer("back: return to list");
    // Wait for back press to leave the result screen.
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back || porkhal::vkey.enter) break;
        delay(20);
    }
    enter();   // rescan (upload-marking may change counts)
}

// Detail view for a capture: SSID/BSSID, sync+crack status, and the recovered
// password if wpa-sec has cracked it. Click deletes, back returns.
static void showCaptureDetail(int fi) {
    char bssid[13]; bool haveBssid = SDLayout::captureBssid(files[fi], bssid);
    App::clear(); App::header("CAPTURE");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[64]; int y = 34;
    snprintf(line, sizeof(line), "%.42s", files[fi]);
    M5.Display.drawString(line, 6, y); y += 14;
    if (haveBssid) {
        snprintf(line, sizeof(line), "bssid %c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                 bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],
                 bssid[6],bssid[7],bssid[8],bssid[9],bssid[10],bssid[11]);
        M5.Display.drawString(line, 6, y); y += 16;
    }
    const char* status = fileStatus[fi] == 2 ? "CRACKED"
                       : fileStatus[fi] == 1 ? "UPLOADED" : "LOCAL (not synced)";
    uint16_t stc = fileStatus[fi] == 2 ? TFT_GREEN
                 : fileStatus[fi] == 1 ? TFT_CYAN : TFT_DARKGREY;
    M5.Display.setTextColor(stc, TFT_BLACK);
    snprintf(line, sizeof(line), "status: %s", status);
    M5.Display.drawString(line, 6, y); y += 18;
    if (fileStatus[fi] == 2 && haveBssid) {
        const char* pw = WPASec::getPassword(bssid);
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        snprintf(line, sizeof(line), "PW: %s", (pw && pw[0]) ? pw : "(?)");
        M5.Display.drawString(line, 6, y);
    }
    App::footer("click = DELETE   back = return");
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back)  { dirty = true; return; }
        if (porkhal::vkey.enter) break;
        delay(20);
    }
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), files[fi]);
    Storage::fs().remove(path);
    sel = 0;
    enter();
}

void tick(const App::Input& in) {
    if (in.back) { App::go(App::Screen::MENU); return; }
    int n = rowCount();
    if (in.up)   { sel = (sel + n - 1) % n; dirty = true; }
    if (in.down) { sel = (sel + 1) % n;     dirty = true; }
    if (sel < firstVisible) { firstVisible = sel; }
    if (sel >= firstVisible + VISIBLE) { firstVisible = sel - VISIBLE + 1; }

    if (in.enter && sel == 0) { doSync(); return; }
    if (in.enter && sel == 1) {
        char path[96];
        bool ok = TestCap::generate(path, sizeof(path));
        App::clear(); App::header("TEST CAP");
        App::centerMsg(ok ? "created" : "FAILED", ok ? TFT_GREEN : TFT_RED);
        delay(1200);
        enter();   // rescan to show the new file
        return;
    }
    if (in.enter && sel == 2) { wifiScanDiag(); return; }

    // Click a file row -> detail view (info + cracked password + delete).
    if (in.enter && sel >= ACTIONS) {
        int fi = sel - ACTIONS;
        if (fi >= 0 && fi < fileCount) showCaptureDetail(fi);
        return;
    }

    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenManage
