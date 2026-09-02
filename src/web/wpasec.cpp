// WPA-SEC distributed cracking service client
// https://wpa-sec.stanev.org/

#include "wpasec.h"
#include "../core/sd_layout.h"
#include "../core/config.h"
#include "../core/heap_gates.h"
#include "../core/wifi_utils.h"
#include "../core/network_recon.h"
#include "../piglet/mood.h"
#include <SD.h>
#include "../core/storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ctype.h>
#include <esp_heap_caps.h>

// WPA-SEC API
static const char* WPASEC_HOST = "wpa-sec.stanev.org";
static const uint16_t WPASEC_PORT = 443;   // HTTPS (wpa-sec redirects HTTP->HTTPS); synced via reboot-to-sync (clean heap)
static const char* WPASEC_UPLOAD_PATH = "/";
static const char* WPASEC_POTFILE_PATH = "/?api&dl=1";
static const size_t WPASEC_MAX_CACHE_ENTRIES = 500;

// Write ALL bytes over a (TLS) client, tolerating partial writes and transient 0s.
// The one-shot write path (HTTPClient.POST / hand-rolled write) sent 0 bytes right
// after the ESP32-C5 TLS handshake, so the multipart body never reached wpa-sec and
// uploads silently failed. Looping in <=1KB chunks with a bounded retry is what
// actually gets the body across.
static bool wpasecWriteAll(WiFiClientSecure& c, const uint8_t* p, size_t n) {
    size_t sent = 0;
    uint32_t t0 = millis();
    while (sent < n) {
        if (!c.connected()) return false;
        size_t chunk = n - sent;
        if (chunk > 1024) chunk = 1024;
        int w = c.write(p + sent, chunk);
        if (w > 0) { sent += (size_t)w; t0 = millis(); }
        else { if (millis() - t0 > 10000) return false; delay(3); }
    }
    return true;
}

// Static member initialization
bool WPASec::cacheLoaded = false;
char WPASec::lastError[64] = "";
std::vector<WPASec::CrackedEntry> WPASec::crackedCache;
std::vector<WPASec::UploadedEntry> WPASec::uploadedCache;
volatile bool WPASec::busy = false;
bool WPASec::batchMode = false;

bool WPASec::isBusy() {
    return busy;
}

void WPASec::normalizeBSSID_Char(const char* bssid, char* output, size_t outLen) {
    if (!bssid || !output || outLen < 1) return;
    size_t outIdx = 0;
    for (int i = 0; bssid[i] && outIdx < outLen - 1; i++) {
        char c = bssid[i];
        if (c != ':' && c != '-') {
            output[outIdx++] = (char)toupper(c);
        }
    }
    output[outIdx] = '\0';
}

// ============================================================================
// Cache Management (disk only)
// ============================================================================

bool WPASec::loadUploadedList() {
    uploadedCache.clear();
    uploadedCache.reserve(64);  // 64 * 13B = 832B — avoids 6 reallocations
    const char* uploadedPath = SDLayout::wpasecUploadedPath();
    if (!Storage::fs().exists(uploadedPath)) return true;

    File f = Storage::fs().open(uploadedPath, FILE_READ);
    if (!f) {
        strncpy(lastError, "CANNOT OPEN UPLOADED", sizeof(lastError) - 1);
        lastError[sizeof(lastError) - 1] = '\0';
        return false;
    }

    char lineBuf[64];
    while (f.available() && uploadedCache.size() < WPASEC_MAX_CACHE_ENTRIES) {
        size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        lineBuf[len] = '\0';
        // Trim trailing whitespace
        while (len > 0 && (lineBuf[len - 1] == '\r' || lineBuf[len - 1] == ' ')) {
            lineBuf[--len] = '\0';
        }
        if (len == 0) continue;

        UploadedEntry entry;
        normalizeBSSID_Char(lineBuf, entry.bssid, sizeof(entry.bssid));
        if (entry.bssid[0] != '\0') {
            uploadedCache.push_back(entry);
        }
    }

    f.close();
    return true;
}

bool WPASec::loadCache() {
    if (cacheLoaded) return true;

    crackedCache.clear();
    crackedCache.reserve(128);  // 128 * 110B = 14KB — avoids 6 reallocations vs no reserve
    uploadedCache.clear();

    const char* cachePath = SDLayout::wpasecResultsPath();
    if (Storage::fs().exists(cachePath)) {
        File f = Storage::fs().open(cachePath, FILE_READ);
        if (!f) {
            strncpy(lastError, "CANNOT OPEN CACHE", sizeof(lastError) - 1);
            lastError[sizeof(lastError) - 1] = '\0';
            return false;
        }

        // Format: AP_BSSID:CLIENT_BSSID:SSID:password (WPA-SEC potfile format)
        // AP_BSSID is always 12 hex chars, CLIENT_BSSID is always 12 hex chars
        // Cap at 500 entries to prevent memory exhaustion
        char lineBuf[160];
        while (f.available() && crackedCache.size() < WPASEC_MAX_CACHE_ENTRIES) {
            size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
            lineBuf[len] = '\0';
            // Trim trailing whitespace
            while (len > 0 && (lineBuf[len - 1] == '\r' || lineBuf[len - 1] == ' ')) {
                lineBuf[--len] = '\0';
            }
            if (len == 0) continue;

            // WPA-SEC potfile: AP_BSSID:CLIENT_BSSID:SSID:password
            // Both BSSIDs are exactly 12 hex chars (no colons)
            // Password can contain colons, so we must find the THIRD colon
            // Find colons by scanning
            const char* firstColon = nullptr;
            const char* secondColon = nullptr;
            const char* thirdColon = nullptr;
            for (const char* p = lineBuf; *p; p++) {
                if (*p == ':') {
                    if (!firstColon) firstColon = p;
                    else if (!secondColon) secondColon = p;
                    else if (!thirdColon) { thirdColon = p; break; }
                }
            }

            // Validate: AP BSSID at pos 0-11 (colon at 12), client BSSID at pos 13-24 (colon at 25)
            if (firstColon && secondColon && thirdColon &&
                (firstColon - lineBuf) == 12 &&
                (secondColon - lineBuf) == 25 &&
                thirdColon > secondColon) {

                CrackedEntry entry;
                memset(&entry, 0, sizeof(entry));

                // AP BSSID: first 12 chars, normalize
                char rawBssid[13];
                memcpy(rawBssid, lineBuf, 12);
                rawBssid[12] = '\0';
                normalizeBSSID_Char(rawBssid, entry.bssid, sizeof(entry.bssid));

                // SSID: between 2nd and 3rd colon
                size_t ssidLen = (size_t)(thirdColon - secondColon - 1);
                if (ssidLen >= sizeof(entry.ssid)) ssidLen = sizeof(entry.ssid) - 1;
                memcpy(entry.ssid, secondColon + 1, ssidLen);
                entry.ssid[ssidLen] = '\0';

                // Password: everything after 3rd colon
                const char* pwStart = thirdColon + 1;
                size_t pwLen = strlen(pwStart);
                if (pwLen >= sizeof(entry.password)) pwLen = sizeof(entry.password) - 1;
                memcpy(entry.password, pwStart, pwLen);
                entry.password[pwLen] = '\0';

                crackedCache.push_back(entry);
            }
        }

        f.close();
    }

    if (!loadUploadedList()) {
        return false;
    }

    cacheLoaded = true;
    return true;
}

// ============================================================================
// Local Cache Queries
// ============================================================================

const WPASec::CrackedEntry* WPASec::findCracked(const char* normalizedBssid) {
    for (size_t i = 0; i < crackedCache.size(); i++) {
        if (strcmp(crackedCache[i].bssid, normalizedBssid) == 0) {
            return &crackedCache[i];
        }
    }
    return nullptr;
}

bool WPASec::isCracked(const char* bssid) {
    loadCache();
    char key[13];
    normalizeBSSID_Char(bssid, key, sizeof(key));
    return findCracked(key) != nullptr;
}

const char* WPASec::getPassword(const char* bssid) {
    loadCache();
    char key[13];
    normalizeBSSID_Char(bssid, key, sizeof(key));
    const CrackedEntry* entry = findCracked(key);
    return entry ? entry->password : "";
}

const char* WPASec::getSSID(const char* bssid) {
    loadCache();
    char key[13];
    normalizeBSSID_Char(bssid, key, sizeof(key));
    const CrackedEntry* entry = findCracked(key);
    return entry ? entry->ssid : "";
}

uint16_t WPASec::getCrackedCount() {
    loadCache();
    return crackedCache.size();
}

