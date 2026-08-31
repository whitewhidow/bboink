// PwnCrack.org client (see pwncrack.h).
#include "pwncrack.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <vector>

static const char*    PWN_HOST = "pwncrack.org";
static const uint16_t PWN_PORT = 443;

namespace PwnCrack {

bool hasApiKey() {
    const char* k = Config::wifi().pwncrackKey;
    return k && strlen(k) >= 8;    // UUID-ish
}

// --- BSSID normalization + local caches -------------------------------------

namespace {
struct UpEntry { char bssid[13]; };
struct CrackEntry { char bssid[13]; char ssid[33]; char password[64]; };

std::vector<UpEntry>    g_uploaded;
std::vector<CrackEntry> g_cracked;
bool g_uploadedLoaded = false;
bool g_crackedLoaded  = false;

void normBssid(const char* in, char out[13]) {
    int o = 0;
    for (int i = 0; in && in[i] && o < 12; i++) {
        char c = in[i];
        if (c >= 'a' && c <= 'f') c -= 32;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) out[o++] = c;
    }
    out[o] = '\0';
}

const char* uploadedPath() {
    static char p[80];
    snprintf(p, sizeof(p), "%s/pwncrack_uploaded.txt", SDLayout::miscDir());
    return p;
}
const char* potfilePath() {
    static char p[80];
    snprintf(p, sizeof(p), "%s/pwncrack_potfile.txt", SDLayout::miscDir());
    return p;
}
} // namespace

bool loadUploaded() {
    if (g_uploadedLoaded) return true;
    g_uploaded.clear();
    File f = Storage::fs().open(uploadedPath());
    if (f) {
        while (f.available() && g_uploaded.size() < 500) {
            String line = f.readStringUntil('\n'); line.trim();
            if (!line.length()) continue;
            UpEntry e{}; normBssid(line.c_str(), e.bssid);
            if (e.bssid[0]) g_uploaded.push_back(e);
        }
        f.close();
    }
    g_uploadedLoaded = true;
    return true;
}

bool isUploaded(const char* bssid) {
    loadUploaded();
    char key[13]; normBssid(bssid, key);
    if (!key[0]) return false;
    for (auto& e : g_uploaded) if (strcmp(e.bssid, key) == 0) return true;
    return false;
}

void markUploaded(const char* bssid) {
    loadUploaded();
    char key[13]; normBssid(bssid, key);
    if (!key[0]) return;
    for (auto& e : g_uploaded) if (strcmp(e.bssid, key) == 0) return;
    UpEntry e{}; memcpy(e.bssid, key, sizeof(e.bssid));
    g_uploaded.push_back(e);
    if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
    File f = Storage::fs().open(uploadedPath(), FILE_APPEND);
    if (f) { f.println(key); f.close(); }
}

// --- cracked potfile cache --------------------------------------------------
// Potfile lines are hashcat 22000 cracked form: hash:APMAC:STAMAC:ESSID:password
// so BSSID = field 1, SSID = field 3, password = field 4.

static void parsePotLine(const String& line) {
    int c1 = line.indexOf(':');                       if (c1 < 0) return;
    int c2 = line.indexOf(':', c1 + 1);               if (c2 < 0) return;
    int c3 = line.indexOf(':', c2 + 1);               if (c3 < 0) return;
    int c4 = line.indexOf(':', c3 + 1);               if (c4 < 0) return;
    String apmac = line.substring(c1 + 1, c2);
    String essid = line.substring(c3 + 1, c4);
    String pass  = line.substring(c4 + 1);
    CrackEntry e{};
    normBssid(apmac.c_str(), e.bssid);
    if (!e.bssid[0]) return;
    strncpy(e.ssid, essid.c_str(), sizeof(e.ssid) - 1);
    strncpy(e.password, pass.c_str(), sizeof(e.password) - 1);
    for (auto& x : g_cracked) if (strcmp(x.bssid, e.bssid) == 0) return;  // dedup
    g_cracked.push_back(e);
}

bool loadCache() {
    if (g_crackedLoaded) return true;
    g_cracked.clear();
    File f = Storage::fs().open(potfilePath());
    if (f) {
        while (f.available() && g_cracked.size() < 1000) {
            String line = f.readStringUntil('\n'); line.trim();
            if (line.length()) parsePotLine(line);
        }
        f.close();
    }
    g_crackedLoaded = true;
    return true;
}

bool isCracked(const char* bssid) {
    loadCache();
    char key[13]; normBssid(bssid, key);
    if (!key[0]) return false;
    for (auto& e : g_cracked) if (strcmp(e.bssid, key) == 0) return true;
    return false;
}

const char* getPassword(const char* bssid) {
    loadCache();
    char key[13]; normBssid(bssid, key);
    if (!key[0]) return "";
    for (auto& e : g_cracked) if (strcmp(e.bssid, key) == 0) return e.password;
    return "";
}

uint16_t getCrackedCount() { loadCache(); return g_cracked.size(); }

// --- network ----------------------------------------------------------------

