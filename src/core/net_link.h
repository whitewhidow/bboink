// net_link.h — shared STA-link helper used by the sync screens.
#pragma once

#include <Arduino.h>

namespace NetLink {

// Ensure the STA link to the configured OTA SSID is up. Reuses the association
// held from boot (claimed before the display grabbed the shared GDMA resource);
// if creds changed or the link dropped it *reconnects on the already-running
// driver* (a brand-new init after the display is up fails to associate — see the
// WiFi notes in screen_manage.cpp). Returns true if connected.
bool connectConfigured(uint32_t timeoutMs = 15000);

} // namespace NetLink
