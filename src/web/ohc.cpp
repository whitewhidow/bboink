// OnlineHashCrack API v2 client.
#include "ohc.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <vector>

static const char*    OHC_HOST = "api.onlinehashcrack.com";
static const uint16_t OHC_PORT = 443;
static const int      OHC_ALGO_WPA = 22000;   // hashcat mode for WPA-PMKID+EAPOL
static const int      OHC_BATCH    = 50;      // max hashes per request
static const size_t   OHC_MAX_HASH = 120;     // cap collected hashes (memory bound)

namespace OHC {

bool hasApiKey() {
    const char* k = Config::wifi().ohcKey;
    return k && strncmp(k, "sk_", 3) == 0 && strlen(k) > 10;
}

// --- uploaded-to-OHC tracking (offline status for the manage screen) --------

namespace {
struct UpEntry { char bssid[13]; };
std::vector<UpEntry> g_uploaded;
bool g_uploadedLoaded = false;

const char* uploadedPath() {
    static char p[80];
    snprintf(p, sizeof(p), "%s/ohc_uploaded.txt", SDLayout::miscDir());
    return p;
}

// Normalize to 12 uppercase hex chars (drop colons/separators).
void normBssid(const char* in, char out[13]) {
    int o = 0;
    for (int i = 0; in && in[i] && o < 12; i++) {
        char c = in[i];
        if (c >= 'a' && c <= 'f') c -= 32;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) out[o++] = c;
    }
    out[o] = '\0';
}
} // namespace

bool loadUploaded() {
    if (g_uploadedLoaded) return true;
    g_uploaded.clear();
    File f = Storage::fs().open(uploadedPath());
    if (f) {
        while (f.available() && g_uploaded.size() < 500) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            UpEntry e{};
            normBssid(line.c_str(), e.bssid);
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
    for (auto& e : g_uploaded) if (strcmp(e.bssid, key) == 0) return;  // already known
    UpEntry e{}; memcpy(e.bssid, key, sizeof(e.bssid));
    g_uploaded.push_back(e);
    // Append to the persisted list (create dir/file as needed).
    if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
    File f = Storage::fs().open(uploadedPath(), FILE_APPEND);
    if (f) { f.println(key); f.close(); }
}

// POST a JSON body to /v2; return response body text in `resp`.
static bool postV2(const String& body, String& resp, char* err, size_t errLen) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(OHC_HOST, OHC_PORT, 12000)) {
        if (err) snprintf(err, errLen, "TLS CONNECT FAIL");
        return false;
    }
    client.printf("POST /v2 HTTP/1.1\r\n");
    client.printf("Host: %s\r\n", OHC_HOST);
    client.print("Content-Type: application/json\r\n");
    client.printf("Content-Length: %u\r\n", (unsigned)body.length());
    client.print("Connection: close\r\n\r\n");
    client.print(body);

    uint32_t timeout = millis() + 15000;
    while (client.connected() && !client.available() && millis() < timeout) { delay(10); yield(); }
    // skip HTTP headers
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line.length() <= 1) break;        // blank line ends headers
        if (millis() > timeout) break;
    }
    // read body (bounded)
    resp = "";
    while ((client.connected() || client.available()) && millis() < timeout + 12000) {
        if (client.available()) { resp += (char)client.read(); if (resp.length() > 6000) break; }
        else delay(5);
    }
    client.stop();
    // The response uses chunked transfer-encoding, so the raw body has hex chunk
    // markers around the JSON. Extract the JSON object (first { .. last }).
    int b = resp.indexOf('{');
    int e = resp.lastIndexOf('}');
    if (b >= 0 && e > b) resp = resp.substring(b, e + 1);
    return resp.length() > 0;
}

