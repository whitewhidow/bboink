// storage.h — capture-storage backend facade (hybrid SD or internal LittleFS).
//
// All capture/wpa-sec/config file I/O goes through Storage::fs() instead of the
// SD object directly. Config::init() probes the SD card first and, if no card is
// present, falls back to the internal LittleFS partition — so the firmware works
// with OR without an SD card. Personality settings still live on SPIFFS.
#pragma once

#include <FS.h>

namespace Storage {

// Set by Config::init() once a backend is mounted (SD preferred, else LittleFS).
void setBackend(fs::FS* backend, bool isSD);

bool available();          // true if any writable filesystem is mounted
bool usingSD();            // true = SD card, false = internal LittleFS
fs::FS& fs();              // the active filesystem (SD or LittleFS)

uint64_t totalBytes();     // dispatches to the concrete backend
uint64_t usedBytes();

const char* backendName(); // "SD" | "LittleFS" | "none"

} // namespace Storage
