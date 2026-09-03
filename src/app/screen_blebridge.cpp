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

// Fixed public BLE-console app (GitHub Pages) — same for everyone; overridable via appUrl.
static const char* CONSOLE_URL = "https://whitewhidow.github.io/bboink/bridge/";

static bool     pConn = false;
static uint16_t pFiles = 0xFFFF, pCrk = 0xFFFF;

static void draw() {
    App::clear();
    const int W = PORK_DISPLAY_W;
    M5.Display.setTextDatum(top_center);
#if defined(PORK_BOARD_TDONGLE_S3)
    const int TTS = 1, TY = 2, Y0 = 15, DS = 10, DL = 11; const bool COMPACT = true;   // 160x80
#else
    const int TTS = 2, TY = 6, Y0 = 34, DS = 14, DL = 20; const bool COMPACT = false;
#endif
    // Title (top, not vertically centred — the content below must all fit).
    M5.Display.setTextSize(TTS);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("BLE BRIDGE", W / 2, TY);
    M5.Display.setTextSize(1);
    int y = Y0;
    const bool conn = BleBridge::connected();
    M5.Display.setTextColor(conn ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(conn ? "phone connected" : "open the app + connect", W / 2, y); y += DS;
    if (!COMPACT) {
        char l[40]; snprintf(l, sizeof(l), "files %u   cracked %u", BleBridge::filesSent(), BleBridge::crackedIn());
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.drawString(l, W / 2, y); y += DL;
    }
    // What the phone needs: the app URL to open + the relay it targets.
    const char* au = Config::wifi().appUrl[0] ? Config::wifi().appUrl : CONSOLE_URL;
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK); M5.Display.drawString("open on phone:", W / 2, y); y += DS;
    M5.Display.setTextColor(0x5AEB, TFT_BLACK);       M5.Display.drawString(au, W / 2, y); y += DL;
    if (!COMPACT) {
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK); M5.Display.drawString("relay:", W / 2, y); y += DS;
        const char* ru = Config::wifi().relayUrl;
        if (ru[0]) { M5.Display.setTextColor(0x5AEB, TFT_BLACK); M5.Display.drawString(ru, W / 2, y); }
        else       { M5.Display.setTextColor(TFT_RED, TFT_BLACK); M5.Display.drawString("(not set)", W / 2, y); }
    }
    App::footer("exit to capture");
}

void enter() {
    pConn = false; pFiles = pCrk = 0xFFFF;
    draw();
}

void tick(const App::Input& in) {
    BleBridge::loop();
    if (BleBridge::exitRequested()) { ModeManager::enterCapture(); return; }
#if !defined(PORK_BOARD_WAVESHARE_C5_LCD)
    if (in.back || in.enter) { ModeManager::enterCapture(); return; }   // multi-button: exit bridge
#endif
    // Redraw only when something changed (no periodic flicker while streaming).
    if (BleBridge::connected() != pConn || BleBridge::filesSent() != pFiles ||
        BleBridge::crackedIn() != pCrk) {
        pConn = BleBridge::connected(); pFiles = BleBridge::filesSent(); pCrk = BleBridge::crackedIn();
        draw();
    }
}

} // namespace ScreenBleBridge
