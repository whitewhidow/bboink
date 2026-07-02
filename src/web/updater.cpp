// Self-update client (see updater.h).
#include "updater.h"
#include "../core/storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>

namespace Updater {

Result fetchToSD(const char* url, const char* sdPath, void (*progress)(size_t, size_t)) {
    Result r = {};
    if (WiFi.status() != WL_CONNECTED)          { strncpy(r.error, "WIFI NOT CONNECTED", sizeof(r.error) - 1); return r; }
    if (!url || !url[0] || !sdPath || !sdPath[0]) { strncpy(r.error, "BAD URL/PATH", sizeof(r.error) - 1); return r; }

    WiFiClientSecure client;
    client.setInsecure();                       // GitHub certs are valid; skip the bundle
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, url)) { strncpy(r.error, "HTTP BEGIN FAIL", sizeof(r.error) - 1); return r; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        snprintf(r.error, sizeof(r.error), "HTTP %d", code);
        http.end();
        return r;
    }
    int total = http.getSize();                 // -1 if the server omits Content-Length

    // Download into a temp file; only swap over the real path once it's verified.
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "%s.tmp", sdPath);
    Storage::fs().remove(tmp);
    File f = Storage::fs().open(tmp, FILE_WRITE);
    if (!f) { strncpy(r.error, "SD OPEN FAIL", sizeof(r.error) - 1); http.end(); return r; }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t got = 0;
    bool   magicOk = false;
    uint32_t lastData = millis();
    while (http.connected() && (total < 0 || got < (size_t)total)) {
        size_t avail = stream->available();
        if (avail) {
            int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
            if (n > 0) {
                if (got == 0) magicOk = (buf[0] == 0xE9);   // ESP app image magic byte
                f.write(buf, n);
                got += n;
                lastData = millis();
                if (progress) progress(got, total > 0 ? (size_t)total : 0);
            }
        } else {
            if (millis() - lastData > 10000) break;         // stalled
            delay(5);
        }
        yield();
    }
    f.close();
    http.end();

    if (!magicOk) {
        Storage::fs().remove(tmp);
        strncpy(r.error, "NOT A FIRMWARE IMG", sizeof(r.error) - 1);   // e.g. an HTML error page
        return r;
    }
    if (total > 0 && got != (size_t)total) {
        Storage::fs().remove(tmp);
        strncpy(r.error, "INCOMPLETE DOWNLOAD", sizeof(r.error) - 1);
        return r;
    }

    Storage::fs().remove(sdPath);
    if (!Storage::fs().rename(tmp, sdPath)) {
        Storage::fs().remove(tmp);
        strncpy(r.error, "SD RENAME FAIL", sizeof(r.error) - 1);
        return r;
    }
    r.ok = true;
    r.bytes = got;
    return r;
}

Result fetchToFlash(const char* url, void (*progress)(size_t, size_t)) {
    Result r = {};
    if (WiFi.status() != WL_CONNECTED)          { strncpy(r.error, "WIFI NOT CONNECTED", sizeof(r.error) - 1); return r; }
    if (!url || !url[0])                         { strncpy(r.error, "BAD URL", sizeof(r.error) - 1); return r; }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, url)) { strncpy(r.error, "HTTP BEGIN FAIL", sizeof(r.error) - 1); return r; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) { snprintf(r.error, sizeof(r.error), "HTTP %d", code); http.end(); return r; }

    int total = http.getSize();                 // Update needs a known size
    if (total <= 0) { strncpy(r.error, "NO CONTENT-LENGTH", sizeof(r.error) - 1); http.end(); return r; }

    // Update.begin() targets the inactive OTA partition and validates the image
    // (magic byte + checksum) as it writes.
    if (!Update.begin((size_t)total)) {
        snprintf(r.error, sizeof(r.error), "BEGIN: %s", Update.errorString());
        http.end();
        return r;
    }
    if (progress) Update.onProgress([progress](size_t d, size_t t) { progress(d, t); });

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != (size_t)total) {
        snprintf(r.error, sizeof(r.error), "WROTE %u/%d", (unsigned)written, total);
        Update.abort();
        return r;
    }
    if (!Update.end(true)) {                     // true = set the new partition bootable
        snprintf(r.error, sizeof(r.error), "END: %s", Update.errorString());
        return r;
    }
    r.ok = true;
    r.bytes = written;
    return r;
}

} // namespace Updater
