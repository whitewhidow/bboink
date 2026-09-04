#include "bridge.h"
#include "../core/storage.h"
#include "../core/sd_layout.h"
#include "../core/config.h"
#include "../modes/oink.h"
#include "../version.h"
#include "../core/boot_sync.h"   // requestFwSwitch() — switch to the sibling firmware
#include "../hal/m5compat.h"
#include "../hal/board.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <vector>

namespace BleBridge {

// GATT UUIDs (see docs/DESIGN-ble-bridge.md)
static const char* SVC_UUID = "b0070000-b0b0-4b0a-9c5e-000000000000";
static const char* RX_UUID  = "b0070001-b0b0-4b0a-9c5e-000000000000";  // phone -> board (write)
static const char* TX_UUID  = "b0070002-b0b0-4b0a-9c5e-000000000000";  // board -> phone (notify)

// TX frame types (1-byte prefix)
static const uint8_t T_TEXT = 0x01;   // UTF-8 JSON
static const uint8_t T_BIN  = 0x02;   // raw file bytes

static NimBLEServer*         s_server = nullptr;
static NimBLECharacteristic* s_tx = nullptr;
static bool     s_running = false;
static bool     s_connected = false;
static uint16_t s_mtu = 23;
static uint16_t s_filesSent = 0;
static uint16_t s_crackedIn = 0;
static bool     s_exit = false;

// File-streaming state (pumped from loop())
static bool   s_streaming = false;
static File   s_file;
static size_t s_size = 0, s_sent = 0;
static bool   s_crkStarted = false;   // truncate the cracked cache on the first {"c":"crk"}
static String s_mem;                  // in-memory stream source (caps/crks/cfg JSON)
static bool   s_memMode = false;
static uint8_t  s_cmdBuf[320];
static size_t   s_cmdLen = 0;
static volatile bool s_cmdPending = false;
static String   s_beginFrame;         // the 'begin' JSON, sent reliably from loop()
static int      s_phase = 0;          // 0=send begin, 1=data, 2=send end

static uint16_t chunkSize() { return s_mtu > 23 ? (uint16_t)(s_mtu - 4) : 20; }

static bool notifyText(const String& json) {
    if (!s_tx || !s_connected) return false;
    std::vector<uint8_t> buf;
    buf.reserve(json.length() + 1);
    buf.push_back(T_TEXT);
    for (size_t i = 0; i < json.length(); i++) buf.push_back((uint8_t)json[i]);
    s_tx->setValue(buf.data(), buf.size());
    return s_tx->notify();   // false = tx buffer full (caller retries)
}

static const char* syncedPath() {
    static char p[80]; snprintf(p, sizeof(p), "%s/bridge_synced.txt", SDLayout::miscDir()); return p;
}
// bridge_synced.txt: one line per file  "name\tflags"  — flags is a subset of "wop"
// (the wpa-sec/OHC/PwnCrack services that already ACCEPTED that file). A legacy line
// with no tab counts as fully synced. A file is "done" once all its applicable services
// are present: .pcap needs "w"; .22000 needs "o" and "p". This is what makes a failed
// service (e.g. PwnCrack 522) retry ONLY that service instead of re-hitting the others.
static String applicableSvc(const char* name) {
    size_t L = strlen(name);
    if (L > 5 && strcmp(name + L - 5, ".pcap") == 0) return "w";
    return "op";
}
static String syncedFlags(const char* name) {
    File f = Storage::fs().open(syncedPath(), FILE_READ);
    if (!f) return "";
    char line[128]; String res = "";
    while (f.available()) {
        size_t L = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1); line[L] = 0;
        if (L && line[L - 1] == '\r') line[--L] = 0;
        char* tab = strchr(line, '\t');
        const char* nm = line; const char* fl = "wop";
        if (tab) { *tab = 0; fl = tab + 1; }
        if (!strcmp(nm, name)) { res = fl; break; }
    }
    f.close(); return res;
}
static bool isSynced(const char* name) {
    String have = syncedFlags(name), need = applicableSvc(name);
    for (size_t i = 0; i < need.length(); i++) if (have.indexOf(need[i]) < 0) return false;
    return true;   // all applicable services accepted
}
// Merge svc flags into name's record (rewrites the file).
static void markSynced(const char* name, const char* svc) {
    if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
    String out; bool updated = false;
    File f = Storage::fs().open(syncedPath(), FILE_READ);
    if (f) {
        char line[128];
        while (f.available()) {
            size_t L = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1); line[L] = 0;
            if (L && line[L - 1] == '\r') line[--L] = 0;
            if (!L) continue;
            char* tab = strchr(line, '\t');
            String nm = tab ? String(line).substring(0, tab - line) : String(line);
            String fl = tab ? String(tab + 1) : String("wop");
            if (nm == name) { for (const char* p = svc; *p; p++) if (fl.indexOf(*p) < 0) fl += *p; updated = true; }
            out += nm; out += '\t'; out += fl; out += '\n';
        }
        f.close();
    }
    if (!updated) { out += name; out += '\t'; out += svc; out += '\n'; }
    File w = Storage::fs().open(syncedPath(), FILE_WRITE);
    if (w) { w.print(out); w.close(); }
}
// Drop synced entries whose filename carries this BSSID (re-captured -> syncs again).
static void clearSyncedForBssid(const char* bhex) {
    File f = Storage::fs().open(syncedPath(), FILE_READ);
    if (!f) return;
    String keep; char line[128];
    while (f.available()) {
        size_t L = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1); line[L] = 0;
        if (L && line[L - 1] == '\r') line[--L] = 0;
        if (!L) continue;
        char* tab = strchr(line, '\t'); char nm[110];
        size_t nl = tab ? (size_t)(tab - line) : strlen(line);
        if (nl >= sizeof(nm)) nl = sizeof(nm) - 1;
        memcpy(nm, line, nl); nm[nl] = 0;
        char fb[13];
        if (!(SDLayout::captureBssid(nm, fb) && strcasecmp(fb, bhex) == 0)) { keep += line; keep += '\n'; }
    }
    f.close();
    File w = Storage::fs().open(syncedPath(), FILE_WRITE);
    if (w) { w.print(keep); w.close(); }
}

