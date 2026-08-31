// relay.h — board-side client for the bboink Render relay.
//
// The board talks to ONE host (the relay). Over a single kept-alive TLS connection
// it uploads all hashes + pcaps and fetches merged cracked results. Because it's one
// host, the whole sync is ONE TLS handshake — no per-service juggling, no chained
// reboots. Keys live on the relay, not the device.
#pragma once
#include <Arduino.h>

namespace Relay {

struct SyncResult {
    bool     ok = false;
    uint16_t hashesUp = 0;    // 22000 lines POSTed
    uint16_t pcapsUp = 0;     // pcap files POSTed OK
    uint16_t cracked = 0;     // cracked entries returned by /v1/cracked
    char     error[64] = {0};
};

bool configured();                 // relayUrl + relayToken both set

// GET /healthz — wakes the (free-tier, sleeps-when-idle) Render service and reports
// whether it's up + the round-trip time. Writes a short status into `status`.
struct PingResult {
    bool     up = false;
    uint32_t ms = 0;
    char     status[64] = {0};   // e.g. "UP 234ms" / "waking… (timeout)" / "DOWN 502"
};
PingResult ping();

// Full relay sync in one connection: POST /v1/hashes, POST /v1/pcap (each), GET
// /v1/cracked (writes results into the wpa-sec cracked cache for the Captures UI).
SyncResult sync();

} // namespace Relay
