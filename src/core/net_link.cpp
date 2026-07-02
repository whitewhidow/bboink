// net_link.cpp — shared STA-link helper (see net_link.h).
#include "net_link.h"
#include "config.h"
#include "../web/wpasec.h"
#include <WiFi.h>

namespace NetLink {

// Opt-in fallback: scan and associate to any in-range network we've cracked,
// using the recovered password, so a field sync/upload can still get an uplink
// when the configured AP isn't around. Requires you be authorized on those APs.
static bool connectCracked(uint32_t timeoutMs) {
    WPASec::loadCache();
    int n = WiFi.scanNetworks();
    bool ok = false;
    for (int i = 0; i < n && !ok; i++) {
        const uint8_t* b = WiFi.BSSID(i);
        if (!b) continue;
        char bstr[13];
        snprintf(bstr, sizeof(bstr), "%02X%02X%02X%02X%02X%02X",
                 b[0], b[1], b[2], b[3], b[4], b[5]);
        if (!WPASec::isCracked(bstr)) continue;
        const char* pass = WPASec::getPassword(bstr);
        if (!pass || !pass[0]) continue;
        WiFi.begin(WiFi.SSID(i).c_str(), pass);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) { delay(200); yield(); }
        ok = (WiFi.status() == WL_CONNECTED);
    }
    WiFi.scanDelete();
    return ok;
}

bool connectConfigured(uint32_t timeoutMs) {
    const char* ssid = Config::wifi().otaSSID;
    const char* pass = Config::wifi().otaPassword;

    if (ssid && ssid[0]) {
        // Already on the configured SSID -> reuse the live (boot-held) connection.
        if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) return true;
        // Reconnect on the running driver (works even with the display up).
        WiFi.begin(ssid, pass);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
            delay(250);
            yield();
        }
        if (WiFi.status() == WL_CONNECTED) return true;
    }

    // Opt-in: fall back to an in-range cracked network as the uplink.
    if (Config::wifi().crackedFallback) return connectCracked(timeoutMs < 8000 ? timeoutMs : 8000);
    return false;
}

} // namespace NetLink