// Extract up to 12 lowercase hex chars (drops ':' and case), for robust BSSID
// matching across the caps (UPPER, no sep) / relay (lower) formats.
static void hexOnly12(char out[13], const char* in) {
    int n = 0;
    for (const char* p = in; *p && n < 12; ++p) {
        char c = *p;
        if ((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) out[n++] = (c<='9') ? c : (char)(c | 0x20);
    }
    out[n] = 0;
}

// Drop the cracked-cache line(s) for a BSSID so deleting a cracked network
// actually removes it (the row is rebuilt from this file). Lines are
// "bssid:ssid:pass"; match on the normalized BSSID.
static void removeCrackedForBssid(const char* bhex) {
    char want[13]; hexOnly12(want, bhex);
    if (!want[0]) return;
    File f = Storage::fs().open(SDLayout::wpasecResultsPath(), FILE_READ);
    if (!f) return;
    String keep; char line[220];
    while (f.available()) {
        size_t L = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1); line[L] = 0;
        if (L && line[L - 1] == '\r') line[--L] = 0;
        if (!L) continue;
        char* c1 = strchr(line, ':');
        char tmp[64]; size_t nl = c1 ? (size_t)(c1 - line) : strlen(line);
        if (nl >= sizeof(tmp)) nl = sizeof(tmp) - 1;
        memcpy(tmp, line, nl); tmp[nl] = 0;
        char bs[13]; hexOnly12(bs, tmp);
        if (strcmp(bs, want) != 0) { keep += line; keep += '\n'; }
    }
    f.close();
    File w = Storage::fs().open(SDLayout::wpasecResultsPath(), FILE_WRITE);
    if (w) { w.print(keep); w.close(); }
}