UploadResult uploadHashes() {
    UploadResult r = {};
    if (!hasApiKey())                    { strncpy(r.error, "NO OHC KEY", sizeof(r.error)-1); return r; }
    if (WiFi.status() != WL_CONNECTED)   { strncpy(r.error, "WIFI NOT CONNECTED", sizeof(r.error)-1); return r; }

    // Collect unique WPA hash lines from .22000 capture files.
    std::vector<String> hashes;
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (d && d.isDirectory()) {
        File f = d.openNextFile();
        while (f && hashes.size() < OHC_MAX_HASH) {
            const char* n = f.name();
            size_t len = strlen(n);
            if (!f.isDirectory() && len > 6 && strcmp(n + len - 6, ".22000") == 0) {
                while (f.available() && hashes.size() < OHC_MAX_HASH) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    if (line.startsWith("WPA*") && line.length() > 20) {
                        bool dup = false;
                        for (auto& h : hashes) if (h == line) { dup = true; break; }
                        if (!dup) hashes.push_back(line);
                    }
                }
            }
            f.close();
            f = d.openNextFile();
        }
        d.close();
    }
    r.totalHashes = hashes.size();
    if (hashes.empty()) { strncpy(r.error, "NO .22000 HASHES", sizeof(r.error)-1); return r; }

    const char* key = Config::wifi().ohcKey;
    for (size_t i = 0; i < hashes.size(); i += OHC_BATCH) {
        JsonDocument doc;
        doc["api_key"]     = key;
        doc["agree_terms"] = "yes";
        doc["action"]      = "add_tasks";
        doc["algo_mode"]   = OHC_ALGO_WPA;
        JsonArray arr = doc["hashes"].to<JsonArray>();
        for (size_t j = i; j < i + OHC_BATCH && j < hashes.size(); j++) arr.add(hashes[j]);
        String body; serializeJson(doc, body);
        String resp;
        if (!postV2(body, resp, r.error, sizeof(r.error))) return r;
        JsonDocument rd;
        if (deserializeJson(rd, resp) == DeserializationError::Ok) {
            r.accepted += (uint16_t)(rd["accepted"]["count"] | 0);
            r.skipped  += (uint16_t)(rd["skipped"]["count"]  | 0);
            r.rejected += (uint16_t)(rd["rejected"]["count"] | 0);
            if (rd["success"].is<bool>() && rd["success"] == false) {
                const char* ec = rd["error_code"] | "api error";
                strncpy(r.error, ec, sizeof(r.error)-1);
                return r;
            }
        } else {
            strncpy(r.error, "BAD RESPONSE", sizeof(r.error)-1);
            return r;
        }
        delay(250);
    }
    r.success = true;
    return r;
}

int listTasks(Task* out, int maxTasks, char* err, size_t errLen) {
    if (!hasApiKey())                  { if (err) snprintf(err, errLen, "NO OHC KEY"); return -1; }
    if (WiFi.status() != WL_CONNECTED) { if (err) snprintf(err, errLen, "WIFI NOT CONNECTED"); return -1; }

    JsonDocument doc;
    doc["api_key"]     = Config::wifi().ohcKey;
    doc["agree_terms"] = "yes";
    doc["action"]      = "list_tasks";
    String body; serializeJson(doc, body);
    String resp;
    if (!postV2(body, resp, err, errLen)) return -1;

    JsonDocument rd;
    if (deserializeJson(rd, resp) != DeserializationError::Ok) { if (err) snprintf(err, errLen, "BAD JSON"); return -1; }
    if (rd["success"].is<bool>() && rd["success"] == false) {
        if (err) snprintf(err, errLen, "%s", rd["error_code"] | "api error");
        return -1;
    }
    int n = 0;
    for (JsonObject t : rd["tasks"].as<JsonArray>()) {
        if (n >= maxTasks) break;
        strncpy(out[n].hash,   t["hash"]   | "", sizeof(out[n].hash)-1);   out[n].hash[sizeof(out[n].hash)-1]='\0';
        strncpy(out[n].status, t["status"] | "", sizeof(out[n].status)-1); out[n].status[sizeof(out[n].status)-1]='\0';
        n++;
    }
    return n;
}

} // namespace OHC
