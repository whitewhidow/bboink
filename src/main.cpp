// tembed-oink — minimal WiFi handshake capture + wpa-sec upload.
// Target: LilyGo T-Embed CC1101 (ESP32-S3, rotary encoder + side button).
#include <Arduino.h>
#include <M5Cardputer.h>            // shim -> hal/m5compat.h on this board
#include <WiFi.h>
#include "core/config.h"
#if defined(__has_include)
#  if __has_include("core/dev_secrets.h")
#    include "core/dev_secrets.h"
#  endif
#endif
#include "core/network_recon.h"
#include "modes/oink.h"
#include "app/app.h"
#include "version.h"

void setup() {
#if defined(PORK_BOARD_TEMBED_CC1101)
    // BOARD_PWR_EN (GPIO15): power the peripheral rail (display/backlight).
    // T-Embed-specific; the T-Display C5 has no such rail-enable pin (its LCD
    // power-enable, GPIO25, is driven in bringUpHardware()).
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
#endif

    Serial.begin(115200);
    delay(100);
    Serial.println("\n[OINK] tembed-oink boot");

    // CRITICAL ORDER: connect WiFi BEFORE initialising the display.
    // LovyanGFX's display SPI init grabs a shared resource (GDMA channel) that a
    // *fresh* WiFi association also needs — whoever claims it first wins. If the
    // display inits first, every later WiFi connect fails (AUTH_EXPIRE/reason 2).
    // So we bring WiFi up and associate here, keep it alive, then init the display;
    // the wpa-sec sync reuses this live connection.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Config::init();                       // load creds (SD; fine before display)
#if defined(DEV_WIFI_SSID)
    // DEV: a git-ignored core/dev_secrets.h can pin the uplink creds so they never
    // need re-entering during development (overrides stored config each boot).
    if (DEV_WIFI_SSID[0]) {
        strncpy(Config::wifi().otaSSID,     DEV_WIFI_SSID, sizeof(Config::wifi().otaSSID) - 1);
        strncpy(Config::wifi().otaPassword, DEV_WIFI_PASS, sizeof(Config::wifi().otaPassword) - 1);
        Serial.println("[DEV] uplink creds overridden from dev_secrets.h");
    }
#endif
    const char* ssid = Config::wifi().otaSSID;
    if (ssid && ssid[0]) {
#if PORK_LED_COUNT > 0
        neopixelWrite(PORK_LED_PIN, 0, 0, 30);   // blue LED: connecting at boot
#endif
        WiFi.begin(ssid, Config::wifi().otaPassword);
        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) { delay(100); }
#if PORK_LED_COUNT > 0
        neopixelWrite(PORK_LED_PIN, 0, 0, 0);
#endif
        Serial.printf("[OINK] boot wifi: %s\n",
                      WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
        // Best-effort NTP (async) so capture registry timestamps are real.
        if (WiFi.status() == WL_CONNECTED)
            configTime(0, 0, "pool.ntp.org", "time.google.com");
    }

    // Now the display + input + engine (display init no longer disturbs WiFi).
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5.Display.setBrightness(Config::wifi().displayBrightness);

#if !defined(PORK_BOARD_TDISPLAY_C5)
    // The SD shares the SPI bus with the display; retry the mount now that the
    // ST7789 is initialised (it wouldn't mount before the display was up).
    // The T-Display C5 has no SD card (captures live on internal LittleFS).
    Config::mountSdAfterDisplay();
#endif

    NetworkRecon::init();
    OinkMode::init();

    App::clear();
    App::centerMsg("BBoink", TFT_CYAN);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_center);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("v" BBOINK_VERSION, PORK_DISPLAY_W / 2, PORK_DISPLAY_H / 2 + 20);
    delay(900);
    App::begin();
}

void loop() {
    M5Cardputer.update();
    App::tick();
    delay(5);
}
