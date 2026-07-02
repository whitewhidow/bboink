// PwnCrack.org client. Uploads WPA hashes (our .22000 = hashcat hc22000 format)
// as multipart to /upload_handshake, and downloads the cracked potfile from
// /download_potfile_script. Key is a UUID, passed as a form field / query param.
#pragma once

#include <Arduino.h>

namespace PwnCrack {

struct UploadResult {
    bool     success;
    uint16_t hashes;       // WPA* lines found in the file and submitted
    char     error[48];
};

bool hasApiKey();

// Local "uploaded to PwnCrack" tracking (persisted to SD), mirroring OHC/wpa-sec.
// BSSIDs stored uppercased without colons.
bool loadUploaded();
bool isUploaded(const char* bssid);
void markUploaded(const char* bssid);

// Upload a single .22000 capture file (by basename) to PwnCrack. Requires WiFi.
UploadResult uploadFile(const char* basename);

// Download + cache the cracked potfile. Returns cracked count (or -1 on error).
int syncPotfile(char* err, size_t errLen);

// Cracked-result lookups (from the cached potfile; no WiFi needed).
bool loadCache();                        // load cached potfile from SD (once)
bool isCracked(const char* bssid);
const char* getPassword(const char* bssid);   // "" if unknown
uint16_t getCrackedCount();

} // namespace PwnCrack
