// mode_manager.cpp — CAPTURE / MANAGEMENT / BLE_BRIDGE coordinator (see mode_manager.h).
//
// No board hosts a SoftAP / web UI. Management is either the on-device MENU (buttons,
// multi-button boards) or the BLE console (all boards). The single-button Waveshare
// has no menu, so its "management" is the BLE bridge directly.
//   - Waveshare (PORK_BOARD_WAVESHARE_C5_LCD): tap = CAPTURE <-> BLE_BRIDGE (reboots in).
//   - Multi-button: back = the MENU (which offers direct sync, options, and BLE bridge).
#include "mode_manager.h"
#include "config.h"
#include "net_link.h"
#include "../modes/oink.h"
#include "../app/app.h"
#include "../ble/bridge.h"
#include "boot_sync.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>

// Single-button boards have no on-device menu: their "management" IS the BLE bridge
// (tap -> reboot into bridge). Multi-button boards get the on-device MENU instead.
#if defined(PORK_BOARD_WAVESHARE_C5_LCD) || defined(PORK_BOARD_CARDPUTER_ADV)
#define PORK_SINGLE_BUTTON_MGMT 1
#endif

#if !defined(PORK_SINGLE_BUTTON_MGMT)
#include "../web/ntfy.h"
#endif

namespace ModeManager {

static Mode mode_ = Mode::CAPTURE;
static bool s_forceBridge = false;
#if !defined(PORK_SINGLE_BUTTON_MGMT)
static bool s_forceManagement = false;
#endif

Mode        current()     { return mode_; }
bool        inCapture()   { return mode_ == Mode::CAPTURE; }
const char* currentName() { return mode_ == Mode::CAPTURE ? "CAPTURE" : mode_ == Mode::BLE_BRIDGE ? "BLE_BRIDGE" : "MANAGEMENT"; }
bool        captureReady(){ return Config::wifi().captureReady; }

static void markCaptureReady() {
    if (Config::wifi().captureReady) return;
    Config::wifi().captureReady = true;
    Config::save();
}

// Identity derived once from the burned-in MAC (stable per device). Used as the
// BLE advertised name on every board.
const char* apSSID() {
    if (Config::wifi().apSSID[0]) return Config::wifi().apSSID;   // user override
    static char s[24] = {0};
    if (!s[0]) {
        uint8_t m[6];
        esp_efuse_mac_get_default(m);
        snprintf(s, sizeof(s), "BBoink-%02X%02X", m[4], m[5]);
    }
    return s;
}
const char* apPassword() {
    static char p[24] = {0};
    if (!p[0]) {
        uint8_t m[6];
        esp_efuse_mac_get_default(m);
        snprintf(p, sizeof(p), "oink%02X%02X%02X", m[3], m[4], m[5]);
    }
    return p;
}

void enterCapture(bool clearLock) {
    if (BleBridge::running()) BleBridge::stop();
    if (clearLock) OinkMode::clearTargetLock();
    markCaptureReady();
    WiFi.mode(WIFI_STA);
    mode_ = Mode::CAPTURE;
    App::go(App::Screen::CAPTURE);
}

#if defined(PORK_SINGLE_BUTTON_MGMT)
// Single-button, no menu: "management" is the BLE bridge (reboots in for the BT RAM).
void enterManagement() { requestBleBridge(); }
#else
// Multi-button: management is the on-device MENU (no AP/web). Leaving capture stops
// the engine, restores the STA uplink, and fires the per-network ntfy alerts.
void enterManagement() {
    const bool leavingCapture = (mode_ == Mode::CAPTURE);
    // ntfy disabled for now (inconsistent across boards) — code kept below.
    const bool wantNtfy = false && leavingCapture &&
                          (OinkMode::getSessionCaptureCount() > 0) && Ntfy::enabled();
    if (leavingCapture) {
        OinkMode::stop();
        esp_wifi_set_promiscuous(false);
    }
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);

    const char* ssid = Config::wifi().otaSSID;
    bool linked = false;
    if (ssid && ssid[0]) {
        App::clear();
        App::centerMsg("connecting wifi", TFT_CYAN);
        linked = NetLink::connectConfigured();
    }
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
    App::go(App::Screen::MENU);
}
#endif

void enterBleBridge() {
    // Bridge is BLE-only: WiFi fully off so NimBLE has the radio + heap. Reached at
    // boot (forceBleBridge) or live from the menu (multi-button boards).
    if (OinkMode::isRunning()) { OinkMode::stop(); esp_wifi_set_promiscuous(false); }
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(150);
    BleBridge::start(apSSID());
    mode_ = Mode::BLE_BRIDGE;
    App::go(App::Screen::BLEBRIDGE);
}

void toggle() {
#if defined(PORK_SINGLE_BUTTON_MGMT)
    if (mode_ == Mode::BLE_BRIDGE) enterCapture();      // bridge -> capture (live)
    else                           requestBleBridge();  // capture -> reboot into bridge
#else
    if (mode_ == Mode::BLE_BRIDGE) enterCapture();
    else if (mode_ == Mode::CAPTURE) enterManagement();
    else                             enterCapture();
#endif
}

void forceBleBridgeBoot() { s_forceBridge = true; }
#if !defined(PORK_SINGLE_BUTTON_MGMT)
void forceManagementBoot() { s_forceManagement = true; }
#else
void forceManagementBoot() {}   // no management mode on this board
#endif

void begin() {
    if (s_forceBridge) { s_forceBridge = false; enterBleBridge(); return; }
#if !defined(PORK_SINGLE_BUTTON_MGMT)
    if (s_forceManagement) { s_forceManagement = false; enterManagement(); return; }
    if (Config::wifi().bootModePolicy == 2) { enterManagement(); return; }   // policy: always management
#endif
    enterCapture();
}

} // namespace ModeManager
