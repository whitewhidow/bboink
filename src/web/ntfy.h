// ntfy.sh push-notification client. Sends a capture alert to the configured
// topic; optionally PUTs the capture file itself as an attachment.
#pragma once

#include <Arduino.h>

namespace Ntfy {

// True if a topic is configured (empty topic => never send).
bool enabled();

// Send a capture alert to the configured ntfy topic. If ntfyAttachFile is on and
// filePath is a readable file, PUT it as an attachment; otherwise POST a text-only
// alert. `newCount` is how many captures happened this session. Returns true on 2xx.
// Requires the STA WiFi link to be up (not during promiscuous capture).
bool sendCapture(const char* ssid, const char* filePath, uint16_t newCount);

// Per-network alert: find this BSSID's capture files and, when ntfyAttachFile is
// on, PUT BOTH the .pcap and the .22000 (one ntfy message each, since ntfy allows
// one attachment per message); otherwise a single text alert. `bssidHex` is the
// 12-char uppercase BSSID. Returns true if any message was accepted.
bool sendCaptureFor(const char* ssid, const char* bssidHex);

} // namespace Ntfy
