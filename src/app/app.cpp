// app.cpp — state machine + plain drawing helpers.
#include "app.h"
#include <WiFi.h>

namespace App {

Screen screen = Screen::MENU;

static constexpr int HEADER_H = 26;
static constexpr int FOOTER_H = 18;
static constexpr uint16_t COL_ACCENT = TFT_CYAN;
static constexpr uint16_t COL_TEXT   = TFT_WHITE;
static constexpr uint16_t COL_DIM    = TFT_DARKGREY;
static constexpr uint16_t COL_SELBG  = TFT_CYAN;
static constexpr uint16_t COL_SELTX  = TFT_BLACK;

Input readInput() {
    Input in;
    in.up    = porkhal::vkey.up;
    in.down  = porkhal::vkey.down;
    in.enter = porkhal::vkey.enter;
    in.back  = porkhal::vkey.back;
    return in;
}

void clear(uint16_t bg) {
    M5.Display.fillScreen(bg);
}

void header(const char* title) {
    M5.Display.fillRect(0, 0, PORK_DISPLAY_W, HEADER_H, TFT_BLACK);
    M5.Display.fillRect(0, HEADER_H - 2, PORK_DISPLAY_W, 2, COL_ACCENT);
    M5.Display.setTextColor(COL_ACCENT, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(title, 6, 4);

    // Right side: battery % (colour by level) + WiFi indicator.
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_right);
    int bat = M5.Power.getBatteryLevel();
    if (bat < 0) bat = 0; if (bat > 100) bat = 100;
    uint16_t bc = bat <= 20 ? TFT_RED : (bat <= 50 ? TFT_YELLOW : TFT_GREEN);
    char bs[8];
    snprintf(bs, sizeof(bs), "%d%%", bat);
    M5.Display.setTextColor(bc, TFT_BLACK);
    M5.Display.drawString(bs, PORK_DISPLAY_W - 6, 4);
    // Connected SSID (green) or "----" when offline, just left of the battery.
    bool wifi = (WiFi.status() == WL_CONNECTED);
    char ws[16];
    if (wifi) { String s = WiFi.SSID(); snprintf(ws, sizeof(ws), "%.12s", s.c_str()); }
    else      { strncpy(ws, "----", sizeof(ws)); }
    M5.Display.setTextColor(wifi ? TFT_GREEN : COL_DIM, TFT_BLACK);
    M5.Display.drawString(ws, PORK_DISPLAY_W - 40, 4);
}

void footer(const char* hint) {
    int y = PORK_DISPLAY_H - FOOTER_H;
    M5.Display.fillRect(0, y, PORK_DISPLAY_W, FOOTER_H, TFT_BLACK);
    M5.Display.setTextColor(COL_DIM, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(hint, 6, y + 5);
}

void drawList(const char* const* rows, int count, int selected,
              int firstVisible, int visibleRows, int textSize) {
    const int rowH = (textSize <= 1) ? 14 : 22;
    const int x0 = 0;
    const int y0 = HEADER_H + 2;
    M5.Display.setTextSize(textSize);
    M5.Display.setTextDatum(middle_left);
    for (int i = 0; i < visibleRows; i++) {
        int idx = firstVisible + i;
        int y = y0 + i * rowH;
        if (idx >= count) {
            M5.Display.fillRect(x0, y, PORK_DISPLAY_W, rowH, TFT_BLACK);
            continue;
        }
        bool sel = (idx == selected);
        uint16_t bg = sel ? COL_SELBG : TFT_BLACK;
        uint16_t fg = sel ? COL_SELTX : COL_TEXT;
        M5.Display.fillRect(x0, y, PORK_DISPLAY_W, rowH, bg);
        M5.Display.setTextColor(fg, bg);
        M5.Display.drawString(rows[idx], x0 + 8, y + rowH / 2);
    }
    // Scroll arrows
    M5.Display.setTextSize(1);
    if (firstVisible > 0) {
        M5.Display.setTextColor(COL_ACCENT, TFT_BLACK);
        M5.Display.setTextDatum(top_right);
        M5.Display.drawString("^", PORK_DISPLAY_W - 4, y0 + 1);
    }
    if (firstVisible + visibleRows < count) {
        M5.Display.setTextColor(COL_ACCENT, TFT_BLACK);
        M5.Display.setTextDatum(bottom_right);
        M5.Display.drawString("v", PORK_DISPLAY_W - 4, PORK_DISPLAY_H - FOOTER_H - 2);
    }
}

void centerMsg(const char* msg, uint16_t color) {
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(msg, PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2);
}

void go(Screen s) {
    screen = s;
    switch (s) {
        case Screen::MENU:    ScreenMenu::enter();    break;
        case Screen::CAPTURE: ScreenCapture::enter(); break;
        case Screen::MANAGE:  ScreenManage::enter();  break;
        case Screen::OHC:     ScreenOHC::enter();     break;
        case Screen::OPTIONS: ScreenOptions::enter(); break;
    }
}

void begin() {
    go(Screen::MENU);
}

void tick() {
    Input in = readInput();
    switch (screen) {
        case Screen::MENU:    ScreenMenu::tick(in);    break;
        case Screen::CAPTURE: ScreenCapture::tick(in); break;
        case Screen::MANAGE:  ScreenManage::tick(in);  break;
        case Screen::OHC:     ScreenOHC::tick(in);     break;
        case Screen::OPTIONS: ScreenOptions::tick(in); break;
    }
}

} // namespace App
