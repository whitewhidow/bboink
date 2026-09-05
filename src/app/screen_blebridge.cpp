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

// Draw a URL centred, wrapped to fit the panel width so it never bleeds off both
// edges on the narrow boards. Strips the scheme (saves ~8 chars), then greedily
// packs size-1 text (6px/char), preferring to break just after a '/'. Advances y.
static void drawUrl(const char* url, int cx, int W, int& y, int lineH, uint16_t color) {
    if (!strncmp(url, "https://", 8)) url += 8;
    else if (!strncmp(url, "http://", 7)) url += 7;
    const int maxChars = (W - 4) / 6;                 // size-1 glyph ~6px wide
    M5.Display.setTextColor(color, TFT_BLACK);
    int len = (int)strlen(url), pos = 0;
    if (maxChars < 4) { M5.Display.drawString(url, cx, y); y += lineH; return; }
    while (pos < len) {
        int take = len - pos;
        if (take > maxChars) {
            take = maxChars;
            int brk = -1;                             // prefer a break just after a '/'
            for (int i = maxChars - 1; i >= maxChars / 2; --i) if (url[pos + i] == '/') { brk = i + 1; break; }
            if (brk > 0) take = brk;
        }
        char line[48]; int n = take < 47 ? take : 47;
        memcpy(line, url + pos, n); line[n] = 0;
        M5.Display.drawString(line, cx, y); y += lineH;
        pos += take;
    }
}

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
    drawUrl(au, W / 2, W, y, DL, 0x5AEB);
    if (!COMPACT) {
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK); M5.Display.drawString("relay:", W / 2, y); y += DS;
        const char* ru = Config::wifi().relayUrl;
        if (ru[0]) drawUrl(ru, W / 2, W, y, DL, 0x5AEB);
        else     { M5.Display.setTextColor(TFT_RED, TFT_BLACK); M5.Display.drawString("(not set)", W / 2, y); }
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
