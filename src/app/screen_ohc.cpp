// screen_ohc.cpp — OnlineHashCrack manage screen (mirrors the WPA-SEC one):
// capture list + counters + a single SYNC action that submits .22000 hashes.
#include "app.h"
#include "../web/ohc.h"
#include "../web/wpasec.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include "../core/net_link.h"
#include <SD.h>
#include <WiFi.h>

namespace ScreenOHC {

static constexpr int MAX_FILES = 80;
// Hold the full basename (~45 chars) so delete-by-name matches the real file.
static constexpr int NAME_LEN  = 64;
static char files[MAX_FILES][NAME_LEN];
static uint8_t fileStatus[MAX_FILES];   // 0=local, 1=uploaded(OHC), 2=cracked
static int  fileCount = 0;
static constexpr int ACTIONS = 1;   // row 0 = SYNC
static int  sel = 0, firstVisible = 0;
static constexpr int VISIBLE = 8;   // small font -> more rows fit
static bool dirty = true;
static const char* rowPtrs[MAX_FILES + ACTIONS];
static char        rowBuf[MAX_FILES + ACTIONS][NAME_LEN + 4];

static void scan() {
    fileCount = 0;
    if (!Config::isSDAvailable()) return;
    OHC::loadUploaded();   // OHC submit status (offline)
    WPASec::loadCache();   // cracked status is shared (potfile = password known)
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    File f = d.openNextFile();
    while (f && fileCount < MAX_FILES) {
        if (!f.isDirectory()) {
            const char* n = f.name(); const char* s = strrchr(n, '/'); if (s) n = s + 1;
            size_t len = strlen(n);
            // OHC submits hashcat .22000 hashes only -> list .22000 only.
            bool cap = (len > 6 && !strcmp(n + len - 6, ".22000"));
            if (cap) {
                strncpy(files[fileCount], n, NAME_LEN - 1); files[fileCount][NAME_LEN - 1] = '\0';
                char bssid[13]; uint8_t st = 0;
                if (SDLayout::captureBssid(n, bssid)) {
                    if (WPASec::isCracked(bssid))   st = 2;
                    else if (OHC::isUploaded(bssid)) st = 1;
                }
                fileStatus[fileCount] = st;
                fileCount++;
            }
        }
        f.close(); f = d.openNextFile();
    }
    d.close();
}

static void rebuildRows() {
    snprintf(rowBuf[0], sizeof(rowBuf[0]), ">> SYNC TO ONLINEHASHCRACK");
    rowPtrs[0] = rowBuf[0];
    for (int i = 0; i < fileCount; i++) {
        const char* tag = fileStatus[i] == 2 ? "CRK" : fileStatus[i] == 1 ? "UP " : "-  ";
        snprintf(rowBuf[i + ACTIONS], sizeof(rowBuf[i + ACTIONS]), "%s %.59s", tag, files[i]);
        rowPtrs[i + ACTIONS] = rowBuf[i + ACTIONS];
    }
}
static int rowCount() { return fileCount + ACTIONS; }

void enter() { scan(); rebuildRows(); sel = 0; firstVisible = 0; dirty = true; }

static void draw() {
    App::clear();
    int up = 0, crk = 0;
    for (int i = 0; i < fileCount; i++) { if (fileStatus[i] == 2) crk++; else if (fileStatus[i] == 1) up++; }
    char title[28]; snprintf(title, sizeof(title), "OHC %d U%d C%d", fileCount, up, crk);
    App::header(title);
    App::drawList(rowPtrs, rowCount(), sel, firstVisible, VISIBLE, 1);
    App::footer("click file = delete   CRK/UP/-");
}

static void doSync() {
    App::clear(); App::header("OHC SYNC");
    if (!OHC::hasApiKey())             { App::centerMsg("NO OHC KEY", TFT_RED); App::footer("set key in OPTIONS"); delay(1500); dirty = true; return; }
    App::centerMsg("connecting wifi...", TFT_CYAN);
    if (!NetLink::connectConfigured()) { App::centerMsg("NO WIFI", TFT_RED); App::footer("reboot to reconnect"); delay(1500); dirty = true; return; }
    App::clear(); App::header("OHC SYNC");   // clear the longer "connecting" msg first
    App::centerMsg("uploading...", TFT_CYAN);

    OHC::UploadResult r = OHC::uploadHashes();
    // Record which networks were submitted so the list tags them UP (offline).
    if (r.success) {
        for (int i = 0; i < fileCount; i++) {
            char bssid[13];
            if (SDLayout::captureBssid(files[i], bssid)) OHC::markUploaded(bssid);
        }
    }

    App::clear(); App::header("OHC SYNC DONE");
    M5.Display.setTextSize(2); M5.Display.setTextDatum(top_left); M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[40]; int y = 36;
    snprintf(line, sizeof(line), "hashes %u", r.totalHashes); M5.Display.drawString(line, 8, y); y += 24;
    snprintf(line, sizeof(line), "new %u  skip %u", r.accepted, r.skipped); M5.Display.drawString(line, 8, y); y += 24;
    snprintf(line, sizeof(line), "rejected %u", r.rejected);
    M5.Display.setTextColor(r.rejected ? TFT_RED : TFT_GREEN, TFT_BLACK); M5.Display.drawString(line, 8, y);
    if (!r.success && r.error[0]) {
        M5.Display.setTextSize(1); M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString(r.error, 8, PORK_DISPLAY_H - 30);
    }
    App::footer("back: return");
    while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
    enter();
}

// Detail view for a capture (mirrors the WPA-SEC screen): SSID/BSSID, OHC
// submit + crack status, and the recovered password if it has been cracked.
// Click deletes, back returns.
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
                       : fileStatus[fi] == 1 ? "SUBMITTED (OHC)" : "LOCAL (not synced)";
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
    char path[96]; snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), files[fi]);
    Storage::fs().remove(path); sel = 0; enter();
}

void tick(const App::Input& in) {
    if (in.back) { App::go(App::Screen::MENU); return; }
    int n = rowCount();
    if (in.up)   { sel = (sel + n - 1) % n; dirty = true; }
    if (in.down) { sel = (sel + 1) % n;     dirty = true; }
    if (sel < firstVisible) firstVisible = sel;
    if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;
    if (in.enter && sel == 0) { doSync(); return; }
    if (in.enter && sel >= ACTIONS) { int fi = sel - ACTIONS; if (fi >= 0 && fi < fileCount) showCaptureDetail(fi); return; }
    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenOHC
