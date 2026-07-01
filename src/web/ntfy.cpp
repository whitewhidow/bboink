// ntfy.sh push-notification client (see ntfy.h).
#include "ntfy.h"
#include "../core/config.h"
#include "../core/storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>

static const char*    NTFY_HOST = "ntfy.sh";
static const uint16_t NTFY_PORT = 443;

namespace Ntfy {

bool enabled() {
    const char* t = Config::wifi().ntfyTopic;
    return t && t[0] != '\0';
}

// Copy a string into an HTTP-header-safe form: printable ASCII only, no CR/LF.
static void headerSafe(const char* in, char* out, size_t outLen) {
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o < outLen - 1; i++) {
        char c = in[i];
        out[o++] = (c >= 0x20 && c < 0x7f) ? c : '_';
    }
    out[o] = '\0';
}

static bool readStatusOk(WiFiClientSecure& c) {
    uint32_t to = millis() + 8000;
    while (c.connected() && !c.available() && millis() < to) { delay(10); yield(); }
    if (!c.available()) return false;
    char line[64];
    size_t n = c.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    return strstr(line, " 200") || strstr(line, " 201");
}

static bool sendText(const char* topic, const char* title, const char* msg) {
    WiFiClientSecure c; c.setInsecure();
    if (!c.connect(NTFY_HOST, NTFY_PORT, 10000)) return false;
    c.printf("POST /%s HTTP/1.1\r\n", topic);
    c.printf("Host: %s\r\n", NTFY_HOST);
    c.printf("Title: %s\r\n", title);
    c.printf("Content-Length: %u\r\n", (unsigned)strlen(msg));
    c.print("Connection: close\r\n\r\n");
    c.print(msg);
    bool ok = readStatusOk(c);
    c.stop();
    return ok;
}

static bool sendFile(const char* topic, const char* title, const char* msg, const char* path) {
    File f = Storage::fs().open(path, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > 1500000) { f.close(); return false; }   // ntfy.sh ~2MB cap
    const char* fn = strrchr(path, '/'); fn = fn ? fn + 1 : path;

    WiFiClientSecure c; c.setInsecure();
    if (!c.connect(NTFY_HOST, NTFY_PORT, 10000)) { f.close(); return false; }
    c.printf("PUT /%s HTTP/1.1\r\n", topic);
    c.printf("Host: %s\r\n", NTFY_HOST);
    c.printf("Title: %s\r\n", title);
    c.printf("Message: %s\r\n", msg);
    c.printf("Filename: %s\r\n", fn);
    c.printf("Content-Length: %u\r\n", (unsigned)sz);
    c.print("Connection: close\r\n\r\n");

    uint8_t buf[256]; size_t sent = 0;
    while (f.available() && sent < sz) {
        size_t r = f.read(buf, sizeof(buf));
        if (r) { c.write(buf, r); sent += r; }
        yield();
    }
    f.close();
    bool ok = readStatusOk(c);
    c.stop();
    return ok;
}

bool sendCapture(const char* ssid, const char* filePath, uint16_t newCount) {
    if (!enabled()) return false;
    const char* topic = Config::wifi().ntfyTopic;

    char safeSsid[40];
    headerSafe((ssid && ssid[0]) ? ssid : "(hidden)", safeSsid, sizeof(safeSsid));
    char title[48]; snprintf(title, sizeof(title), "BBoink: %u capture(s)", newCount);
    char msg[64];   snprintf(msg, sizeof(msg), "latest: %.32s", safeSsid);

    if (Config::wifi().ntfyAttachFile && filePath && filePath[0]) {
        if (sendFile(topic, title, msg, filePath)) return true;  // else fall back to text
    }
    return sendText(topic, title, msg);
}

} // namespace Ntfy
