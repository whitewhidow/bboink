// mode_manager.cpp — CAPTURE / MANAGEMENT coordinator (see mode_manager.h).
#include "mode_manager.h"
#include "config.h"
#include "net_link.h"
#include "../modes/oink.h"
#include "../web/ntfy.h"
#include "../app/app.h"
#include <WiFi.h>
#include <esp_wifi.h>

namespace ModeManager {

// Start in MANAGEMENT so enterManagement()'s "leaving capture" work is skipped
// on a cold boot (the engine has never run yet).
static Mode mode_ = Mode::MANAGEMENT;

Mode        current()     { return mode_; }
bool        inCapture()   { return mode_ == Mode::CAPTURE; }
const char* currentName() { return mode_ == Mode::CAPTURE ? "CAPTURE" : "MANAGEMENT"; }
bool        captureReady(){ return Config::wifi().captureReady; }

// Persist the "provisioned for capture" flag the first time we enter capture.
// Proxy for §7.1's provisioning wizard until that lands: once you've captured,
// the device knows what to do and boots straight into CAPTURE thereafter.
static void markCaptureReady() {
    if (Config::wifi().captureReady) return;
    Config::wifi().captureReady = true;
    Config::save();
}

void enterCapture(bool clearLock) {
    if (clearLock) OinkMode::clearTargetLock();
    markCaptureReady();
    mode_ = Mode::CAPTURE;
    // ScreenCapture::enter() releases the boot STA link, disables auto-reconnect
    // and calls OinkMode::start() (promiscuous). Kept there so a direct
    // App::go(CAPTURE) still fully arms the engine.
    App::go(App::Screen::CAPTURE);
}

void enterManagement() {
    // Only tear the radio back down if we're actually leaving capture; a boot or
    // menu-side entry has no promiscuous session to unwind.
    if (mode_ == Mode::CAPTURE) {
        // The per-network session list (OinkMode::getSessionCapture*) survives
        // stop() — only start() clears it — so it's safe to read after stop().
        bool wantNtfy = (OinkMode::getSessionCaptureCount() > 0) && Ntfy::enabled();

        OinkMode::stop();
        // Capture left WiFi promiscuous/disconnected. Restore the STA uplink on
        // the still-initialised driver (a fresh init post-display would fail) —
        // needed for sync and the ntfy push below.
        esp_wifi_set_promiscuous(false);
        WiFi.setAutoReconnect(true);

        const char* ssid = Config::wifi().otaSSID;
        bool linked = false;
        if (ssid && ssid[0]) {
            App::clear();
            App::centerMsg("reconnecting wifi", TFT_CYAN);
            linked = NetLink::connectConfigured();   // robust ~15s reconnect
        }

        // One ntfy alert PER captured network (both .pcap + .22000 attached when
        // Ntfy File is on). Can't send mid-capture (promiscuous drops STA), so do
        // it here on the way out. Always report the outcome.
        if (wantNtfy) {
            if (linked) {
                int nsc = OinkMode::getSessionCaptureCount();
                int sent = 0;
                for (int i = 0; i < nsc; i++) {
                    char l[40]; snprintf(l, sizeof(l), "notifying %d/%d...", i + 1, nsc);
                    App::clear(); App::centerMsg(l, TFT_CYAN);
                    if (Ntfy::sendCaptureFor(OinkMode::getSessionCaptureSSID(i),
                                             OinkMode::getSessionCaptureBssid(i))) sent++;
                }
                char l[40]; snprintf(l, sizeof(l), "ntfy sent %d/%d", sent, nsc);
                App::clear(); App::centerMsg(l, sent ? TFT_GREEN : TFT_RED);
            } else {
                App::clear(); App::centerMsg("ntfy: no wifi", TFT_YELLOW);
            }
            delay(1200);
        }
    }

    mode_ = Mode::MANAGEMENT;
    App::go(App::Screen::MENU);   // the management surface (web UI replaces it later)
}

void toggle() {
    if (mode_ == Mode::CAPTURE) enterManagement();
    else                        enterCapture();
}

// Resolve the boot mode from the persisted policy, then enter it.
void begin() {
    uint8_t policy = Config::wifi().bootModePolicy;   // 0=auto, 1=capture, 2=management
    bool goCapture;
    switch (policy) {
        case 1:  goCapture = true;  break;                 // always capture
        case 2:  goCapture = false; break;                 // always management
        default: goCapture = Config::wifi().captureReady;  // auto: ready -> capture
    }
    if (goCapture) enterCapture();
    else { mode_ = Mode::MANAGEMENT; App::go(App::Screen::MENU); }
}

} // namespace ModeManager
