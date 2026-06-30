// screen_menu.cpp — main menu: Capture / Manage / Options / Reboot / Power Off.
#include "app.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace ScreenMenu {

static const char* kItems[] = { "CAPTURE", "WPA-SEC", "HASHCRACK", "OPTIONS", "REBOOT", "POWER OFF" };
static constexpr int kCount = 6;
static constexpr int VISIBLE = 5;   // rows that fit on the 170px panel at size 2
static int sel = 0;
static int firstVisible = 0;
static bool dirty = true;

void enter() {
    sel = 0;
    firstVisible = 0;
    dirty = true;
}

static void draw() {
    App::clear();
    App::header("BBoink");
    App::drawList(kItems, kCount, sel, firstVisible, VISIBLE);
    App::footer("turn: move   click: open");
}

static void reboot() {
    App::clear();
    App::centerMsg("REBOOTING", TFT_CYAN);
    delay(700);
    ESP.restart();
}

// Power off = deep sleep with ANY-button wake (wheel GPIO0 or side button GPIO6,
// both active-low). SAFE: a wake source IS configured, so a button press boots it
// back up; and the hardware PWR-button power-cycle remains a guaranteed backup.
// (The earlier strand was deep sleep with NO wake source — fixed here.)
static void powerOff() {
    // Confirm so a stray click can't trigger it.
    App::clear();
    App::header("POWER OFF");
    App::centerMsg("click = confirm", TFT_RED);
    App::footer("back = cancel");
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back)  { dirty = true; return; }
        if (porkhal::vkey.enter) break;
        delay(20);
    }

    App::clear();
    App::centerMsg("POWERING OFF", TFT_RED);
    App::footer("press any button to wake");
    delay(1000);
    M5.Display.setBrightness(0);

    // Hold pull-ups on the button pins through deep sleep so idle=high, press=low.
    rtc_gpio_pullup_en(GPIO_NUM_0);  rtc_gpio_pulldown_dis(GPIO_NUM_0);
    rtc_gpio_pullup_en(GPIO_NUM_6);  rtc_gpio_pulldown_dis(GPIO_NUM_6);
    // Wake when EITHER button goes low.
    esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 6), ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();   // boots fresh on the next button press
}

void tick(const App::Input& in) {
    if (in.up)   { sel = (sel + kCount - 1) % kCount; dirty = true; }
    if (in.down) { sel = (sel + 1) % kCount;          dirty = true; }
    // Keep the selection within the visible window (scrolls for >VISIBLE items).
    if (sel < firstVisible)            firstVisible = sel;
    if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;
    if (in.enter) {
        switch (sel) {
            case 0: App::go(App::Screen::CAPTURE); return;
            case 1: App::go(App::Screen::MANAGE);  return;
            case 2: App::go(App::Screen::OHC);     return;
            case 3: App::go(App::Screen::OPTIONS); return;
            case 4: reboot();                      return;
            case 5: powerOff();                    return;
        }
    }
    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenMenu
