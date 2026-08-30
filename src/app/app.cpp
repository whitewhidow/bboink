// app.cpp — state machine + plain drawing helpers.
#include "app.h"
#include "../core/config.h"
#include "../core/mode_manager.h"
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <esp_wifi.h>

namespace App {

Screen screen = Screen::MENU;

// Auto-dim the backlight after this long with no input (battery saver).
static constexpr uint32_t kIdleDimMs = 30000;
static constexpr uint8_t  kDimLevel  = 12;
static uint32_t lastInputMs = 0;
static bool     dimmed      = false;

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
    in.toggle= porkhal::vkey.toggle;
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
    if (wifi) { String s = WiFi.SSID(); snprintf(ws, sizeof(ws), "%.8s", s.c_str()); }
    else      { strncpy(ws, "----", sizeof(ws)); }
    M5.Display.setTextColor(wifi ? TFT_GREEN : COL_DIM, TFT_BLACK);
    M5.Display.drawString(ws, PORK_DISPLAY_W - 40, 4);

    // SD status, left of the WiFi/SSID block: green "SD" = card mounted,
    // dim red "sd" = no card (running on internal LittleFS).
    bool sd = Config::isSDCardMounted();
    M5.Display.setTextColor(sd ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.drawString(sd ? "SD" : "sd", PORK_DISPLAY_W - 92, 4);
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

const char* fmtBytes(uint64_t b) {
    static char s[12];
    if (b >= (1ULL << 30))      snprintf(s, sizeof(s), "%.1fG", b / (float)(1ULL << 30));
    else if (b >= (1ULL << 20)) snprintf(s, sizeof(s), "%lluM", (unsigned long long)(b >> 20));
    else                        snprintf(s, sizeof(s), "%lluK", (unsigned long long)(b >> 10));
    return s;
}

void go(Screen s) {
    screen = s;
    switch (s) {
        case Screen::MENU:    ScreenMenu::enter();    break;
        case Screen::CAPTURE: ScreenCapture::enter(); break;
        case Screen::MANAGE:  ScreenManage::enter();  break;
        case Screen::OHC:     ScreenOHC::enter();     break;
        case Screen::PWNCRACK:ScreenPwnCrack::enter();break;
        case Screen::OPTIONS: ScreenOptions::enter(); break;
        case Screen::CONFIGAP:ScreenConfigAP::enter();break;
        case Screen::CONNECT: ScreenConnect::enter(); break;
    }
}

void powerOff() {
    clear();
    centerMsg("SLEEPING", TFT_RED);
    footer("press the button to wake");
    delay(1000);
    M5.Display.setBrightness(0);
#if defined(PORK_BOARD_WAVESHARE_C5_LCD)
    // The one button (BOOT = GPIO28) is a high-power GPIO, NOT an LP/RTC-IO pin,
    // so it cannot wake DEEP sleep on the ESP32-C5 (only GPIO0-7 can). Use LIGHT
    // sleep + GPIO wakeup instead: the same button turns the screen back on, and
    // light sleep keeps RAM so we resume right here and redraw the live screen.
    esp_wifi_set_promiscuous(false);            // quiesce RX before halting the CPU
    M5.Display.sleep();
    while (digitalRead(PORK_ENC_KEY) == LOW) delay(10);   // wait for the long-press release
    delay(60);
    gpio_wakeup_enable((gpio_num_t)PORK_ENC_KEY, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();                     // halts until GPIO28 goes LOW (a tap)
    // --- woke on a button tap ---
    while (digitalRead(PORK_ENC_KEY) == LOW) delay(10);   // wait for the wake tap to release
    delay(60);
    M5.Display.wakeup();
    M5.Display.setBrightness(Config::wifi().displayBrightness);
    go(screen);                                  // re-enter current screen -> redraw (+ re-arm capture)
    // TODO(waveshare): tap-to-wake from light sleep NOT working on-device yet —
    // GPIO wakeup on GPIO28 doesn't resume; investigate (pin hold/pullup during
    // sleep, USB-CDC keeping the CPU up, or display wake). Deferred.
    return;
#elif defined(PORK_BOARD_TDISPLAY_C5)
    // Buttons: GPIO0 (BOOT) + GPIO28. UNVERIFIED: confirm both are RTC/LP-IO wake
    // capable on the C5; if not, fall back to a timer or the hardware PWR button.
    rtc_gpio_pullup_en(GPIO_NUM_0);   rtc_gpio_pulldown_dis(GPIO_NUM_0);
    rtc_gpio_pullup_en(GPIO_NUM_28);  rtc_gpio_pulldown_dis(GPIO_NUM_28);
    esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 28), ESP_EXT1_WAKEUP_ANY_LOW);
#else
    // Hold pull-ups on the button pins so idle=high, press=low wakes it.
    rtc_gpio_pullup_en(GPIO_NUM_0);  rtc_gpio_pulldown_dis(GPIO_NUM_0);
    rtc_gpio_pullup_en(GPIO_NUM_6);  rtc_gpio_pulldown_dis(GPIO_NUM_6);
    esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 6), ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    esp_deep_sleep_start();   // boots fresh on the next button press
}

void begin() {
    lastInputMs = millis();
    ModeManager::begin();   // provisioning-aware: CAPTURE if ready, else MANAGEMENT
}

void tick() {
    // Hold the side button ~3s anywhere -> power off.
    if (porkhal::vkey.backLongPress) { powerOff(); return; }

    // Single-button mode swap (inert on multi-button boards, which reach the
    // same transitions via the menu + capture-screen back).
    if (porkhal::vkey.toggle) { ModeManager::toggle(); return; }

    // Auto-dim backlight after idle; restore on any input.
    if (porkhal::vkey.changed) {
        lastInputMs = millis();
        if (dimmed) { M5.Display.setBrightness(Config::wifi().displayBrightness); dimmed = false; }
    } else if (!dimmed && millis() - lastInputMs > kIdleDimMs) {
        M5.Display.setBrightness(kDimLevel);
        dimmed = true;
    }

    Input in = readInput();
    switch (screen) {
        case Screen::MENU:    ScreenMenu::tick(in);    break;
        case Screen::CAPTURE: ScreenCapture::tick(in); break;
        case Screen::MANAGE:  ScreenManage::tick(in);  break;
        case Screen::OHC:     ScreenOHC::tick(in);     break;
        case Screen::PWNCRACK:ScreenPwnCrack::tick(in);break;
        case Screen::OPTIONS: ScreenOptions::tick(in); break;
        case Screen::CONFIGAP:ScreenConfigAP::tick(in);break;
        case Screen::CONNECT: ScreenConnect::tick(in); break;
    }
}

} // namespace App
