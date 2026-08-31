// screen_blebridge.cpp — BLE_BRIDGE mode status screen (Waveshare C5, single button).
//
// The board is a NimBLE peripheral; an Android Web-Bluetooth app pulls captures and
// pushes back cracked results over the phone's own cellular. This screen just pumps
// the bridge and shows status. A tap (ModeManager::toggle in App::tick) exits to
// CAPTURE; the phone can also send {"c":"done"}. See docs/DESIGN-ble-bridge.md.
#include "app.h"
#include "../core/mode_manager.h"
#include "../core/config.h"
#include "../ble/bridge.h"

namespace ScreenBleBridge {

static bool     pConn = false;
static uint16_t pFiles = 0xFFFF, pCrk = 0xFFFF;

static void draw() {
    App::clear();
    App::centerMsg("BLE BRIDGE", TFT_CYAN);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_center);
    const bool conn = BleBridge::connected();
    M5.Display.setTextColor(conn ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(conn ? "phone connected" : "open app + connect",
                          PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2 + 18);
    char l[40]; snprintf(l, sizeof(l), "files %u   cracked %u", BleBridge::filesSent(), BleBridge::crackedIn());
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString(l, PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2 + 34);
    // Relay the phone app should target (board doesn't use it in bridge mode — shown for reference).
    const char* ru = Config::wifi().relayUrl;
    if (ru[0]) {
        char rl[112]; snprintf(rl, sizeof(rl), "relay: %s", ru);
        M5.Display.setTextColor(0x5AEB, TFT_BLACK);   // light blue-grey
        M5.Display.drawString(rl, PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2 + 50);
    } else {
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("missing relay url", PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2 + 50);
    }
    App::footer("tap: exit to capture");
}

void enter() {
    pConn = false; pFiles = pCrk = 0xFFFF;
    draw();
}

void tick(const App::Input&) {
    BleBridge::loop();
    if (BleBridge::exitRequested()) { ModeManager::enterCapture(); return; }
    // Redraw only when something changed (no periodic flicker while streaming).
    if (BleBridge::connected() != pConn || BleBridge::filesSent() != pFiles ||
        BleBridge::crackedIn() != pCrk) {
        pConn = BleBridge::connected(); pFiles = BleBridge::filesSent(); pCrk = BleBridge::crackedIn();
        draw();
    }
}

} // namespace ScreenBleBridge
