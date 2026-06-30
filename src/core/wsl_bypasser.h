// WSL Bypasser - Bypass ESP32 WiFi frame validation
#pragma once

#include <Arduino.h>

namespace WSLBypasser {

// Initialize the bypasser (call early in setup)
void init();

// Randomize MAC address (call before promiscuous mode or scan)
// Sets a locally-administered random MAC to avoid device fingerprinting
void randomizeMAC();

// Send a deauth frame (returns true if sent successfully)
bool sendDeauthFrame(const uint8_t* bssid, uint8_t channel, const uint8_t* staMac, uint8_t reason);

// Send a disassoc frame
bool sendDisassocFrame(const uint8_t* bssid, uint8_t channel, const uint8_t* staMac, uint8_t reason);

// Deauth/raw-TX result counters (to verify frames are actually accepted).
uint32_t txOkCount();
uint32_t txFailCount();
int      lastTxErr();

// Transmit a raw 802.11 frame, spoofing the interface MAC to `srcMac` so the
// WiFi driver's real sanity check accepts it (it requires frame source ==
// interface MAC). This is the override-free way to inject deauth/disassoc while
// leaving normal WiFi STA association intact. The original MAC is restored after.
void rawTx(const uint8_t* srcMac, const uint8_t* frame, size_t len);

}
