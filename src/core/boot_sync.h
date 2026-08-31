// boot_sync.h — reboot-to-sync. Management mode (AP+STA+web+DNS) can't muster the
// ~35KB CONTIGUOUS heap block TLS needs (heap is fragmented ~17KB even at 47KB free).
// The web queues a sync here, we reboot, and run it EARLY in boot when the heap is
// clean/unfragmented and only the STA is up. State survives the soft reset via RTC
// memory (no flash write); a magic guards against power-on garbage.
#pragma once
#include <Arduino.h>

static const uint32_t BOOT_SYNC_MAGIC = 0xB0075EACu;
extern RTC_NOINIT_ATTR uint32_t bootSyncMagic;
extern RTC_NOINIT_ATTR int      bootSyncReq;        // 1=wpasec 2=ohc 3=pwncrack
extern RTC_NOINIT_ATTR char     bootSyncResult[96]; // shown later via /api/status
extern RTC_NOINIT_ATTR uint32_t bootShowMgmt;       // set on request; survives a sync crash -> boot into MANAGEMENT

// Queue a sync + reboot into it (called from the web handler).
inline void requestBootSync(int svc) {
    bootSyncMagic = BOOT_SYNC_MAGIC;
    bootSyncReq   = svc;
    bootShowMgmt  = BOOT_SYNC_MAGIC;   // land in MANAGEMENT next boot even if the sync crashes
    delay(150);
    ESP.restart();
}