static void startMem(const char* name, const String& json);   // fwd decl (defined below)

// Build + send the capture manifest ({"t":"list","files":[{name,size,kind}]}).
static void sendList(bool all) {
    s_filesSent = 0;   // new sync sequence -> fresh file count
    JsonDocument doc;
    doc["t"] = "list";
    JsonArray arr = doc["files"].to<JsonArray>();
    File d = Storage::fs().open(SDLayout::handshakesDir());
    if (d && d.isDirectory()) {
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            if (!f.isDirectory()) {
                const char* n = f.name(); const char* sl = strrchr(n, '/'); if (sl) n = sl + 1;
                size_t L = strlen(n);
                const char* kind = nullptr;
                if (L > 6 && strcmp(n + L - 6, ".22000") == 0) kind = "22000";
                else if (L > 5 && strcmp(n + L - 5, ".pcap") == 0) kind = "pcap";
                if (kind && (all || !isSynced(n))) {
                    JsonObject o = arr.add<JsonObject>();
                    o["name"] = n; o["size"] = (uint32_t)f.size(); o["kind"] = kind;
                    o["done"] = syncedFlags(n); o["pmkid"] = (strstr(n, "pmkid") != nullptr);
                }
            }
            f.close();
        }
        d.close();
    }
    String out; serializeJson(doc, out);
    startMem("list", out);   // stream it — a big list overflows one BLE notification
}

static void startFile(const char* name) {
    char path[176]; snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), name);
    s_file = Storage::fs().open(path, FILE_READ);
    if (!s_file) { notifyText(String("{\"t\":\"err\",\"e\":\"open\"}")); return; }
    s_memMode = false; s_size = s_file.size(); s_sent = 0;
    const char* sl = strrchr(name, '.'); const char* kind = (sl && !strcmp(sl, ".pcap")) ? "pcap" : "22000";
    JsonDocument doc; doc["t"] = "begin"; doc["name"] = name; doc["size"] = (uint32_t)s_size; doc["kind"] = kind;
    s_beginFrame = ""; serializeJson(doc, s_beginFrame);
    s_phase = 0; s_streaming = true;
}

// Write one cracked entry (bssid:ssid:password) to the wpa-sec cracked cache. The
// relay returns the FULL cracked list every sync, so the first entry of each new
// sequence TRUNCATES the cache (refresh, not append) and resets the session counter;
// {"c":"crkdone"} then re-arms the truncate for the next sync.
static void writeCracked(const char* b, const char* ssid, const char* pass) {
    if (!pass || !pass[0]) return;
    if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
    if (!s_crkStarted) s_crackedIn = 0;   // new cracked sequence -> fresh count
    File f = Storage::fs().open(SDLayout::wpasecResultsPath(), s_crkStarted ? FILE_APPEND : FILE_WRITE);
    s_crkStarted = true;
    if (f) { f.printf("%s:%s:%s\n", b ? b : "", ssid ? ssid : "", pass); f.close(); s_crackedIn++; }
}

static const char* authStr(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        default:                        return "?";
    }
}

static String jsonQuote(const char* v) {
    String o = "\"";
    for (; v && *v; v++) {
        char c = *v;
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((uint8_t)c >= 0x20) o += c;
    }
    o += "\"";
    return o;
}

