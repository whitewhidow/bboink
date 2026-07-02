// screen_capture.cpp — the "oink": start the engine, show live stats.
// The OinkMode engine channel-hops, auto-targets, deauths and auto-saves
// handshakes/PMKIDs to SD on its own; we just pump it and render counters.
#include "app.h"
#include "../modes/oink.h"
#include "../core/network_recon.h"
#include "../core/wsl_bypasser.h"
#include "../core/config.h"
#include "../web/ntfy.h"
#include "../core/net_link.h"
#include <WiFi.h>
#include <esp_wifi.h>

namespace ScreenCapture {

static uint32_t lastDraw = 0;
static bool framed = false;   // static labels drawn once
static uint16_t prevCaps = 0;        // total captures seen, to detect new ones (LED/beep)
static uint16_t sessionStartCaps = 0; // baseline at screen entry (for the exit ntfy alert)

// Flash the onboard WS2812 green + chirp the speaker to signal a fresh capture.
static void captureNotify() {
    M5.Speaker.tone(2200, 120);                  // beep
    for (int i = 0; i < 3; i++) {
        neopixelWrite(PORK_LED_PIN, 0, 60, 0);   // green
        delay(70);
        neopixelWrite(PORK_LED_PIN, 0, 0, 0);    // off
        delay(70);
    }
}

static void drawFrame() {
    App::clear();
    if (OinkMode::isTargetLocked()) {
        char h[40];
        snprintf(h, sizeof(h), "TGT %.22s", OinkMode::getLockSSID());
        App::header(h);
    } else {
        App::header("CAPTURE");
    }
    App::footer("back: stop & exit");
    framed = true;
}

static void drawStats() {
    char line[48];

    // --- current phase, prominent + colour-coded ---
    const char* st = OinkMode::getStateString();
    uint16_t sc = TFT_CYAN;
    if      (strstr(st, "DEAUTH"))     sc = TFT_RED;
    else if (strstr(st, "LOCK"))       sc = TFT_YELLOW;
    else if (strstr(st, "PMKID"))      sc = TFT_MAGENTA;
    else if (strstr(st, "NEXT"))       sc = TFT_WHITE;
    else if (strstr(st, "WAIT") || strstr(st, "NO TARGETS")) sc = TFT_DARKGREY;
    M5.Display.fillRect(0, 30, PORK_DISPLAY_W, 18, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(sc, TFT_BLACK);
    M5.Display.drawString(st, 6, 30);

    // --- small stat lines ---
    M5.Display.setTextSize(1);
    int y = 52; const int lh = 13;
    auto row = [&](const char* s, uint16_t c) {
        M5.Display.fillRect(0, y, PORK_DISPLAY_W, lh, TFT_BLACK);
        M5.Display.setTextColor(c, TFT_BLACK);
        M5.Display.drawString(s, 6, y);
        y += lh;
    };

    // When there's nothing to attack, explain why (pmf/captured/weak/open/idle).
    if (strstr(st, "NO TARGETS")) row(OinkMode::getNoTargetSummary(), TFT_ORANGE);

    snprintf(line, sizeof(line), "ch %02u   networks %u   pkts %lu",
             OinkMode::getChannel(), OinkMode::getNetworkCount(),
             (unsigned long)OinkMode::getPacketCount());
    row(line, TFT_WHITE);

    snprintf(line, sizeof(line), "handshakes %u   pmkid %u",
             OinkMode::getCompleteHandshakeCount(), OinkMode::getPMKIDCount());
    row(line, TFT_GREEN);

    // Which network the last saved handshake/PMKID was for.
    const char* last = OinkMode::getLastCaptureSSID();
    snprintf(line, sizeof(line), "last: %.26s", (last && last[0]) ? last : "-");
    row(line, (last && last[0]) ? TFT_GREEN : TFT_DARKGREY);

    snprintf(line, sizeof(line), "deauth %lu   tx ok %lu / fail %lu",
             (unsigned long)OinkMode::getDeauthCount(),
             (unsigned long)WSLBypasser::txOkCount(),
             (unsigned long)WSLBypasser::txFailCount());
    row(line, OinkMode::isDeauthing() ? TFT_RED : TFT_DARKGREY);

    const char* tgt = OinkMode::getTargetSSID();
    snprintf(line, sizeof(line), "target: %.26s", (tgt && tgt[0]) ? tgt : "-");
    row(line, (tgt && tgt[0]) ? TFT_YELLOW : TFT_DARKGREY);

    snprintf(line, sizeof(line), "clients %u%s",
             OinkMode::getTargetClientCount(),
             OinkMode::isTargetHidden() ? "   [hidden SSID]" : "");
    row(line, TFT_WHITE);

    // The NO TARGETS breakdown adds an extra row; when it disappears the row count
    // shrinks, so clear any stale trailing line left below the last row drawn.
    if (y < 148) M5.Display.fillRect(0, y, PORK_DISPLAY_W, 148 - y, TFT_BLACK);
}

void enter() {
    // The boot WiFi connection (kept alive for sync) must be released for capture,
    // and auto-reconnect disabled — otherwise Arduino keeps re-associating to the
    // home AP and pins the radio to its channel, breaking channel-hopping (the
    // engine then only ever sees / targets that one network).
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    OinkMode::start();
    framed = false;
    lastDraw = 0;
    // Baseline the capture count so we don't notify for ones already captured.
    prevCaps = OinkMode::getCompleteHandshakeCount() + OinkMode::getPMKIDCount();
    sessionStartCaps = prevCaps;
}

void tick(const App::Input& in) {
    if (in.back) {
        OinkMode::stop();
        // Capture left WiFi in promiscuous/disconnected. Restore the STA uplink on
        // the still-initialised driver (a fresh init post-display would fail) — it's
        // needed both for sync and for the ntfy push below.
        esp_wifi_set_promiscuous(false);
        WiFi.setAutoReconnect(true);

        uint16_t caps = OinkMode::getCompleteHandshakeCount() + OinkMode::getPMKIDCount();
        bool wantNtfy = (caps > sessionStartCaps) && Ntfy::enabled();

        const char* ssid = Config::wifi().otaSSID;
        bool linked = false;
        if (ssid && ssid[0]) {
            App::clear();
            App::centerMsg("reconnecting wifi", TFT_CYAN);
            linked = NetLink::connectConfigured();   // robust ~15s reconnect
        }

        // Push an ntfy alert for this session's captures (can't send mid-capture:
        // promiscuous drops the STA link). Always report the outcome so it's never
        // a silent no-op.
        if (wantNtfy) {
            if (linked) {
                App::centerMsg("notifying phone...", TFT_CYAN);
                bool sent = Ntfy::sendCapture(OinkMode::getLastCaptureSSID(),
                                              OinkMode::getLastCapturePath(),
                                              caps - sessionStartCaps);
                App::centerMsg(sent ? "ntfy sent" : "ntfy failed", sent ? TFT_GREEN : TFT_RED);
            } else {
                App::centerMsg("ntfy: no wifi", TFT_YELLOW);
            }
            delay(1200);
        }
        App::go(App::Screen::MENU);
        return;
    }
    // Pump the capture engine every frame.
    NetworkRecon::update();
    OinkMode::update();

    // New handshake/PMKID since last frame? Flash the LED to signal it.
    uint16_t caps = OinkMode::getCompleteHandshakeCount() + OinkMode::getPMKIDCount();
    if (caps > prevCaps) captureNotify();
    prevCaps = caps;

    if (!framed) drawFrame();
    uint32_t now = millis();
    if (now - lastDraw >= 250) {
        drawStats();
        lastDraw = now;
    }
}

} // namespace ScreenCapture
