// mode_manager.cpp — CAPTURE / MANAGEMENT coordinator (see mode_manager.h).
#include "mode_manager.h"
#include "config.h"
#include "net_link.h"
#include "../modes/oink.h"
#include "../web/ntfy.h"
#include "../web/webui.h"
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

// SoftAP identity, derived once from the STA MAC so it's stable per device.
// WPA2 requires >= 8 chars; the password below is 10. (Per-device, shown on the
// connect screen; a user-set override can come later.)
const char* apSSID() {
    static char s[24] = {0};
    if (!s[0]) {
        uint8_t m[6]; WiFi.macAddress(m);
        snprintf(s, sizeof(s), "BBoink-%02X%02X", m[4], m[5]);
    }
    return s;
}
const char* apPassword() {
    static char p[24] = {0};
    if (!p[0]) {
        uint8_t m[6]; WiFi.macAddress(m);
        snprintf(p, sizeof(p), "oink%02X%02X%02X", m[3], m[4], m[5]);
    }
    return p;
}

// Raise the management SoftAP (WPA2). AP IP defaults to 192.168.4.1.
static bool apUp = false;
static void startSoftAP() {
    WiFi.softAP(apSSID(), apPassword());
    apUp = true;
}

void enterCapture(bool clearLock) {
    if (clearLock) OinkMode::clearTargetLock();
    markCaptureReady();
    // Drop the web server + management SoftAP; capture needs the radio to itself.
    // Only touch the AP interface if we actually started one — calling
    // softAPdisconnect() at boot (no AP netif) faults in hostap_attach on the C5.
    if (apUp) { WebUI::stop(); WiFi.softAPdisconnect(true); apUp = false; }
    WiFi.mode(WIFI_STA);
    mode_ = Mode::CAPTURE;
    // ScreenCapture::enter() releases the boot STA link, disables auto-reconnect
    // and calls OinkMode::start() (promiscuous). Kept there so a direct
    // App::go(CAPTURE) still fully arms the engine.
    App::go(App::Screen::CAPTURE);
}

void enterManagement() {
    const bool leavingCapture = (mode_ == Mode::CAPTURE);
    // The per-network session list (OinkMode::getSessionCapture*) survives stop()
    // — only start() clears it — so it's safe to read after stop() below.
    const bool wantNtfy = leavingCapture &&
                          (OinkMode::getSessionCaptureCount() > 0) && Ntfy::enabled();

    if (leavingCapture) {
        OinkMode::stop();
        esp_wifi_set_promiscuous(false);   // capture left the radio in promiscuous
    }
    WiFi.setAutoReconnect(true);

    // MANAGEMENT is AP+STA: the SoftAP hosts the connect screen / web UI, while the
    // STA joins the configured network for cracking sync (topology #1, docs §7.4).
    // Bring the AP up robustly: the AP_STA netif must be fully created before
    // softAP() or the C5 faults in hostap_attach (a boot-time race). Give the
    // stack a solid settle window after the mode switch.
    WiFi.mode(WIFI_AP_STA);
    delay(250);
    startSoftAP();
    WebUI::begin();          // serve the management page off the SoftAP

    const char* ssid = Config::wifi().otaSSID;
    bool linked = false;
    if (ssid && ssid[0]) {
        App::clear();
        App::centerMsg("connecting wifi", TFT_CYAN);
        linked = NetLink::connectConfigured();   // reconnect on the running driver
    }

    // One ntfy alert PER captured network on the way out of capture (both .pcap +
    // .22000 when Ntfy File is on) — can't send mid-capture (promiscuous drops STA).
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

    mode_ = Mode::MANAGEMENT;
    App::go(App::Screen::CONNECT);   // SSID/IP/QR + STA status (web UI serves off this AP later)
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
        default: goCapture = true;   // auto: boot into CAPTURE (safe; no boot-time SoftAP)
    }
    if (goCapture) { enterCapture(); }
    else {
        // mode_ starts MANAGEMENT, so enterManagement() skips the capture-teardown
        // path and just raises AP+STA and shows the connect screen.
        enterManagement();
    }
}

} // namespace ModeManager
