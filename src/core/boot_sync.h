// boot_sync.h — reboot-to-sync QUEUE. Management mode (AP+STA+web+DNS) can't muster
// the ~35KB CONTIGUOUS heap block TLS needs, and the ESP32-C5 reliably completes only
// ONE TLS handshake per boot (a 2nd handshake runs at heap fragmented by the 1st and
// fails/aborts). So each sync op = one handshake, run EARLY in boot at clean heap.
// "Sync All" / "Check cracked" queue several ops; we run one per boot and reboot to
// run the next (chained reboots) until the queue drains. State survives the soft reset
// via RTC memory (no flash write); a magic guards against power-on garbage.
#pragma once
#include <Arduino.h>

static const uint32_t BOOT_SYNC_MAGIC = 0xB0075EACu;

// One bit per sync op. Each op is a single TLS handshake at clean heap.
enum SyncOp : uint32_t {
    SYNC_WPA_UP  = 1u << 0,   // wpa-sec   : upload captures
    SYNC_OHC_UP  = 1u << 1,   // OHC       : upload hashes
    SYNC_PWN_UP  = 1u << 2,   // PwnCrack  : upload handshakes
    SYNC_WPA_CHK = 1u << 3,   // wpa-sec   : download potfile (fetch cracked)
    SYNC_PWN_CHK = 1u << 4,   // PwnCrack  : download potfile (fetch cracked)
    SYNC_RELAY   = 1u << 5,   // Relay     : upload all + fetch cracked in ONE connection
    SYNC_RELAY_PING = 1u << 6,// Relay     : GET /healthz (wake + status)
};
static const uint32_t SYNC_ALL_UP  = SYNC_WPA_UP | SYNC_OHC_UP | SYNC_PWN_UP;
static const uint32_t SYNC_ALL_CHK = SYNC_WPA_CHK | SYNC_PWN_CHK;

extern RTC_NOINIT_ATTR uint32_t bootSyncMagic;
extern RTC_NOINIT_ATTR uint32_t bootSyncQueue;      // pending SyncOp bits; one op per boot
extern RTC_NOINIT_ATTR char     bootSyncResult[96]; // accumulated result, shown via /api/status
extern RTC_NOINIT_ATTR uint32_t bootShowMgmt;       // set on request (currently boots to capture; kept for future)
extern RTC_NOINIT_ATTR uint32_t bootBleBridge;      // BOOT_SYNC_MAGIC -> boot straight into BLE_BRIDGE (WiFi off)
extern RTC_NOINIT_ATTR uint32_t bootFwFetch;        // FW_FETCH_* -> on next (WiFi) boot, flash a fw image + boot into it

// Reboot-to-fetch OTA targets (distinct magics guard against power-on RTC garbage).
static const uint32_t FW_FETCH_SELF   = 0xF7C0DE01u;   // update to the latest of THIS firmware
static const uint32_t FW_FETCH_SWITCH = 0xF7C0DE02u;   // flash the sibling firmware (PoC)

// Queue one or more sync ops + reboot to run them (one handshake per boot, chained).
inline void requestSyncQueue(uint32_t mask) {
    if (!mask) return;
    bootSyncMagic     = BOOT_SYNC_MAGIC;
    bootSyncQueue     = mask;
    bootSyncResult[0] = 0;             // fresh accumulation for this run
    bootShowMgmt      = BOOT_SYNC_MAGIC;
    delay(150);
    ESP.restart();
}

// Reboot into BLE bridge mode (WiFi off, NimBLE peripheral). Called from the web UI.
inline void requestBleBridge() {
    bootBleBridge = BOOT_SYNC_MAGIC;
    delay(150);
    ESP.restart();
}

// Reboot-to-fetch OTA: flag a firmware fetch and reboot into a NORMAL (WiFi) boot;
// main's boot hook then downloads the target app bin into the spare OTA slot and
// boots into it. SELF = update to this firmware's latest; SWITCH = the sibling
// (PoC). Shares the clean-heap-at-boot approach used for sync (and mirrors the PoC
// side). WiFi creds must be configured. This gives the single-button boards (no
// on-device menu) a WiFi OTA path they otherwise lack.
inline void requestFwUpdate() { bootFwFetch = FW_FETCH_SELF;   delay(150); ESP.restart(); }
inline void requestFwSwitch() { bootFwFetch = FW_FETCH_SWITCH; delay(150); ESP.restart(); }

// Back-compat shim for single-service upload (1=wpa 2=ohc 3=pwn).
inline void requestBootSync(int svc) {
    requestSyncQueue(svc == 1 ? SYNC_WPA_UP : svc == 2 ? SYNC_OHC_UP : SYNC_PWN_UP);
}
