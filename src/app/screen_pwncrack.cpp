// screen_pwncrack.cpp — PwnCrack.org manage screen (mirrors the OHC one):
// per-capture .22000 upload + a potfile sync to pull cracked results.
#include "app.h"
#include "../web/pwncrack.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include "../core/net_link.h"
#include <SD.h>
#include <WiFi.h>

namespace ScreenPwnCrack {

static constexpr int MAX_FILES = 80;
static constexpr int NAME_LEN  = 64;
static char files[MAX_FILES][NAME_LEN];
static uint8_t fileStatus[MAX_FILES];   // 0=local, 1=uploaded, 2=cracked
static int  fileCount = 0;
static constexpr int ACTIONS = 1;       // row 0 = SYNC POTFILE
static int  sel = 0, firstVisible = 0;
static constexpr int VISIBLE = 8;
static bool dirty = true;
static const char* rowPtrs[MAX_FILES + ACTIONS];
static char        rowBuf[MAX_FILES + ACTIONS][NAME_LEN + 4];

static void scan() {
    fileCount = 0;
    if (!Config::isSDAvailable()) return;
    PwnCrack::loadUploaded();
    PwnCrack::loadCache();     // cracked status from PwnCrack's own potfile
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (!d || !d.isDirectory()) { if (d) d.close(); return; }
    File f = d.openNextFile();
    while (f && fileCount < MAX_FILES) {
        if (!f.isDirectory()) {
            const char* n = f.name(); const char* s = strrchr(n, '/'); if (s) n = s + 1;
            size_t len = strlen(n);
            if (len > 6 && !strcmp(n + len - 6, ".22000")) {
                strncpy(files[fileCount], n, NAME_LEN - 1); files[fileCount][NAME_LEN - 1] = '\0';
                char bssid[13]; uint8_t st = 0;
                if (SDLayout::captureBssid(n, bssid)) {
                    if (PwnCrack::isCracked(bssid))   st = 2;
                    else if (PwnCrack::isUploaded(bssid)) st = 1;
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
    snprintf(rowBuf[0], sizeof(rowBuf[0]), ">> SYNC POTFILE (get cracked)");
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
    char title[28]; snprintf(title, sizeof(title), "PWN %d U%d C%d", fileCount, up, crk);
    App::header(title);
    App::drawList(rowPtrs, rowCount(), sel, firstVisible, VISIBLE, 1);
    uint64_t freeB = Storage::totalBytes() > Storage::usedBytes()
                   ? Storage::totalBytes() - Storage::usedBytes() : 0;
    char foot[48];
    snprintf(foot, sizeof(foot), "click=open  CRK/UP/-   %s free", App::fmtBytes(freeB));
    App::footer(foot);
}

static void waitBackKey() {
    while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
}

static void doSyncPotfile() {
    App::clear(); App::header("PWN SYNC");
    if (!PwnCrack::hasApiKey())        { App::centerMsg("NO PWN KEY", TFT_RED); App::footer("set key in OPTIONS"); delay(1500); dirty = true; return; }
    App::centerMsg("connecting wifi...", TFT_CYAN);
    if (!NetLink::connectConfigured()) { App::centerMsg("NO WIFI", TFT_RED); App::footer("reboot to reconnect"); delay(1500); dirty = true; return; }
    App::clear(); App::header("PWN SYNC"); App::centerMsg("fetching potfile...", TFT_CYAN);
    char err[48] = {0};
    int n = PwnCrack::syncPotfile(err, sizeof(err));
    App::clear(); App::header("PWN SYNC");
    if (n >= 0) {
        char l[32]; snprintf(l, sizeof(l), "cracked: %d", n);
        App::centerMsg(l, TFT_GREEN);
    } else {
        App::centerMsg(err[0] ? err : "FAILED", TFT_RED);
    }
    App::footer("back: return");
    waitBackKey();
    enter();
}

static void doUploadOne(int fi) {
    App::clear(); App::header("PWN UPLOAD");
    if (!PwnCrack::hasApiKey())        { App::centerMsg("NO PWN KEY", TFT_RED); App::footer("set key in OPTIONS"); delay(1500); dirty = true; return; }
    App::centerMsg("connecting wifi...", TFT_CYAN);
    if (!NetLink::connectConfigured()) { App::centerMsg("NO WIFI", TFT_RED); App::footer("reboot to reconnect"); delay(1500); dirty = true; return; }
    App::clear(); App::header("PWN UPLOAD"); App::centerMsg("uploading...", TFT_CYAN);

    PwnCrack::UploadResult r = PwnCrack::uploadFile(files[fi]);
    if (r.success) {
        char bssid[13];
        if (SDLayout::captureBssid(files[fi], bssid)) PwnCrack::markUploaded(bssid);
    }

    App::clear(); App::header("PWN UPLOAD");
    M5.Display.setTextSize(2); M5.Display.setTextDatum(top_left); M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[40]; int y = 40;
    snprintf(line, sizeof(line), "hashes %u", r.hashes); M5.Display.drawString(line, 8, y); y += 26;
    M5.Display.setTextColor(r.success ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.drawString(r.success ? "OK" : "FAILED", 8, y);
    if (!r.success && r.error[0]) {
        M5.Display.setTextSize(1); M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString(r.error, 8, PORK_DISPLAY_H - 30);
    }
    App::footer("back: return");
    waitBackKey();
    enter();
}

static void drawDetail(int fi, int action) {
    char bssid[13]; bool haveBssid = SDLayout::captureBssid(files[fi], bssid);
    App::clear(); App::header("CAPTURE");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[64]; int y = 30;
    snprintf(line, sizeof(line), "%.42s", files[fi]);
    M5.Display.drawString(line, 6, y); y += 15;
    if (haveBssid) {
        snprintf(line, sizeof(line), "bssid %c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                 bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],
                 bssid[6],bssid[7],bssid[8],bssid[9],bssid[10],bssid[11]);
        M5.Display.drawString(line, 6, y); y += 16;
    }
    const char* status = fileStatus[fi] == 2 ? "CRACKED"
                       : fileStatus[fi] == 1 ? "SUBMITTED (PwnCrack)" : "LOCAL (not synced)";
    uint16_t stc = fileStatus[fi] == 2 ? TFT_GREEN : fileStatus[fi] == 1 ? TFT_CYAN : TFT_DARKGREY;
    M5.Display.setTextColor(stc, TFT_BLACK);
    snprintf(line, sizeof(line), "status: %s", status);
    M5.Display.drawString(line, 6, y); y += 16;
    if (fileStatus[fi] == 2 && haveBssid) {
        const char* pw = PwnCrack::getPassword(bssid);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        snprintf(line, sizeof(line), "PW: %s", (pw && pw[0]) ? pw : "(?)");
        M5.Display.drawString(line, 6, y); y += 16;
    }
    y += 4;
    const char* acts[2] = { "UPLOAD TO PWNCRACK", "DELETE FILE" };
    for (int a = 0; a < 2; a++) {
        bool seld = (a == action);
        M5.Display.setTextColor(seld ? TFT_BLACK : TFT_WHITE, seld ? TFT_CYAN : TFT_BLACK);
        snprintf(line, sizeof(line), " %s %s ", seld ? ">" : " ", acts[a]);
        M5.Display.drawString(line, 6, y); y += 14;
    }
    App::footer("turn: pick   click: do   back: return");
}

static void showCaptureDetail(int fi) {
    int action = 0; bool redraw = true;
    while (true) {
        if (redraw) { drawDetail(fi, action); redraw = false; }
        M5Cardputer.update();
        if (porkhal::vkey.back)  { dirty = true; return; }
        if (porkhal::vkey.up || porkhal::vkey.down) { action ^= 1; redraw = true; }
        if (porkhal::vkey.enter) {
            if (action == 0) { doUploadOne(fi); return; }
            char path[96]; snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), files[fi]);
            Storage::fs().remove(path); sel = 0; enter(); return;
        }
        delay(20);
    }
}

void tick(const App::Input& in) {
    if (in.back) { App::go(App::Screen::MENU); return; }
    int n = rowCount();
    if (in.up)   { sel = (sel + n - 1) % n; dirty = true; }
    if (in.down) { sel = (sel + 1) % n;     dirty = true; }
    if (sel < firstVisible) firstVisible = sel;
    if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;
    if (in.enter && sel == 0) { doSyncPotfile(); return; }
    if (in.enter && sel >= ACTIONS) { int fi = sel - ACTIONS; if (fi >= 0 && fi < fileCount) showCaptureDetail(fi); return; }
    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenPwnCrack
