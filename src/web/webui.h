// webui.h — the MANAGEMENT-mode web server (served off the SoftAP).
//
// M3: minimal server + read-only JSON API + a captive-portal DNS redirect so the
// page pops on the phone automatically. Started by ModeManager when entering
// MANAGEMENT (AP up), stopped when leaving for CAPTURE. Pumped from the connect
// screen's tick(). The full tabbed SPA + write API is M4.
#pragma once
#include <Arduino.h>

namespace WebUI {
void begin();      // start the HTTP server + captive DNS on the SoftAP
void stop();       // stop both (called before capture takes the radio)
void loop();       // pump DNS + HTTP; call each frame while in MANAGEMENT
bool running();
void servicePendingSync();   // run a queued sync (AP torn down for heap); call each frame in MANAGEMENT
} // namespace WebUI