static String buildCapsJson() {
    OinkMode::loadBoarBros();
    const OinkMode::BoarBro* list = OinkMode::getExcludedList();
    int n = OinkMode::getExcludedCount();
    String o = "[";
    char hb[13];
    for (int i = 0; i < n; i++) {
        snprintf(hb, 13, "%02X%02X%02X%02X%02X%02X",
                 (uint8_t)(list[i].bssid >> 40), (uint8_t)(list[i].bssid >> 32), (uint8_t)(list[i].bssid >> 24),
                 (uint8_t)(list[i].bssid >> 16), (uint8_t)(list[i].bssid >> 8), (uint8_t)list[i].bssid);
        if (i) o += ',';
        o += "{\"bssid\":\""; o += hb; o += "\",\"ssid\":"; o += jsonQuote(list[i].ssid);
        o += ",\"captured\":"; o += (list[i].flags & OinkMode::BB_CAPTURED) ? "true" : "false";
        o += ",\"manual\":";   o += (list[i].flags & OinkMode::BB_MANUAL) ? "true" : "false";
        o += ",\"ts\":"; o += (uint32_t)list[i].ts;
        uint8_t mch, mau; int8_t mrs; bool mpf;
        if (OinkMode::capMeta(list[i].bssid, mch, mrs, mau, mpf)) {
            o += ",\"ch\":";   o += mch;
            o += ",\"rssi\":"; o += (int)mrs;
            o += ",\"auth\":"; o += jsonQuote(authStr((wifi_auth_mode_t)mau));
            o += ",\"pmf\":";  o += mpf ? "true" : "false";
        }
        o += "}";
    }
    o += "]";
    return o;
}

static String buildCrksJson() {
    String o = "["; bool first = true;
    File f = Storage::fs().open(SDLayout::wpasecResultsPath(), FILE_READ);
    if (f) {
        char line[220];
        while (f.available()) {
            size_t L = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1); line[L] = 0;
            if (L && line[L - 1] == '\r') line[--L] = 0;
            if (L < 5) continue;
            char* c1 = strchr(line, ':'); if (!c1) continue; *c1 = 0;
            char* c2 = strchr(c1 + 1, ':'); if (!c2) continue; *c2 = 0;
            if (!first) o += ','; first = false;
            o += "{\"bssid\":\""; o += line; o += "\",\"ssid\":"; o += jsonQuote(c1 + 1);
            o += ",\"pass\":"; o += jsonQuote(c2 + 1); o += "}";
        }
        f.close();
    }
    o += "]";
    return o;
}

static String buildCfgJson() {
    const WiFiConfig& w = Config::wifi();
    String o = "{";
    o += "\"wifi_ssid\":" + jsonQuote(w.otaSSID);
    o += ",\"has_wifi_pass\":"; o += w.otaPassword[0] ? "true" : "false";
    o += ",\"wpa_key\":"     + jsonQuote(w.wpaSecKey);
    o += ",\"ohc_key\":"     + jsonQuote(w.ohcKey);
    o += ",\"pwn_key\":"     + jsonQuote(w.pwncrackKey);
    o += ",\"relay_url\":"   + jsonQuote(w.relayUrl);
    o += ",\"relay_token\":" + jsonQuote(w.relayToken);
    o += ",\"app_url\":"     + jsonQuote(w.appUrl);
    o += ",\"ntfy_topic\":"  + jsonQuote(w.ntfyTopic);
    o += ",\"ap_ssid\":"     + jsonQuote(w.apSSID);
    o += ",\"ch_hop_ms\":"   + String(w.channelHopInterval);
    o += ",\"atk_rssi\":"    + String(w.attackMinRssi);
    o += ",\"max_tries\":"   + String(w.maxAttackAttempts);
    o += ",\"burst\":"       + String(w.deauthBurstCount);
    o += ",\"brightness\":"  + String(w.displayBrightness);
    o += ",\"deauth\":"; o += w.enableDeauth ? "true" : "false";
    o += ",\"pmkid\":";  o += w.pmkidEnabled ? "true" : "false";
    o += ",\"sound\":";  o += w.soundEnabled ? "true" : "false";
    o += ",\"led\":";    o += w.ledEnabled ? "true" : "false";
#if PORK_LED_COUNT > 0
    o += ",\"has_led\":true";
#endif
    // Read-only device status (surfaced in the portal header). ver is always
    // accurate; batt is only reported on boards with a real fuel gauge.
    o += ",\"ver\":" + jsonQuote(BBOINK_VERSION);
#if defined(PORK_BOARD_TEMBED_CC1101)
    o += ",\"board\":" + jsonQuote("T-Embed CC1101");
#elif defined(PORK_BOARD_TDISPLAY_C5)
    o += ",\"board\":" + jsonQuote("T-Display C5");
#elif defined(PORK_BOARD_WAVESHARE_C5_LCD)
    o += ",\"board\":" + jsonQuote("Waveshare C5-LCD");
#endif
#if !defined(PORK_BOARD_TDISPLAY_C5) && !defined(PORK_BOARD_WAVESHARE_C5_LCD)
    o += ",\"batt\":"     + String(M5.Power.getBatteryLevel());
    o += ",\"has_batt\":true";
#endif
    o += "}";
    return o;
}

