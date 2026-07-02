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
static constexpr int ACTIONS = 0;   // captures only; upload is per-file (in detail)
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
    for (int i = 0; i < fileCount; i++) {
        const char* tag = fileStatus[i] == 2 ? "CRK" : fileStatus[i] == 1 ? "UP " : "-  ";
        snprintf(rowBuf[i], sizeof(rowBuf[i]), "%s %.59s", tag, files[i]);
        rowPtrs[i] = rowBuf[i];
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
    if (fileCount == 0) {
        App::centerMsg("no .22000 captures", TFT_DARKGREY);
        App::footer("back: return");
        return;
    }
    App::drawList(rowPtrs, rowCount(), sel, firstVisible, VISIBLE, 1);
    uint64_t freeB = Storage::totalBytes() > Storage::usedBytes()
                   ? Storage::totalBytes() - Storage::usedBytes() : 0;
    char foot[48];
    snprintf(foot, sizeof(foot), "click=open  CRK/UP/-   %s free", App::fmtBytes(freeB));
    App::footer(foot);
}

// Upload a SINGLE capture's hashes to OHC (individual upload -> no dup floods).
static void doUploadOne(int fi) {
    App::clear(); App::header("OHC UPLOAD");
    if (!OHC::hasApiKey())             { App::centerMsg("NO OHC KEY", TFT_RED); App::footer("set key in OPTIONS"); delay(1500); dirty = true; return; }
    App::centerMsg("connecting wifi...", TFT_CYAN);
    if (!NetLink::connectConfigured()) { App::centerMsg("NO WIFI", TFT_RED); App::footer("reboot to reconnect"); delay(1500); dirty = true; return; }
    App::clear(); App::header("OHC UPLOAD");   // clear the longer "connecting" msg first
    App::centerMsg("uploading...", TFT_CYAN);

    OHC::UploadResult r = OHC::uploadFile(files[fi]);
    if (r.success) {
        char bssid[13];
        if (SDLayout::captureBssid(files[fi], bssid)) OHC::markUploaded(bssid);
    }

    App::clear(); App::header("OHC UPLOAD");
    M5.Display.setTextSize(2); M5.Display.setTextDatum(top_left); M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[40]; int y = 36;
    snprintf(line, sizeof(line), "hashes %u", r.totalHashes); M5.Display.drawString(line, 8, y); y += 24;
    snprintf(line, sizeof(line), "new %u  skip %u", r.accepted, r.skipped); M5.Display.drawString(line, 8, y); y += 24;
    M5.Display.setTextColor(r.success ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.drawString(r.success ? "OK" : "FAILED", 8, y);
    if (!r.success && r.error[0]) {
        M5.Display.setTextSize(1); M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString(r.error, 8, PORK_DISPLAY_H - 30);
    }
    App::footer("back: return");
    while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
    enter();
}

// Draw the capture detail (info + a 2-action selector: upload / delete).
static void drawDetail(int fi, int action) {
    char bssid[13]; bool haveBssid = SDLayout::captureBssid(files[fi], bssid);
    App::clear(); App::header("CAPTURE");
    M5.Display.setTextSize(1); M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[64]; int y = 30;
    snprintf(line, sizeof(line), "%.42s", files[fi]);
    M5.Display.drawString(line, 6, y); y += 14;
    if (haveBssid) {
        snprintf(line, sizeof(line), "bssid %c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                 bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],
                 bssid[6],bssid[7],bssid[8],bssid[9],bssid[10],bssid[11]);
        M5.Display.drawString(line, 6, y); y += 15;
    }
    const char* status = fileStatus[fi] == 2 ? "CRACKED"
                       : fileStatus[fi] == 1 ? "SUBMITTED (OHC)" : "LOCAL (not synced)";
    uint16_t stc = fileStatus[fi] == 2 ? TFT_GREEN
                 : fileStatus[fi] == 1 ? TFT_CYAN : TFT_DARKGREY;
    M5.Display.setTextColor(stc, TFT_BLACK);
    snprintf(line, sizeof(line), "status: %s", status);
    M5.Display.drawString(line, 6, y); y += 15;
    // Handshake quality: does the file actually contain a crackable hash?
    {
        char qpath[128];
        snprintf(qpath, sizeof(qpath), "%s/%s", SDLayout::handshakesDir(), files[fi]);
        size_t fl = strlen(files[fi]);
        const char* q; uint16_t qc;
        if (fl > 6 && strcmp(files[fi] + fl - 6, ".22000") == 0) {
            bool eapol = false, pmkid = false;
            File qf = Storage::fs().open(qpath, FILE_READ);
            if (qf) {
                while (qf.available()) {
                    String ln = qf.readStringUntil('\n');
                    if (ln.startsWith("WPA*02*")) { eapol = true; break; }
                    if (ln.startsWith("WPA*01*")) pmkid = true;
                }
                qf.close();
            }
            q = eapol ? "hash: EAPOL (crackable)" : pmkid ? "hash: PMKID (crackable)" : "hash: none!";
            qc = (eapol || pmkid) ? TFT_GREEN : TFT_RED;
        } else { q = "pcap (checked on upload)"; qc = TFT_CYAN; }
        M5.Display.setTextColor(qc, TFT_BLACK);
        M5.Display.drawString(q, 6, y); y += 15;
    }
    if (fileStatus[fi] == 2 && haveBssid) {
        const char* pw = WPASec::getPassword(bssid);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        snprintf(line, sizeof(line), "PW: %s", (pw && pw[0]) ? pw : "(?)");
        M5.Display.drawString(line, 6, y); y += 15;
    }
    // Action selector
    y += 4;
    const char* acts[2] = { "UPLOAD TO OHC", "DELETE FILE" };
    for (int a = 0; a < 2; a++) {
        bool seld = (a == action);
        M5.Display.setTextColor(seld ? TFT_BLACK : TFT_WHITE, seld ? TFT_CYAN : TFT_BLACK);
        snprintf(line, sizeof(line), " %s %s ", seld ? ">" : " ", acts[a]);
        M5.Display.drawString(line, 6, y); y += 14;
    }
    App::footer("turn: pick   click: do   back: return");
}

// Detail view for a capture: SSID/BSSID, OHC submit + crack status, quality,
// and a per-file action selector (upload this one to OHC, or delete it).
static void showCaptureDetail(int fi) {
    int action = 0;      // 0 = upload, 1 = delete
    bool redraw = true;
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
    if (n > 0) {
        if (in.up)   { sel = (sel + n - 1) % n; dirty = true; }
        if (in.down) { sel = (sel + 1) % n;     dirty = true; }
        if (sel < firstVisible) firstVisible = sel;
        if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;
        if (in.enter && sel >= 0 && sel < fileCount) { showCaptureDetail(sel); return; }
    }
    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenOHC
