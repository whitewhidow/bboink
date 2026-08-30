// screen_connect.cpp — MANAGEMENT connect screen.
//
// Shown whenever the device is in MANAGEMENT mode. The radio is up as AP+STA
// (ModeManager): the SoftAP hosts this screen's join info (and, later, the web
// UI), while the STA reaches the configured network for cracking sync.
//
// Left: a WiFi-join QR for the SoftAP (phones parse WIFI:S:..;T:WPA;P:..;; and
// offer to join). Right: SSID / password / AP IP + the live STA status line.
//
// Inputs: on multi-button boards, click = open the legacy on-device menu (until
// the web UI lands), back = return to CAPTURE. On the single-button Waveshare
// the global tap = ModeManager::toggle() returns to CAPTURE; there is no menu.
#include "app.h"
#include "../core/mode_manager.h"
#include "../core/config.h"
#include "../web/webui.h"
#include <WiFi.h>

namespace ScreenConnect {

static bool     framed  = false;
static uint32_t lastSta = 0;

// Escape \ ; , : " per the WIFI: QR spec.
static void qrEscape(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o < cap - 2; i++) {
        char c = in[i];
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out[o++] = '\\';
        out[o++] = c;
    }
    out[o] = '\0';
}

static void drawStatus() {
    // Clear the STA line region and redraw it (right column, below the static text).
    const int x = 150, y = PORK_DISPLAY_H - 34;
    M5.Display.fillRect(x, y, PORK_DISPLAY_W - x, 16, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_left);
    char line[40];
    if (WiFi.status() == WL_CONNECTED) {
        String s = WiFi.SSID();
        snprintf(line, sizeof(line), "wifi: %.14s", s.c_str());
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    } else {
        const char* cfg = Config::wifi().otaSSID;
        snprintf(line, sizeof(line), "wifi: %s", (cfg && cfg[0]) ? "connecting" : "not set");
        M5.Display.setTextColor((cfg && cfg[0]) ? TFT_YELLOW : TFT_DARKGREY, TFT_BLACK);
    }
    M5.Display.drawString(line, x, y);
}

static void drawFrame() {
    App::clear();
    App::header("MANAGEMENT");
#if defined(PORK_BOARD_WAVESHARE_C5_LCD)
    App::footer("click: back to capture");
#else
    App::footer("click: menu   back: capture");
#endif

    const char* ssid = ModeManager::apSSID();
    const char* pass = ModeManager::apPassword();

    // Left: SoftAP join QR (WIFI:S:<ssid>;T:WPA;P:<pass>;;).
    char es[40], ep[40], payload[128];
    qrEscape(ssid, es, sizeof(es));
    qrEscape(pass, ep, sizeof(ep));
    snprintf(payload, sizeof(payload), "WIFI:S:%s;T:WPA;P:%s;;", es, ep);
    const int sz = 118, qx = 8, qy = 34;
    M5.Display.fillRect(qx - 4, qy - 4, sz + 8, sz + 8, TFT_WHITE);   // quiet zone
    M5.Display.qrcode(payload, qx, qy, sz, 1, true);

    // Right: join details.
    int x = 150, y = 36; char line[48];
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("join this AP:", x, y); y += 16;
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "SSID:"); M5.Display.drawString(line, x, y); y += 12;
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(ssid, x + 4, y); y += 16;
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("PASS:", x, y); y += 12;
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(pass, x + 4, y); y += 16;
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("http://192.168.4.1", x, y);

    framed = true;
    drawStatus();
    lastSta = millis();
}

void enter() {
    framed = false;
    lastSta = 0;
}

void tick(const App::Input& in) {
#if !defined(PORK_BOARD_WAVESHARE_C5_LCD)
    // Multi-button boards: reach the legacy menu / return to capture on-device.
    if (in.enter) { App::go(App::Screen::MENU); return; }
    if (in.back)  { ModeManager::enterCapture(); return; }
#endif
    // (Single-button Waveshare: the global tap = ModeManager::toggle() in App::tick
    //  returns to CAPTURE; this screen just displays + refreshes STA status.)

    WebUI::loop();   // pump captive-DNS + HTTP while the connect screen is up
    WebUI::servicePendingSync();   // runs a queued upload (frees heap by dropping the AP)

    if (!framed) drawFrame();
    if (millis() - lastSta >= 1000) { drawStatus(); lastSta = millis(); }
}

} // namespace ScreenConnect