static void setCfgField(const char* k, const char* v) {
    WiFiConfig& w = Config::wifi();
    auto S = [&](char* dst, size_t n) { strncpy(dst, v, n - 1); dst[n - 1] = 0; };
    bool tb = (v[0] == '1' || v[0] == 't' || v[0] == 'T');
    if      (!strcmp(k, "wifi_ssid"))   S(w.otaSSID, sizeof(w.otaSSID));
    else if (!strcmp(k, "wifi_pass"))   { if (v[0]) S(w.otaPassword, sizeof(w.otaPassword)); }
    else if (!strcmp(k, "wpa_key"))     S(w.wpaSecKey, sizeof(w.wpaSecKey));
    else if (!strcmp(k, "ohc_key"))     S(w.ohcKey, sizeof(w.ohcKey));
    else if (!strcmp(k, "pwn_key"))     S(w.pwncrackKey, sizeof(w.pwncrackKey));
    else if (!strcmp(k, "relay_url"))   S(w.relayUrl, sizeof(w.relayUrl));
    else if (!strcmp(k, "relay_token")) S(w.relayToken, sizeof(w.relayToken));
    else if (!strcmp(k, "app_url"))     S(w.appUrl, sizeof(w.appUrl));
    else if (!strcmp(k, "ntfy_topic"))  S(w.ntfyTopic, sizeof(w.ntfyTopic));
    else if (!strcmp(k, "ap_ssid"))     S(w.apSSID, sizeof(w.apSSID));
    else if (!strcmp(k, "ch_hop_ms"))   w.channelHopInterval = atoi(v);
    else if (!strcmp(k, "atk_rssi"))    w.attackMinRssi = atoi(v);
    else if (!strcmp(k, "max_tries"))   w.maxAttackAttempts = atoi(v);
    else if (!strcmp(k, "burst"))       w.deauthBurstCount = atoi(v);
    else if (!strcmp(k, "brightness"))  w.displayBrightness = atoi(v);
    else if (!strcmp(k, "deauth"))      w.enableDeauth = tb;
    else if (!strcmp(k, "pmkid"))       w.pmkidEnabled = tb;
    else if (!strcmp(k, "sound"))       w.soundEnabled = tb;
    else if (!strcmp(k, "led"))         w.ledEnabled = tb;
}

static void delCapture(const char* bhex) {
    OinkMode::removeBoarBro(strtoull(bhex, nullptr, 16));
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (d && d.isDirectory()) {
        char paths[8][110]; int nd = 0;
        for (File f = d.openNextFile(); f && nd < 8; f = d.openNextFile()) {
            if (!f.isDirectory()) {
                const char* n = f.name(); const char* sl = strrchr(n, '/'); if (sl) n = sl + 1;
                char fb[13];
                if (SDLayout::captureBssid(n, fb) && strcasecmp(fb, bhex) == 0) {
                    snprintf(paths[nd], sizeof(paths[nd]), "%s/%s", dir, n); nd++;
                }
            }
            f.close();
        }
        d.close();
        for (int i = 0; i < nd; i++) Storage::fs().remove(paths[i]);
    }
}