UploadResult uploadFile(const char* basename) {
    UploadResult r = {};
    if (!hasApiKey())                  { strncpy(r.error, "NO PWN KEY", sizeof(r.error) - 1); return r; }
    if (WiFi.status() != WL_CONNECTED) { strncpy(r.error, "WIFI NOT CONNECTED", sizeof(r.error) - 1); return r; }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), basename);
    File f = Storage::fs().open(path, FILE_READ);
    if (!f) { strncpy(r.error, "SD OPEN FAIL", sizeof(r.error) - 1); return r; }
    size_t fileSize = f.size();
    if (fileSize == 0 || fileSize > 200000) { f.close(); strncpy(r.error, "BAD FILE SIZE", sizeof(r.error) - 1); return r; }
    // Count WPA* lines (informational).
    while (f.available()) { String l = f.readStringUntil('\n'); if (l.startsWith("WPA*")) r.hashes++; }
    f.seek(0);

    char boundary[40];
    snprintf(boundary, sizeof(boundary), "----BBoink%08lX", (unsigned long)millis());
    const char* key = Config::wifi().pwncrackKey;

    // PwnCrack only accepts a .hc22000 filename; present our .22000 under that name.
    char upname[88];
    snprintf(upname, sizeof(upname), "%s", basename);
    size_t un = strlen(upname);
    if (un > 6 && strcmp(upname + un - 6, ".22000") == 0) strcpy(upname + un - 6, ".hc22000");
    else strncat(upname, ".hc22000", sizeof(upname) - un - 1);

    char pre[512];
    int pn = snprintf(pre, sizeof(pre),
        "--%s\r\nContent-Disposition: form-data; name=\"key\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"handshake\"; filename=\"%s\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n",
        boundary, key, boundary, upname);
    char epi[48];
    int en = snprintf(epi, sizeof(epi), "\r\n--%s--\r\n", boundary);

    // Assemble the whole multipart body in one heap buffer, then POST via HTTPClient
    // (the raw client.write() path silently sent 0 bytes after the TLS handshake on the C5).
    size_t bodyLen = (size_t)pn + fileSize + (size_t)en;
    uint8_t* body = (uint8_t*)malloc(bodyLen);
    if (!body) { f.close(); snprintf(r.error, sizeof(r.error), "OOM %u", (unsigned)bodyLen); return r; }
    memcpy(body, pre, pn);
    size_t rd = f.read(body + pn, fileSize);
    f.close();
    if (rd != fileSize) { free(body); snprintf(r.error, sizeof(r.error), "READ %u/%u", (unsigned)rd, (unsigned)fileSize); return r; }
    memcpy(body + pn + fileSize, epi, en);

    size_t maxblk = ESP.getMaxAllocHeap();
    Serial.printf("[PWN] upload body=%u maxAlloc=%u free=%u\n",
                  (unsigned)bodyLen, (unsigned)maxblk, (unsigned)ESP.getFreeHeap());
    if (maxblk < 36000) { free(body); snprintf(r.error, sizeof(r.error), "LOW HEAP %u", (unsigned)maxblk); return r; }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(15000);
    if (!https.begin(client, String("https://") + PWN_HOST + "/upload_handshake")) {
        free(body); strncpy(r.error, "BEGIN FAILED", sizeof(r.error) - 1); return r;
    }
    https.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);
    int code = https.POST(body, bodyLen);
    free(body);
    String resp = (code > 0) ? https.getString() : String();
    https.end();
    if (code == 200 || code == 201) r.success = true;
    else if (code > 0) snprintf(r.error, sizeof(r.error), "HTTP %d %.28s", code, resp.c_str());
    else snprintf(r.error, sizeof(r.error), "TLS ERR %d", code);
    return r;
}

int syncPotfile(char* err, size_t errLen) {
    if (!hasApiKey())                  { if (err) snprintf(err, errLen, "NO PWN KEY"); return -1; }
    if (WiFi.status() != WL_CONNECTED) { if (err) snprintf(err, errLen, "WIFI NOT CONNECTED"); return -1; }

    // GET via HTTPClient (raw client GET request write also failed post-handshake on C5).
    size_t maxblk = ESP.getMaxAllocHeap();
    Serial.printf("[PWN] potfile maxAlloc=%u free=%u\n", (unsigned)maxblk, (unsigned)ESP.getFreeHeap());
    if (maxblk < 36000) { if (err) snprintf(err, errLen, "LOW HEAP %u", (unsigned)maxblk); return -1; }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(15000);
    String url = String("https://") + PWN_HOST + "/download_potfile_script?key=" + Config::wifi().pwncrackKey;
    if (!https.begin(client, url)) { if (err) snprintf(err, errLen, "BEGIN FAILED"); return -1; }
    int status = https.GET();
    // 404 = no potfile for this key yet (nothing cracked) — not an error.
    if (status == 404) { https.end(); loadCache(); return (int)g_cracked.size(); }
    if (status != 200) { https.end(); if (err) snprintf(err, errLen, status > 0 ? "HTTP %d" : "TLS ERR %d", status); return -1; }

    // Stream the body straight to SD, (re)building the cache line by line.
    if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
    File out = Storage::fs().open(potfilePath(), FILE_WRITE);
    g_cracked.clear();
    WiFiClient* stream = https.getStreamPtr();
    uint32_t to = millis() + 20000;
    while (https.connected() && millis() < to) {
        if (!stream->available()) { delay(5); if (!https.connected() && !stream->available()) break; continue; }
        String line = stream->readStringUntil('\n'); line.trim();
        if (!line.length()) continue;
        if (out) out.println(line);
        parsePotLine(line);
    }
    if (out) out.close();
    g_crackedLoaded = true;
    https.end();
    return (int)g_cracked.size();
}

} // namespace PwnCrack
