// OnlineHashCrack API v2 client (https://api.onlinehashcrack.com/v2).
// Submits WPA hashes (the WPA*02*/WPA*01* lines from our .22000 capture files)
// as algo_mode 22000, and lists task status. JSON over HTTPS, sk_ API key.
#pragma once

#include <Arduino.h>

namespace OHC {

struct UploadResult {
    bool     success;
    uint16_t accepted;     // newly queued
    uint16_t skipped;      // already_sent
    uint16_t rejected;     // invalid/quota
    uint16_t totalHashes;  // hashes found locally and submitted
    char     error[48];
};

struct Task {
    char hash[40];     // masked/short hash id
    char status[28];   // e.g. "In queue", "Cracked", ...
};

bool hasApiKey();

// Local "uploaded to OHC" tracking (persisted to SD), mirroring WPA-SEC's
// uploaded list so the manage screen can tag/count submitted captures offline.
// BSSIDs are stored uppercased without colons.
bool loadUploaded();                  // load cache from SD (once)
bool isUploaded(const char* bssid);   // was this BSSID submitted to OHC?
void markUploaded(const char* bssid); // record + persist a submitted BSSID

// Read every .22000 file in the captures dir, collect the WPA hash lines, and
// submit them in batches of 50 to OHC. Requires WiFi connected.
UploadResult uploadHashes();

// Fetch the account's task list. Returns task count (or -1 on error).
int listTasks(Task* out, int maxTasks, char* err, size_t errLen);

} // namespace OHC