bool WPASec::isUploaded(const char* bssid) {
    loadCache();
    char key[13];
    normalizeBSSID_Char(bssid, key, sizeof(key));
    if (findCracked(key) != nullptr) return true;
    for (size_t i = 0; i < uploadedCache.size(); i++) {
        if (strcmp(uploadedCache[i].bssid, key) == 0) return true;
    }
    return false;
}

const char* WPASec::getLastError() {
    return lastError;
}

void WPASec::freeCacheMemory() {
    size_t crackedCount = crackedCache.size();
    size_t uploadedCount = uploadedCache.size();
    crackedCache.clear();
    crackedCache.shrink_to_fit();
    uploadedCache.clear();
    uploadedCache.shrink_to_fit();
    cacheLoaded = false;
    Serial.printf("[WPASEC] Freed cache: %u cracked, %u uploaded\n",
                  (unsigned int)crackedCount, (unsigned int)uploadedCount);
}

bool WPASec::saveUploadedList() {
    const char* uploadedPath = SDLayout::wpasecUploadedPath();
    File f = Storage::fs().open(uploadedPath, FILE_WRITE);
    if (!f) {
        strncpy(lastError, "CANNOT WRITE UPLOADED", sizeof(lastError) - 1);
        lastError[sizeof(lastError) - 1] = '\0';
        return false;
    }

    for (size_t i = 0; i < uploadedCache.size(); i++) {
        f.println(uploadedCache[i].bssid);
    }

    f.close();
    return true;
}

void WPASec::markAsUploaded(const char* bssid) {
    loadCache();
    char key[13];
    normalizeBSSID_Char(bssid, key, sizeof(key));
    if (key[0] == '\0') return;

    // Check if already present
    for (size_t i = 0; i < uploadedCache.size(); i++) {
        if (strcmp(uploadedCache[i].bssid, key) == 0) return;
    }
    // Cap in-memory cache to avoid unbounded heap growth
    if (uploadedCache.size() >= WPASEC_MAX_CACHE_ENTRIES) return;

    UploadedEntry entry;
    memcpy(entry.bssid, key, sizeof(entry.bssid));
    uploadedCache.push_back(entry);
    if (!batchMode) {
        saveUploadedList();
    }
}

void WPASec::beginBatchUpload() {
    batchMode = true;
}

void WPASec::endBatchUpload() {
    if (batchMode) {
        batchMode = false;
        saveUploadedList();  // Single save at end of batch
        Serial.println("[WPASEC] Batch upload complete, saved uploaded list");
    }
}

// ============================================================================
// Network Operations
// ============================================================================

bool WPASec::hasApiKey() {
    const char* key = Config::wifi().wpaSecKey;
    if (!key || key[0] == '\0') return false;
    // Key should be 32 hex characters
    size_t len = strlen(key);
    if (len != 32) return false;
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit(key[i])) return false;
    }
    return true;
}

