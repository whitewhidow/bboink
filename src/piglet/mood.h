// Stub: Mood — the porkchop piglet personality is removed in tembed-oink.
// All methods are no-ops; signatures mirror the originals (incl. defaults) so the
// copied engine compiles unchanged.
#pragma once

#include <Arduino.h>

class Mood {
public:
    static void onHandshakeCaptured(const char* apName = nullptr) { (void)apName; }
    static void onPMKIDCaptured(const char* apName = nullptr) { (void)apName; }
    static void onNewNetwork(const char* apName = nullptr, int8_t rssi = 0, uint8_t channel = 0) {
        (void)apName; (void)rssi; (void)channel;
    }
    static void setStatusMessage(const char* msg) { (void)msg; }
    static void onSniffing(uint16_t networkCount, uint8_t channel) { (void)networkCount; (void)channel; }
    static void onDeauthing(const char* apName, uint32_t deauthCount) { (void)apName; (void)deauthCount; }
    static void onDeauthSuccess(const uint8_t* clientMac) { (void)clientMac; }
    static void onBored(uint16_t networkCount = 0) { (void)networkCount; }
    static void setDialogueLock(bool locked) { (void)locked; }
};
