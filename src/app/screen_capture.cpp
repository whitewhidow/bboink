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
#include "../core/mode_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>

namespace ScreenCapture {

static uint32_t lastDraw = 0;
static bool framed = false;   // static labels drawn once
static uint16_t prevCaps = 0;        // total captures seen, to detect new ones (LED/beep)
static uint16_t sessionStartCaps = 0; // baseline at screen entry (for the exit ntfy alert)

// Flash the onboard WS2812 green + chirp the speaker to signal a fresh capture.
static void captureNotify() {
    if (Config::wifi().soundEnabled)
        M5.Speaker.tone(2200, 120);              // beep (no-op on boards w/o speaker)
#if PORK_LED_COUNT > 0
    if (Config::wifi().ledEnabled) {
        for (int i = 0; i < 3; i++) {
            neopixelWrite(PORK_LED_PIN, 0, 60, 0);   // green
            delay(70);
            neopixelWrite(PORK_LED_PIN, 0, 0, 0);    // off
            delay(70);
        }
    }
#endif
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

    // --- small stat lines (all labels use "label: value" for consistency) ---
    M5.Display.setTextSize(1);
    int y = 51; const int lh = 12;
    auto row = [&](const char* s, uint16_t c) {
        M5.Display.fillRect(0, y, PORK_DISPLAY_W, lh, TFT_BLACK);
        M5.Display.setTextColor(c, TFT_BLACK);
        M5.Display.drawString(s, 6, y);
        y += lh;
    };

    snprintf(line, sizeof(line), "ch:%02u nets:%u pkts:%lu mgmt:%lu",
             OinkMode::getChannel(), OinkMode::getNetworkCount(),
             (unsigned long)OinkMode::getPacketCount(),
             (unsigned long)NetworkRecon::getMgmtCount());
    row(line, TFT_WHITE);

    // Always-on pool breakdown across two rows: the attack pipeline, then why the
    // rest are skipped. cap = captured/has handshake, ign = manually excluded.
    OinkMode::PoolCounts pc = OinkMode::getPoolCounts();
    snprintf(line, sizeof(line), "work: %u  cool: %u  idle: %u", pc.work, pc.cool, pc.idle);
    row(line, TFT_CYAN);
    snprintf(line, sizeof(line), "cap: %u ign: %u pmf: %u weak: %u open: %u",
             pc.cap, pc.ign, pc.pmf, pc.weak, pc.open);
    row(line, TFT_DARKGREY);

    snprintf(line, sizeof(line), "handshakes: %u   pmkid: %u",
             OinkMode::getCompleteHandshakeCount(), OinkMode::getPMKIDCount());
    row(line, TFT_GREEN);

    // Which network the last saved handshake/PMKID was for.
    const char* last = OinkMode::getLastCaptureSSID();
    snprintf(line, sizeof(line), "last: %.26s", (last && last[0]) ? last : "-");
    row(line, (last && last[0]) ? TFT_GREEN : TFT_DARKGREY);

    snprintf(line, sizeof(line), "deauth: %lu   tx: %lu / %lu",
             (unsigned long)OinkMode::getDeauthCount(),
             (unsigned long)WSLBypasser::txOkCount(),
             (unsigned long)WSLBypasser::txFailCount());
    row(line, OinkMode::isDeauthing() ? TFT_RED : TFT_DARKGREY);

    const char* tgt = OinkMode::getTargetSSID();
    int8_t trssi = OinkMode::getTargetRssi();
    if (tgt && tgt[0])
        snprintf(line, sizeof(line), "target: %.20s  %ddBm", tgt, trssi);
    else
        snprintf(line, sizeof(line), "target: -");
    row(line, (tgt && tgt[0]) ? TFT_YELLOW : TFT_DARKGREY);

    snprintf(line, sizeof(line), "clients: %u%s",
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
        // Leaving capture: ModeManager stops the engine, restores the STA
        // uplink and fires the per-network ntfy alerts, then shows management.
        ModeManager::enterManagement();
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