// Stream an in-memory JSON payload to the phone (begin -> BIN chunks -> end).
static void startMem(const char* name, const String& json) {
    if (s_streaming) return;
    s_mem = json; s_memMode = true; s_size = s_mem.length(); s_sent = 0;
    JsonDocument doc; doc["t"] = "begin"; doc["name"] = name; doc["size"] = (uint32_t)s_size; doc["kind"] = "json";
    s_beginFrame = ""; serializeJson(doc, s_beginFrame);
    s_phase = 0; s_streaming = true;
}

static void handleCommand(const uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;
    const char* c = doc["c"] | "";
    if (!strcmp(c, "list"))        sendList(doc["all"] | false);
    else if (!strcmp(c, "get"))    { const char* n = doc["name"] | ""; if (n[0]) startFile(n); }
    else if (!strcmp(c, "crk"))    { writeCracked(doc["b"] | "", doc["s"] | "", doc["p"] | ""); notifyText("{\"t\":\"ok\"}"); }
    else if (!strcmp(c, "crkdone")){ s_crkStarted = false; notifyText(String("{\"t\":\"ok\",\"n\":") + s_crackedIn + "}"); }
    else if (!strcmp(c, "caps"))   startMem("caps", buildCapsJson());
    else if (!strcmp(c, "crks"))   startMem("crks", buildCrksJson());
    else if (!strcmp(c, "getcfg")) startMem("cfg", buildCfgJson());
    else if (!strcmp(c, "del"))    { const char* b = doc["bssid"] | ""; if (b[0]) { delCapture(b); clearSyncedForBssid(b); removeCrackedForBssid(b); notifyText("{\"t\":\"ok\"}"); } }
    else if (!strcmp(c, "synced"))  { const char* n = doc["name"] | ""; const char* sv = doc["svc"] | "wop"; if (n[0]) markSynced(n, sv); notifyText("{\"t\":\"ok\"}"); }
    else if (!strcmp(c, "scfg"))   { const char* k = doc["k"] | ""; const char* v = doc["v"] | ""; if (k[0]) { setCfgField(k, v); notifyText("{\"t\":\"ok\"}"); } }
    else if (!strcmp(c, "savecfg")){ Config::save(); notifyText("{\"t\":\"ok\"}"); }
    else if (!strcmp(c, "excl"))   {   // add a manual never-attack exclusion (by SSID name, optional BSSID)
        const char* ssid = doc["s"] | "";
        const char* bh   = doc["b"] | "";
        uint8_t bssid[6] = {0};
        if (bh[0]) { uint64_t v = strtoull(bh, nullptr, 16); for (int i = 0; i < 6; i++) bssid[i] = (uint8_t)(v >> ((5 - i) * 8)); }
        if (ssid[0] || bh[0]) { OinkMode::excludeNetworkByBSSID(bssid, ssid); notifyText("{\"t\":\"ok\"}"); }
        else notifyText("{\"t\":\"err\",\"e\":\"need ssid\"}");
    }
    else if (!strcmp(c, "updatefw")) {   // update to the latest of THIS firmware (reboot-to-fetch)
        notifyText("{\"t\":\"ok\",\"m\":\"updating\"}");
        delay(300);                      // let the notify flush before we reboot
        requestFwUpdate();               // RTC flag + reboot into a WiFi boot (never returns)
    }
    else if (!strcmp(c, "switchfw")) {   // flash the sibling firmware + boot into it (reboot-to-switch)
#if defined(BBOINK_OTHER_FW_URL)
        notifyText("{\"t\":\"ok\",\"m\":\"switching\"}");
        delay(300);
        requestFwSwitch();               // RTC flag + reboot into a WiFi boot (never returns)
#else
        notifyText("{\"t\":\"err\",\"e\":\"no sibling fw\"}");
#endif
    }
    else if (!strcmp(c, "done"))   { s_exit = true; notifyText("{\"t\":\"bye\"}"); }
}

class RxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        NimBLEAttValue v = c->getValue();
        // Commands are sequential (the phone awaits each response), so one slot is
        // enough. Defer the actual work to loop().
        if (v.length() && v.length() < sizeof(s_cmdBuf) && !s_cmdPending) {
            memcpy(s_cmdBuf, v.data(), v.length());
            s_cmdLen = v.length();
            s_cmdPending = true;
        }
    }
};

class SrvCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override { s_connected = true; }
    void onDisconnect(NimBLEServer* srv, NimBLEConnInfo&, int) override {
        s_connected = false; s_streaming = false; if (s_file) s_file.close();
        NimBLEDevice::startAdvertising();   // allow reconnection
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { s_mtu = mtu; }
};

static RxCB  s_rxcb;
static SrvCB s_srvcb;

void start(const char* advName) {
    if (s_running) return;
    s_filesSent = s_crackedIn = 0; s_connected = s_exit = s_streaming = s_crkStarted = false; s_mtu = 23;

    NimBLEDevice::init(advName);
    NimBLEDevice::setMTU(247);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_srvcb);
    NimBLEService* svc = s_server->createService(SVC_UUID);
    s_tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic* rx = svc->createCharacteristic(
        RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(&s_rxcb);
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    // GOTCHA: a 128-bit service UUID (18 bytes) + the name won't both fit the 31-byte
    // primary adv packet ("Data length exceeded" -> name silently dropped). Keep the UUID
    // in the primary (clients filter by it) and move the NAME into the scan response.
    adv->addServiceUUID(SVC_UUID);
    NimBLEAdvertisementData scanResp;
    scanResp.setName(advName);
    adv->setScanResponseData(scanResp);
    adv->enableScanResponse(true);
    NimBLEDevice::startAdvertising();
    s_running = true;
}

void stop() {
    if (!s_running) return;
    if (s_file) s_file.close();
    s_streaming = false;
    NimBLEDevice::deinit(true);
    s_server = nullptr; s_tx = nullptr; s_running = false; s_connected = false;
}

void loop() {
    if (!s_running || !s_connected) return;
    // Handle a queued command in THIS (main-loop) task — safe stack for FS + JSON.
    if (s_cmdPending && !s_streaming) {
        s_cmdPending = false;
        handleCommand(s_cmdBuf, s_cmdLen);
    }
    if (!s_streaming) return;
    // One frame per call, retried until NimBLE accepts it (notify()==false means its
    // tx buffer is full) — so begin, EVERY data chunk, and end are all delivered in
    // order. Nothing is dropped, so no truncated/merged JSON.
    if (s_phase == 0) {                              // begin
        if (notifyText(s_beginFrame)) s_phase = 1;
        return;
    }
    if (s_phase == 1) {                              // data
        uint16_t cs = chunkSize();
        static uint8_t buf[256];
        if (cs > sizeof(buf) - 1) cs = sizeof(buf) - 1;
        buf[0] = T_BIN;
        size_t toRead = s_size - s_sent; if (toRead > cs) toRead = cs;
        size_t rd = 0;
        if (toRead > 0) {
            if (s_memMode) { memcpy(buf + 1, s_mem.c_str() + s_sent, toRead); rd = toRead; }
            else           { s_file.seek(s_sent); rd = s_file.read(buf + 1, toRead); }
        }
        if (rd > 0) {
            s_tx->setValue(buf, rd + 1);
            if (s_tx->notify()) s_sent += rd;   // advance only if queued; else retry this chunk
            else return;
        }
        if (s_sent >= s_size || rd == 0) s_phase = 2;
        return;
    }
    if (s_phase == 2) {                              // end
        if (notifyText("{\"t\":\"end\"}")) {
            if (s_memMode) { s_memMode = false; s_mem = String(); }
            else           { s_file.close(); s_filesSent++; }
            s_streaming = false;
        }
    }
}

bool     running()       { return s_running; }
bool     connected()     { return s_connected; }
uint16_t filesSent()     { return s_filesSent; }
uint16_t crackedIn()     { return s_crackedIn; }
bool     exitRequested() { return s_exit; }

} // namespace BleBridge