bool WPASec::canSync() {
    freeCacheMemory();
    HeapGates::TlsGateStatus tls = HeapGates::checkTlsGates();
    return HeapGates::canTls(tls, lastError, sizeof(lastError));
}
bool WPASec::uploadSingleCapture(const char* filepath, const char* bssid) {
    if (!filepath || !bssid) return false;

    Serial.printf("[WPASEC] Uploading: %s\n", filepath);

    File capFile = Storage::fs().open(filepath, FILE_READ);
    if (!capFile) {
        Serial.printf("[WPASEC] Cannot open file: %s\n", filepath);
        return false;
    }
    size_t fileSize = capFile.size();
    if (fileSize == 0 || fileSize > 100000) {  // Max 100KB
        capFile.close();
        Serial.printf("[WPASEC] Invalid file size: %u\n", (unsigned int)fileSize);
        return false;
    }

    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    // Build the whole multipart body in one heap buffer, then POST it via
    // HTTPClient. HTTPClient drives WiFiClientSecure's TLS write/read loop
    // correctly on the ESP32-C5, where the hand-rolled client.write() path
    // silently failed right after the TLS handshake (body sent 0/N).
    char boundary[32];
    snprintf(boundary, sizeof(boundary), "----WPASec%08lX", millis());

    String pre = String("--") + boundary + "\r\n"
               + "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n"
               + "Content-Type: application/octet-stream\r\n\r\n";
    String post = String("\r\n--") + boundary + "--\r\n";

    size_t bodyLen = pre.length() + fileSize + post.length();
    uint8_t* body = (uint8_t*)malloc(bodyLen);
    if (!body) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "OOM %u", (unsigned int)bodyLen);
        Serial.printf("[WPASEC] malloc(%u) failed\n", (unsigned int)bodyLen);
        return false;
    }
    memcpy(body, pre.c_str(), pre.length());
    size_t rd = capFile.read(body + pre.length(), fileSize);
    capFile.close();
    if (rd != fileSize) {
        free(body);
        snprintf(lastError, sizeof(lastError), "READ %u/%u", (unsigned int)rd, (unsigned int)fileSize);
        return false;
    }
    memcpy(body + pre.length() + fileSize, post.c_str(), post.length());

    size_t maxblk = ESP.getMaxAllocHeap();
    Serial.printf("[WPASEC] upload body=%u maxAlloc=%u free=%u\n",
                  (unsigned)bodyLen, (unsigned)maxblk, (unsigned)ESP.getFreeHeap());
    if (maxblk < 36000) { free(body); snprintf(lastError, sizeof(lastError), "LOW HEAP %u/%u", (unsigned)maxblk, (unsigned)ESP.getFreeHeap()); return false; }
    // FIX #2 (C5 empty-body bug): HTTPClient.POST() sent 0 body bytes right after the
    // TLS handshake on the ESP32-C5, so wpa-sec received an empty upload. POST the
    // request ourselves, writing head+body in <=1KB chunks via wpasecWriteAll().
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(25);
    if (!client.connect(WPASEC_HOST, WPASEC_PORT)) {
        free(body);
        snprintf(lastError, sizeof(lastError), "CONNECT FAIL");
        return false;
    }
    String head = String("POST ") + WPASEC_UPLOAD_PATH + " HTTP/1.1\r\n"
        + "Host: " + WPASEC_HOST + "\r\n"
        + "Cookie: key=" + Config::wifi().wpaSecKey + "\r\n"
        + "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
        + "Content-Length: " + String((unsigned)bodyLen) + "\r\n"
        + "Connection: close\r\n\r\n";
    Serial.printf("[WPASEC] POST / (%u bytes body)\n", (unsigned int)bodyLen);
    bool okSend = wpasecWriteAll(client, (const uint8_t*)head.c_str(), head.length())
               && wpasecWriteAll(client, body, bodyLen);
    free(body);
    if (!okSend) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "BODY SEND FAIL");
        return false;
    }
    client.flush();

    // Read the whole response (headers + body) until wpa-sec closes the connection.
    String raw; raw.reserve(1024);
    uint32_t t0 = millis();
    while ((client.connected() || client.available()) && millis() - t0 < 25000) {
        while (client.available()) { raw += (char)client.read(); t0 = millis(); }
        delay(2);
    }
    client.stop();

    int code = 0;
    int sp = raw.indexOf(' ');
    if (sp >= 0) code = raw.substring(sp + 1, sp + 4).toInt();
    int bs = raw.indexOf("\r\n\r\n");
    String resp = (bs >= 0) ? raw.substring(bs + 4) : raw;
    resp.trim();
    Serial.printf("[WPASEC] HTTP %d resp: %.80s\n", code, resp.c_str());

    // FIX #1: wpa-sec returns HTTP 200 for REJECTS too — a valid upload returns the
    // hcxpcapngtool report, an empty/bad one returns "Not a valid capture file...".
    // Checking only the status code marked rejected uploads as "accepted" (they never
    // landed). Parse the body: accept only when it's not an empty/known-error page.
    String lower = resp; lower.toLowerCase();
    bool httpOk   = (code == 200 || code == 201);
    bool rejected = (resp.length() == 0)
                 || (lower.indexOf("not a valid capture") >= 0)
                 || (lower.indexOf("please provide a valid key") >= 0);
    bool success = httpOk && !rejected;
    if (success) {
        Serial.printf("[WPASEC] Upload accepted: %s\n", bssid);
    } else if (httpOk) {
        // reached wpa-sec but it rejected the file (usually a truncated/empty body)
        snprintf(lastError, sizeof(lastError), "REJECTED: %.40s", resp.c_str());
    } else if (code > 0) {
        snprintf(lastError, sizeof(lastError), "HTTP %d", code);
    } else {
        snprintf(lastError, sizeof(lastError), "TLS ERR %d", code);
    }
    return success;
}

