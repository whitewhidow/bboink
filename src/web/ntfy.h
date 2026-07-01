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

} // namespace Ntfy
