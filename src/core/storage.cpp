// storage.cpp — backend selection for the capture-storage facade.
#include "storage.h"
#include <SD.h>
#include <LittleFS.h>

namespace {
    fs::FS* g_fs   = nullptr;
    bool    g_isSD = false;
}

namespace Storage {

void setBackend(fs::FS* backend, bool isSD) {
    g_fs   = backend;
    g_isSD = isSD;
}

bool available() { return g_fs != nullptr; }
bool usingSD()   { return g_isSD; }

// Never deref null: fall back to the (possibly unmounted) SD object, whose ops
// just fail safely. All real callers gate on available()/isSDAvailable() first.
fs::FS& fs() { return g_fs ? *g_fs : (fs::FS&)SD; }

uint64_t totalBytes() {
    if (!g_fs) return 0;
    return g_isSD ? SD.totalBytes() : LittleFS.totalBytes();
}
uint64_t usedBytes() {
    if (!g_fs) return 0;
    return g_isSD ? SD.usedBytes() : LittleFS.usedBytes();
}

const char* backendName() { return g_fs ? (g_isSD ? "SD" : "LittleFS") : "none"; }

} // namespace Storage
