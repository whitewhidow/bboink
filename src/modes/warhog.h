// Stub: WarhogMode — wardriving mode removed in tembed-oink.
// oink.cpp calls WarhogMode::markCaptured() as a post-capture notification only.
#pragma once

#include <Arduino.h>

class WarhogMode {
public:
    static void markCaptured(const uint8_t* bssid) { (void)bssid; }
};
