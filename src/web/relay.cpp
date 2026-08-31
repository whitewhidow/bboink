#include "relay.h"
#include "../core/config.h"
#include "../core/storage.h"
#include "../core/sd_layout.h"
#include "wpasec.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <set>

namespace Relay {

static String base() {
    String u = Config::wifi().relayUrl;
    while (u.endsWith("/")) u.remove(u.length() - 1);   // no trailing slash
    return u;
}

bool configured() {
    return Config::wifi().relayUrl[0] && Config::wifi().relayToken[0];
}

// Collect de-duplicated WPA* lines from every .22000 file into one text body.
static String collectHashes(uint16_t& count) {
    String out; std::set<String> seen;
    File d = Storage::fs().open(SDLayout::handshakesDir());
    if (d && d.isDirectory()) {
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            if (!f.isDirectory()) {
                const char* n = f.name(); size_t L = strlen(n);
                if (L > 6 && strcmp(n + L - 6, ".22000") == 0) {
                    while (f.available()) {
                        String l = f.readStringUntil('\n'); l.trim();
                        if (l.startsWith("WPA*") && l.length() > 20 && seen.insert(l).second) {
                            out += l; out += '\n';
                        }
                    }
                }
            }
            f.close();
        }
        d.close();
    }
    count = seen.size();
    return out;
}

PingResult ping() {
    PingResult r;
    if (!configured())                 { strncpy(r.status, "relay not configured", sizeof(r.status) - 1); return r; }
    if (WiFi.status() != WL_CONNECTED) { strncpy(r.status, "no uplink", sizeof(r.status) - 1); return r; }
    if (ESP.getMaxAllocHeap() < 36000) { strncpy(r.status, "LOW HEAP", sizeof(r.status) - 1); return r; }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(70000);   // free tier can cold-start ~30-50s; the GET wakes it
    uint32_t t0 = millis();
    int code = -1;
    if (https.begin(client, base() + "/healthz")) {
        code = https.GET();
        https.end();
    }
    r.ms = millis() - t0;
    if (code == 200) { r.up = true; snprintf(r.status, sizeof(r.status), "UP %ums", (unsigned)r.ms); }
    else if (code > 0) snprintf(r.status, sizeof(r.status), "DOWN HTTP %d", code);
    else               snprintf(r.status, sizeof(r.status), "unreachable (%d)", code);
    Serial.printf("[RELAY] ping -> %s\n", r.status);
    return r;
}

SyncResult sync() {
    SyncResult r;
    if (!configured())                 { strncpy(r.error, "relay not configured", sizeof(r.error) - 1); return r; }
    if (WiFi.status() != WL_CONNECTED) { strncpy(r.error, "no uplink", sizeof(r.error) - 1); return r; }

    size_t maxblk = ESP.getMaxAllocHeap();
    Serial.printf("[RELAY] sync maxAlloc=%u free=%u\n", (unsigned)maxblk, (unsigned)ESP.getFreeHeap());
    if (maxblk < 36000) { snprintf(r.error, sizeof(r.error), "LOW HEAP %u", (unsigned)maxblk); return r; }

    const String authHdr = String("Bearer ") + Config::wifi().relayToken;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setReuse(true);            // keep the ONE connection open across all requests
    https.setTimeout(60000);         // Render free tier can cold-start ~30-50s

    // 1) POST all hashes -> /v1/hashes  (this is the one and only TLS handshake)
    uint16_t nHashes = 0;
    String hashes = collectHashes(nHashes);
    if (nHashes > 0) {
        if (https.begin(client, base() + "/v1/hashes")) {
            https.addHeader("Authorization", authHdr);
            https.addHeader("Content-Type", "text/plain");
            int code = https.POST(hashes);
            Serial.printf("[RELAY] /v1/hashes -> %d\n", code);
            if (code == 200) r.hashesUp = nHashes;
            else if (code < 0) { snprintf(r.error, sizeof(r.error), "hashes %d", code); https.end(); return r; }
            https.end();
        }
    }
    hashes = String();  // free

    // 2) POST each pcap -> /v1/pcap?name=... (reuses the same connection)
    {
        std::vector<String> pcaps;
        File d = Storage::fs().open(SDLayout::handshakesDir());
        if (d && d.isDirectory()) {
            for (File f = d.openNextFile(); f; f = d.openNextFile()) {
                if (!f.isDirectory()) {
                    const char* n = f.name(); const char* sl = strrchr(n, '/'); if (sl) n = sl + 1;
                    size_t L = strlen(n);
                    if (L > 5 && strcmp(n + L - 5, ".pcap") == 0) pcaps.push_back(n);
                }
                f.close();
            }
            d.close();
        }
        for (auto& nm : pcaps) {
            char path[160]; snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), nm.c_str());
            File f = Storage::fs().open(path, FILE_READ);
            if (!f) continue;
            size_t sz = f.size();
            if (sz == 0 || sz > 200000) { f.close(); continue; }
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (!buf) { f.close(); continue; }
            size_t rd = f.read(buf, sz); f.close();
            if (rd == sz && https.begin(client, base() + "/v1/pcap?name=" + nm)) {
                https.addHeader("Authorization", authHdr);
                https.addHeader("Content-Type", "application/octet-stream");
                int code = https.POST(buf, sz);
                Serial.printf("[RELAY] /v1/pcap %s -> %d\n", nm.c_str(), code);
                if (code == 200) r.pcapsUp++;
                https.end();
            }
            free(buf);
        }
    }

    // 3) GET merged cracked -> write into the wpa-sec cracked cache for the Captures UI
    if (https.begin(client, base() + "/v1/cracked")) {
        https.addHeader("Authorization", authHdr);
        int code = https.GET();
        Serial.printf("[RELAY] /v1/cracked -> %d\n", code);
        if (code == 200) {
            String body = https.getString();
            JsonDocument doc;
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
                if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
                File out = Storage::fs().open(SDLayout::wpasecResultsPath(), FILE_WRITE);
                for (JsonObject e : doc["cracked"].as<JsonArray>()) {
                    const char* bssid = e["bssid"] | "";
                    const char* ssid  = e["ssid"]  | "";
                    const char* pass  = e["password"] | "";
                    if (pass[0] && (bssid[0] || ssid[0])) {
                        if (out) out.printf("%s:%s:%s\n", bssid, ssid, pass);
                        r.cracked++;
                    }
                }
                if (out) out.close();
            }
        }
        https.end();
    }

    r.ok = true;
    return r;
}

} // namespace Relay
