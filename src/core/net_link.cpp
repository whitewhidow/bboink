// net_link.cpp — shared STA-link helper (see net_link.h).
#include "net_link.h"
#include "config.h"
#include <WiFi.h>

namespace NetLink {

bool connectConfigured(uint32_t timeoutMs) {
    const char* ssid = Config::wifi().otaSSID;
    const char* pass = Config::wifi().otaPassword;
    if (!ssid || ssid[0] == '\0') return false;

    // Already on the configured SSID -> reuse the live (boot-held) connection.
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) return true;

    // Reconnect on the running driver (works even with the display up).
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
        yield();
    }
    return WiFi.status() == WL_CONNECTED;
}

} // namespace NetLink
