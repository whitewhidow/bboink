// screen_capture.cpp — the "oink": start the engine, show live stats.
// The OinkMode engine channel-hops, auto-targets, deauths and auto-saves
// handshakes/PMKIDs to SD on its own; we just pump it and render counters.
#include "app.h"
#include "../modes/oink.h"
#include "../core/network_recon.h"
#include "../core/wsl_bypasser.h"
#include "../core/config.h"
#include <WiFi.h>
#include <esp_wifi.h>

namespace ScreenCapture {

static uint32_t lastDraw = 0;
static bool framed = false;   // static labels drawn once

static void drawFrame() {
    App::clear();
    App::header("CAPTURE");
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
}

void tick(const App::Input& in) {
    if (in.back) {
        OinkMode::stop();
        // Capture left WiFi in promiscuous/disconnected. The driver is still
        // initialised (holds the GDMA resource — porkchop never deinits WiFi), so
        // re-associate here to restore the uplink for sync. Reconnect on the
        // running driver works post-display; a fresh init would not.
        esp_wifi_set_promiscuous(false);
        WiFi.setAutoReconnect(true);
        const char* ssid = Config::wifi().otaSSID;
        if (ssid && ssid[0]) {
            App::clear();
            App::centerMsg("reconnecting wifi", TFT_CYAN);
            WiFi.begin(ssid, Config::wifi().otaPassword);
            uint32_t t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) { delay(100); yield(); }
        }
        App::go(App::Screen::MENU);
        return;
    }
    // Pump the capture engine every frame.
    NetworkRecon::update();
    OinkMode::update();

    if (!framed) drawFrame();
    uint32_t now = millis();
    if (now - lastDraw >= 250) {
        drawStats();
        lastDraw = now;
    }
}

} // namespace ScreenCapture