bool WPASec::downloadPotfile(uint16_t& newCracks) {
    newCracks = 0;
    
    Serial.println("[WPASEC] Downloading potfile...");
    
    // GET the potfile via HTTPClient (raw GET request write also failed post-handshake on C5).
    size_t maxblk = ESP.getMaxAllocHeap();
    if (maxblk < 36000) { snprintf(lastError, sizeof(lastError), "LOW HEAP %u/%u", (unsigned)maxblk, (unsigned)ESP.getFreeHeap()); return false; }
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(25000);
    https.setReuse(false);
    String url = String("https://") + WPASEC_HOST + WPASEC_POTFILE_PATH;
    if (!https.begin(client, url)) {
        snprintf(lastError, sizeof(lastError), "POTFILE BEGIN FAILED");
        return false;
    }
    https.addHeader("Cookie", String("key=") + Config::wifi().wpaSecKey);
    https.addHeader("Connection", "close");
    int status = https.GET();
    if (status != 200) {
        https.end();
        snprintf(lastError, sizeof(lastError), status > 0 ? "POTFILE HTTP %d" : "POTFILE TLS %d", status);
        return false;
    }

    // Count entries already cached so we report only genuinely-new cracks this sync.
    const char* cachePath = SDLayout::wpasecResultsPath();
    uint16_t oldCount = 0;
    {
        File oldFile = Storage::fs().open(cachePath, FILE_READ);
        if (oldFile) {
            char tmp[160];
            while (oldFile.available()) {
                size_t l = oldFile.readBytesUntil('\n', tmp, sizeof(tmp) - 1);
                if (l > 10) oldCount++;
            }
            oldFile.close();
        }
    }

    File cacheFile = Storage::fs().open(cachePath, FILE_WRITE);
    if (!cacheFile) {
        https.end();
        strncpy(lastError, "CANNOT WRITE CACHE", sizeof(lastError) - 1);
        return false;
    }

    // Stream potfile line-by-line (BSSID:SSID:password) straight to SD.
    WiFiClient* stream = https.getStreamPtr();
    char lineBuf[160];
    uint16_t lineCount = 0;
    uint32_t to = millis() + 45000;
    while (https.connected() && millis() < to) {
        if (!stream->available()) { delay(10); if (!https.connected() && !stream->available()) break; continue; }
        size_t len = stream->readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        if (len == 0) continue;
        lineBuf[len] = '\0';
        if (lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
        int colonCount = 0;
        for (size_t i = 0; lineBuf[i]; i++) if (lineBuf[i] == ':') colonCount++;
        if (colonCount >= 2 && strlen(lineBuf) > 10) { cacheFile.println(lineBuf); lineCount++; }
        yield();
    }
    cacheFile.close();
    https.end();

    Serial.printf("[WPASEC] Potfile downloaded: %u entries (%u previously)\n",
                  (unsigned int)lineCount, (unsigned int)oldCount);
    newCracks = (lineCount > oldCount) ? (lineCount - oldCount) : 0;
    return true;
}

WPASecSyncResult WPASec::syncCaptures(WPASecProgressCallback cb, bool doDownload) {
    WPASecSyncResult result = {};
    result.success = false;
    result.error[0] = '\0';
    
    busy = true;
    
    // Pause NetworkRecon - TLS operations conflict with promiscuous mode
    // conditionHeapForTLS() overrides promiscuous callbacks, breaking NetworkRecon state
    bool wasReconRunning = NetworkRecon::isRunning();
    if (wasReconRunning) {
        Serial.println("[WPASEC] Pausing NetworkRecon for TLS operations");
        NetworkRecon::pause();
    }
    
    // Pre-flight checks
    if (!hasApiKey()) {
        strncpy(result.error, "NO WPA-SEC KEY", sizeof(result.error) - 1);
        if (wasReconRunning) NetworkRecon::resume();
        busy = false;
        return result;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(result.error, "WIFI NOT CONNECTED", sizeof(result.error) - 1);
        if (wasReconRunning) NetworkRecon::resume();
        busy = false;
        return result;
    }

    if (cb) {
        cb("prepping heap", 0, 0);
    }
    
    // wpa-sec uploads over plain HTTP now — do NOT run any TLS heap conditioning.
    // WiFiUtils::conditionHeapForTLS() reworks the WiFi/promiscuous state and was
    // dropping the STA uplink ("uplink gone"). Just a modest free-heap check.
    if (!canSync()) {
        strncpy(result.error, lastError, sizeof(result.error) - 1);
        if (wasReconRunning) NetworkRecon::resume();
        busy = false;
        return result;
    }

    // Collect files to upload from handshakes directory
    if (cb) {
        cb("scanning caps", 0, 0);
    }
    const char* hsDir = SDLayout::handshakesDir();
    if (!Storage::fs().exists(hsDir)) {
        strncpy(result.error, "NO HANDSHAKES DIR", sizeof(result.error) - 1);
        if (wasReconRunning) NetworkRecon::resume();
        busy = false;
        return result;
    }
    
    // First pass: count files and check which need upload
    // Only load uploaded list (not cracked cache) to avoid 14KB allocation before TLS
    loadUploadedList();
    
    // Collect pending uploads (store paths temporarily)
    struct PendingUpload {
        char path[80];
        char bssid[13];
    };
    static PendingUpload pendingUploads[16];  // Max 16 per sync (reduced from 50, saves ~3KB BSS)
    uint8_t pendingCount = 0;

    File dir = Storage::fs().open(hsDir);
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        uint8_t filesScanned = 0;
        while (file && pendingCount < 16) {
            // Yield every 10 files to prevent WDT on large directories
            if (++filesScanned >= 10) {
                filesScanned = 0;
                yield();
            }
            
            const char* fname = file.name();
            size_t fnameLen = strlen(fname);
            
            // wpa-sec ingests packet captures only — it runs hcxpcapngtool
            // server-side and cannot parse a hashcat .22000 (that's the OHC
            // format). Upload .pcap only; otherwise the per-BSSID dedup below
            // could pick the .22000 and the network never gets a usable capture.
            bool isPCAP = (fnameLen > 5 && strcmp(fname + fnameLen - 5, ".pcap") == 0);

            if (isPCAP) {
                // Extract the AP BSSID from the filename (handles type tokens
                // _pcap/_22000/_pmkid/_hs and both new/legacy name forms).
                char bssid[13];
                if (SDLayout::captureBssid(fname, bssid)) {

                    // Check uploaded list directly (avoids 14KB crackedCache load from isUploaded)
                    char key[13];
                    normalizeBSSID_Char(bssid, key, sizeof(key));
                    bool alreadyUploaded = false;
                    for (size_t j = 0; j < uploadedCache.size(); j++) {
                        if (strcmp(uploadedCache[j].bssid, key) == 0) { alreadyUploaded = true; break; }
                    }
                    // Also skip if another file for the SAME BSSID is already queued
                    // this run (wpa-sec dedups by network, so one upload per AP is enough).
                    if (!alreadyUploaded) {
                        for (uint8_t j = 0; j < pendingCount; j++) {
                            if (strcmp(pendingUploads[j].bssid, key) == 0) { alreadyUploaded = true; break; }
                        }
                    }
                    if (!alreadyUploaded) {
                        snprintf(pendingUploads[pendingCount].path, 
                                sizeof(pendingUploads[pendingCount].path),
                                "%s/%s", hsDir, fname);
                        memcpy(pendingUploads[pendingCount].bssid, bssid, 13);
                        pendingCount++;
                    } else {
                        result.skipped++;
                    }
                }
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }
    
    Serial.printf("[WPASEC] Found %u files to upload, %u skipped\n", 
                  (unsigned int)pendingCount, (unsigned int)result.skipped);
    
    // Free cache before TLS operations - keeps heap clear for WiFiClientSecure
    freeCacheMemory();
    
    // Track successful uploads with bitmask - avoids reloading cache during TLS
    // We mark uploaded AFTER all TLS operations complete to keep heap clear
    uint8_t successMask[50] = {0};
    
    // Upload each pending file
    if (cb) {
        cb("yoinking caps", 0, 0);
    }
    for (uint8_t i = 0; i < pendingCount; i++) {
        if (cb) {
            char status[32];
            snprintf(status, sizeof(status), "UPLOAD %u/%u", i + 1, pendingCount);
            cb(status, i + 1, pendingCount);
        }
        
        Serial.printf("[WPASEC] Heap before upload %u: %u\n", 
                      i, (unsigned int)ESP.getFreeHeap());
        
        if (uploadSingleCapture(pendingUploads[i].path, pendingUploads[i].bssid)) {
            result.uploaded++;
            successMask[i] = 1;  // Track for deferred marking
        } else {
            result.failed++;
            Serial.printf("[WPASEC] Failed: %s\n", pendingUploads[i].path);
        }
        
        // Small delay between uploads to let heap settle
        delay(100);
        yield();
    }
    
    // Mark successful uploads AFTER all TLS operations complete
    // This avoids cache reload during TLS when heap is tight
    if (result.uploaded > 0) {
        if (cb) {
            cb("marking loot", 0, 0);
        }
        loadCache();
        for (uint8_t i = 0; i < pendingCount; i++) {
            if (successMask[i]) {
                char key[13];
                normalizeBSSID_Char(pendingUploads[i].bssid, key, sizeof(key));
                // Check not already present before push
                bool found = false;
                for (size_t j = 0; j < uploadedCache.size(); j++) {
                    if (strcmp(uploadedCache[j].bssid, key) == 0) { found = true; break; }
                }
                if (!found && uploadedCache.size() < WPASEC_MAX_CACHE_ENTRIES) {
                    UploadedEntry entry;
                    memcpy(entry.bssid, key, sizeof(entry.bssid));
                    uploadedCache.push_back(entry);
                }
            }
        }
        saveUploadedList();
        Serial.printf("[WPASEC] Marked %u uploads after TLS complete\n", result.uploaded);
    }
    
    // Download potfile
    if (cb) {
        cb("slurping potfile", 0, 0);
    }
    
    // Free any residual memory before potfile TLS
    // NOTE: We do NOT recondition heap mid-sync - that causes more fragmentation!
    // If heap was good enough to start sync, trust it. Graceful degradation if not.
    freeCacheMemory();
    delay(100);
    
    Serial.printf("[WPASEC] Heap before potfile: %u largest=%u\n", 
                  (unsigned int)ESP.getFreeHeap(),
                  (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    
    uint16_t newCracks = 0;
    bool potfileOk = false;
    
    // The potfile download is a SECOND TLS handshake; on the C5 that must run in its
    // own boot (SYNC_WPA_CHK), so upload-only callers pass doDownload=false.
    potfileOk = doDownload ? downloadPotfile(newCracks) : false;
    if (!doDownload) {
        result.success = (result.failed == 0);
        if (wasReconRunning) NetworkRecon::resume();
        busy = false;
        return result;
    }
    if (potfileOk) {
        result.newCracked = newCracks;
        loadCache();
        result.cracked = crackedCache.size();
    }
    
    // Graceful degradation: partial success if uploads worked but potfile failed
    if (!potfileOk && result.uploaded > 0) {
        // Uploads succeeded, potfile failed - still report partial success
        snprintf(result.error, sizeof(result.error), "POTFILE: %s", lastError);
        result.success = true;  // Partial success - uploads worked
    } else if (!potfileOk) {
        strncpy(result.error, lastError, sizeof(result.error) - 1);
        result.success = (result.failed == 0);
    } else {
        result.success = (result.failed == 0);
    }
    
    // Resume NetworkRecon after sync operations complete
    if (wasReconRunning) {
        Serial.println("[WPASEC] Resuming NetworkRecon after TLS operations");
        NetworkRecon::resume();
    }
    
    busy = false;
    Serial.printf("[WPASEC] Sync complete: uploaded=%u failed=%u cracked=%u\n",
                  (unsigned int)result.uploaded, (unsigned int)result.failed,
                  (unsigned int)result.cracked);

    return result;
}

int WPASec::purgeCrackedCaptures() {
    loadCache();
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (!d || !d.isDirectory()) { if (d) d.close(); return 0; }

    // Collect names first — deleting while iterating the dir handle is unsafe.
    std::vector<String> toDelete;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        if (!f.isDirectory()) {
            const char* n = f.name(); const char* s = strrchr(n, '/'); if (s) n = s + 1;
            size_t len = strlen(n);
            bool cap = (len > 5 && strcmp(n + len - 5, ".pcap") == 0) ||
                       (len > 6 && strcmp(n + len - 6, ".22000") == 0);
            char bssid[13];
            if (cap && SDLayout::captureBssid(n, bssid) && isCracked(bssid))
                toDelete.push_back(String(n));
        }
        f.close();
    }
    d.close();

    int count = 0;
    char path[128];
    for (auto& name : toDelete) {
        snprintf(path, sizeof(path), "%s/%s", dir, name.c_str());
        if (Storage::fs().remove(path)) count++;
    }
    return count;
}
