// Stub: Display — porkchop's themed display layer is replaced by src/app/ screens.
// oink.cpp only calls setWiFiStatus()/showLoot(); both are no-ops here.
#pragma once

#include <Arduino.h>

class Display {
public:
    static void setWiFiStatus(bool connected) { (void)connected; }
    static void showLoot(const String& ssid) { (void)ssid; }
};
