// testcap.h — debug helper: write a synthetic handshake .pcap to the captures
// directory so the wpa-sec upload path can be exercised without a real capture.
#pragma once

#include <Arduino.h>

namespace TestCap {
// Generates a radiotap PCAP (beacon + EAPOL M1/M2, random nonces) in the
// handshakes dir. Structurally valid so wpa-sec accepts the upload; not
// crackable. Returns true and fills outPath on success.
bool generate(char* outPath, size_t outLen);
}
