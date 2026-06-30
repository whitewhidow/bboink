// SD card layout + migration implementation

#include "sd_layout.h"
#include <SD.h>
#include "storage.h"
#include <time.h>
#include <vector>
#include <string.h>

namespace {
static constexpr const char* kNewRoot = "/m5porkchop";
static constexpr const char* kMarker = "/m5porkchop/meta/.migrated_v1";

static constexpr const char* kLegacyHandshakes = "/handshakes";
static constexpr const char* kLegacyWardriving = "/wardriving";
static constexpr const char* kLegacyModels = "/models";
static constexpr const char* kLegacyLogs = "/logs";
static constexpr const char* kLegacyCrash = "/crash";
static constexpr const char* kLegacyScreenshots = "/screenshots";

static constexpr const char* kNewHandshakes = "/m5porkchop/handshakes";
static constexpr const char* kNewWardriving = "/m5porkchop/wardriving";
static constexpr const char* kNewModels = "/m5porkchop/models";
static constexpr const char* kNewLogs = "/m5porkchop/logs";
static constexpr const char* kNewCrash = "/m5porkchop/crash";
static constexpr const char* kNewScreenshots = "/m5porkchop/screenshots";
static constexpr const char* kNewDiagnostics = "/m5porkchop/diagnostics";
static constexpr const char* kNewWpaSec = "/m5porkchop/wpa-sec";
static constexpr const char* kNewWigle = "/m5porkchop/wigle";
static constexpr const char* kNewXp = "/m5porkchop/xp";
static constexpr const char* kNewMisc = "/m5porkchop/misc";
static constexpr const char* kNewConfig = "/m5porkchop/config";
static constexpr const char* kNewMeta = "/m5porkchop/meta";

static constexpr const char* kLegacyConfig = "/porkchop.conf";
static constexpr const char* kLegacyPersonality = "/personality.json";
static constexpr const char* kLegacyWpasecResults = "/wpasec_results.txt";
static constexpr const char* kLegacyWpasecUploaded = "/wpasec_uploaded.txt";
static constexpr const char* kLegacyWpasecSent = "/wpasec_sent.txt";
static constexpr const char* kLegacyWigleUploaded = "/wigle_uploaded.txt";
static constexpr const char* kLegacyWigleStats = "/wigle_stats.json";
static constexpr const char* kLegacyXpBackup = "/xp_backup.bin";
static constexpr const char* kLegacyXpAwardedWpa = "/xp_awarded_wpa.txt";
static constexpr const char* kLegacyXpAwardedWigle = "/xp_awarded_wigle.txt";
static constexpr const char* kLegacyBoarBros = "/boar_bros.txt";
static constexpr const char* kLegacyHeapLog = "/heap_log.txt";
static constexpr const char* kLegacyHeapWatermarks = "/heap_wm.bin";
static constexpr const char* kLegacyWpasecKey = "/wpasec_key.txt";
static constexpr const char* kLegacyWigleKey = "/wigle_key.txt";
static constexpr const char* kLegacyConfigBin = "/porkchop.dat";

static constexpr const char* kNewConfigPath = "/m5porkchop/config/porkchop.conf";
static constexpr const char* kNewPersonalityPath = "/m5porkchop/config/personality.json";
static constexpr const char* kNewWpasecResults = "/m5porkchop/wpa-sec/wpasec_results.txt";
static constexpr const char* kNewWpasecUploaded = "/m5porkchop/wpa-sec/wpasec_uploaded.txt";
static constexpr const char* kNewWpasecSent = "/m5porkchop/wpa-sec/wpasec_sent.txt";
static constexpr const char* kNewWigleUploaded = "/m5porkchop/wigle/wigle_uploaded.txt";
static constexpr const char* kNewWigleStats = "/m5porkchop/wigle/wigle_stats.json";
static constexpr const char* kNewXpBackup = "/m5porkchop/xp/xp_backup.bin";
static constexpr const char* kNewXpAwardedWpa = "/m5porkchop/xp/xp_awarded_wpa.txt";
static constexpr const char* kNewXpAwardedWigle = "/m5porkchop/xp/xp_awarded_wigle.txt";
static constexpr const char* kNewBoarBros = "/m5porkchop/misc/boar_bros.txt";
static constexpr const char* kNewHeapLog = "/m5porkchop/diagnostics/heap_log.txt";
static constexpr const char* kNewHeapWatermarks = "/m5porkchop/diagnostics/heap_wm.bin";
static constexpr const char* kNewWpasecKey = "/m5porkchop/wpa-sec/wpasec_key.txt";
static constexpr const char* kNewWigleKey = "/m5porkchop/wigle/wigle_key.txt";
static constexpr const char* kNewConfigBin = "/m5porkchop/config/porkchop.dat";

// Use mutex to protect shared state
static portMUX_TYPE layoutMutex = portMUX_INITIALIZER_UNLOCKED;
static bool g_useNewLayout = false;

struct MoveOp {
    String from;
    String to;
};

static const char* basenameFromPath(const char* path) {
    if (!path) return "";
    const char* last = strrchr(path, '/');
    if (!last) return path;
    if (*(last + 1) == '\0') return path;
    return last + 1;
}

static bool isDirEmpty(const char* path) {
    File dir = Storage::fs().open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return true;
    }
    File entry = dir.openNextFile();
    while (entry) {
        entry.close();
        dir.close();
        return false;
    }
    dir.close();
    return true;
}

static bool ensureDir(const char* path) {
    if (Storage::fs().exists(path)) return true;
    return Storage::fs().mkdir(path);
}

static const int MAX_RECURSE_DEPTH = 8;

static uint64_t calcPathSize(const char* path, int depth = 0) {
    if (depth > MAX_RECURSE_DEPTH) return 0;

    File f = Storage::fs().open(path);
    if (!f) return 0;

    if (!f.isDirectory()) {
        uint64_t size = f.size();
        f.close();
        return size;
    }

    uint64_t total = 0;
    File entry = f.openNextFile();
    int fileCount = 0;

    while (entry) {
        const char* name = basenameFromPath(entry.name());
        char child[256];
        size_t pathLen = strlen(path);
        bool needsSlash = (pathLen > 0 && path[pathLen - 1] != '/');
        snprintf(child, sizeof(child), "%s%s%s", path, needsSlash ? "/" : "", name);
        entry.close();
        total += calcPathSize(child, depth + 1);
        entry = f.openNextFile();
        fileCount++;

        if (fileCount % 10 == 0) {
            yield();
        }
    }
    f.close();
    return total;
}

static bool copyFile(const char* src, const char* dst) {
    File in = Storage::fs().open(src, FILE_READ);
    if (!in) return false;

    File out = Storage::fs().open(dst, FILE_WRITE);
    if (!out) {
        in.close();
        return false;
    }

    // Use a reasonable buffer size for constrained devices
    static uint8_t buf[2048]; // Reduced from 4096 to save memory
    size_t bytesRead = 0;
    const size_t maxBytes = 10 * 1024 * 1024; // Limit file copy to 10MB to prevent overflow
    
    while (in.available() && bytesRead < maxBytes) {
        size_t toRead = sizeof(buf);
        if (toRead > in.available()) {
            toRead = in.available();
        }
        size_t readBytes = in.read(buf, toRead);
        if (readBytes == 0) break;
        if (out.write(buf, readBytes) != readBytes) {
            out.close();
            in.close();
            return false;
        }
        bytesRead += readBytes;
        // Yield periodically during file copy to prevent WDT resets
        if (bytesRead % (1024 * 4) == 0) { // Yield every 4KB
            yield();
        }
    }

    out.close();
    in.close();
    return true;
}

static bool copyPathRecursive(const char* src, const char* dst, int depth = 0) {
    if (depth > MAX_RECURSE_DEPTH) return true; // silently skip deep trees

    File f = Storage::fs().open(src);
    if (!f) return false;
    bool isDir = f.isDirectory();
    f.close();

    if (!isDir) {
        return copyFile(src, dst);
    }

    if (!Storage::fs().exists(dst) && !Storage::fs().mkdir(dst)) {
        return false;
    }

    File dir = Storage::fs().open(src);
    if (!dir) return false;

    File entry = dir.openNextFile();
    int fileCount = 0;

    while (entry) {
        const char* name = basenameFromPath(entry.name());
        char childSrc[256];
        char childDst[256];
        size_t srcLen = strlen(src);
        size_t dstLen = strlen(dst);
        bool srcSlash = (srcLen > 0 && src[srcLen - 1] != '/');
        bool dstSlash = (dstLen > 0 && dst[dstLen - 1] != '/');
        snprintf(childSrc, sizeof(childSrc), "%s%s%s", src, srcSlash ? "/" : "", name);
        snprintf(childDst, sizeof(childDst), "%s%s%s", dst, dstSlash ? "/" : "", name);
        entry.close();
        if (!copyPathRecursive(childSrc, childDst, depth + 1)) {
            dir.close();
            return false;
        }
        entry = dir.openNextFile();
        fileCount++;
        if (fileCount % 10 == 0) {
            yield();
        }
    }
    dir.close();
    return true;
}

static bool isDiagFile(const char* name) {
    if (!name) return false;
    String nameStr(name);
    return nameStr.startsWith("diag_") && nameStr.endsWith(".txt");
}

static void collectDiagFiles(std::vector<String>& out) {
    File root = Storage::fs().open("/");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File entry = root.openNextFile();
    int fileCount = 0; // Prevent infinite loops on corrupted filesystems
    
    while (entry) {
        const char* name = basenameFromPath(entry.name());
        bool isFile = !entry.isDirectory();
        entry.close();
        if (isFile && isDiagFile(name)) {
            out.push_back(String("/") + String(name));
        }
        entry = root.openNextFile();
        fileCount++;
        // Yield periodically to prevent WDT resets
        if (fileCount % 10 == 0) {
            yield();
        }
    }
    root.close();
}

static bool hasLegacyData() {
    if (Storage::fs().exists(kLegacyHandshakes)) return true;
    if (Storage::fs().exists(kLegacyWardriving)) return true;
    if (Storage::fs().exists(kLegacyModels)) return true;
    if (Storage::fs().exists(kLegacyLogs)) return true;
    if (Storage::fs().exists(kLegacyCrash)) return true;
    if (Storage::fs().exists(kLegacyScreenshots)) return true;
    if (Storage::fs().exists(kLegacyConfig)) return true;
    if (Storage::fs().exists(kLegacyPersonality)) return true;
    if (Storage::fs().exists(kLegacyWpasecResults)) return true;
    if (Storage::fs().exists(kLegacyWpasecUploaded)) return true;
    if (Storage::fs().exists(kLegacyWpasecSent)) return true;
    if (Storage::fs().exists(kLegacyWigleUploaded)) return true;
    if (Storage::fs().exists(kLegacyWigleStats)) return true;
    if (Storage::fs().exists(kLegacyXpBackup)) return true;
    if (Storage::fs().exists(kLegacyXpAwardedWpa)) return true;
    if (Storage::fs().exists(kLegacyXpAwardedWigle)) return true;
    if (Storage::fs().exists(kLegacyBoarBros)) return true;
    if (Storage::fs().exists(kLegacyHeapLog)) return true;
    if (Storage::fs().exists(kLegacyHeapWatermarks)) return true;
    if (Storage::fs().exists(kLegacyWpasecKey)) return true;
    if (Storage::fs().exists(kLegacyWigleKey)) return true;
    if (Storage::fs().exists(kLegacyConfigBin)) return true;

    std::vector<String> diag;
    // Reserve space to reduce allocations
    diag.reserve(10);
    collectDiagFiles(diag);
    bool result = !diag.empty();
    return result;
}

static bool backupLegacy(const char* backupRoot) {
    const char* legacyDirs[] = {
        kLegacyHandshakes,
        kLegacyWardriving,
        kLegacyModels,
        kLegacyLogs,
        kLegacyCrash,
        kLegacyScreenshots
    };
    const int numDirs = sizeof(legacyDirs) / sizeof(legacyDirs[0]);

    const char* legacyFiles[] = {
        kLegacyConfig,
        kLegacyPersonality,
        kLegacyConfigBin,
        kLegacyWpasecResults,
        kLegacyWpasecUploaded,
        kLegacyWpasecSent,
        kLegacyWigleUploaded,
        kLegacyWigleStats,
        kLegacyXpBackup,
        kLegacyXpAwardedWpa,
        kLegacyXpAwardedWigle,
        kLegacyBoarBros,
        kLegacyHeapLog,
        kLegacyHeapWatermarks,
        kLegacyWpasecKey,
        kLegacyWigleKey
    };
    const int numFiles = sizeof(legacyFiles) / sizeof(legacyFiles[0]);

    int failures = 0;

    for (int i = 0; i < numDirs; i++) {
        const char* dir = legacyDirs[i];
        if (!Storage::fs().exists(dir)) continue;
        String dst = String(backupRoot) + String(dir);
        if (!copyPathRecursive(dir, dst.c_str())) {
            Serial.printf("[MIGRATE] Backup failed for dir: %s (continuing)\n", dir);
            failures++;
        }
        yield();
    }

    for (int i = 0; i < numFiles; i++) {
        const char* file = legacyFiles[i];
        if (!Storage::fs().exists(file)) continue;
        String dst = String(backupRoot) + String(file);
        if (!copyFile(file, dst.c_str())) {
            Serial.printf("[MIGRATE] Backup failed for file: %s (continuing)\n", file);
            failures++;
        }
        yield();
    }

    std::vector<String> diag;
    diag.reserve(10);
    collectDiagFiles(diag);
    for (const String& path : diag) {
        String dst = String(backupRoot) + path;
        if (!copyFile(path.c_str(), dst.c_str())) {
            Serial.printf("[MIGRATE] Backup failed for diag: %s (continuing)\n", path.c_str());
            failures++;
        }
        yield();
    }

    if (failures > 0) {
        Serial.printf("[MIGRATE] Backup completed with %d failures (non-fatal)\n", failures);
    }
    return true;  // Best-effort backup — don't block migration over copy failures
}

static bool movePath(const char* src, const char* dst, std::vector<MoveOp>& moved) {
    if (!Storage::fs().exists(src)) return true;  // Source gone = already moved or never existed

    if (Storage::fs().exists(dst)) {
        // Destination already exists — prior partial migration likely moved it.
        // Backup was already created, safe to skip this move.
        Serial.printf("[MIGRATE] Dest exists, skipping: %s (src still at %s)\n", dst, src);
        return true;
    }

    if (Storage::fs().rename(src, dst)) {
        if (moved.size() < 100) {
            moved.push_back({String(src), String(dst)});
        }
        return true;
    }

    // Rename failed — FatFs cross-directory rename can be flaky.
    // Fallback: copy + delete for files. Directories use copyPathRecursive.
    Serial.printf("[MIGRATE] Rename failed, trying copy fallback: %s -> %s\n", src, dst);

    File probe = Storage::fs().open(src);
    if (!probe) return false;
    bool isDir = probe.isDirectory();
    probe.close();

    bool ok = isDir ? copyPathRecursive(src, dst) : copyFile(src, dst);
    if (!ok) {
        Serial.printf("[MIGRATE] Copy fallback also failed: %s -> %s\n", src, dst);
        return false;
    }

    // Copy succeeded — remove source
    if (!isDir) {
        Storage::fs().remove(src);
    }
    // For directories, leave source in place (recursive delete is expensive
    // and risky mid-migration). The backup already preserves the data.

    if (moved.size() < 100) {
        moved.push_back({String(src), String(dst)});
    }
    return true;
}

static void rollbackMoves(const std::vector<MoveOp>& moved) {
    for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
        Storage::fs().rename(it->to.c_str(), it->from.c_str());
    }
}

} // namespace

namespace SDLayout {

bool usingNewLayout() {
    bool result;
    portENTER_CRITICAL(&layoutMutex);
    result = g_useNewLayout;
    portEXIT_CRITICAL(&layoutMutex);
    return result;
}

void setUseNewLayout(bool enable) {
    portENTER_CRITICAL(&layoutMutex);
    g_useNewLayout = enable;
    portEXIT_CRITICAL(&layoutMutex);
}

const char* newRoot() { return kNewRoot; }
const char* migrationMarkerPath() { return kMarker; }

const char* handshakesDir() { return usingNewLayout() ? kNewHandshakes : kLegacyHandshakes; }
const char* wardrivingDir() { return usingNewLayout() ? kNewWardriving : kLegacyWardriving; }
const char* modelsDir() { return usingNewLayout() ? kNewModels : kLegacyModels; }
const char* logsDir() { return usingNewLayout() ? kNewLogs : kLegacyLogs; }
const char* crashDir() { return usingNewLayout() ? kNewCrash : kLegacyCrash; }
const char* screenshotsDir() { return usingNewLayout() ? kNewScreenshots : kLegacyScreenshots; }
const char* diagnosticsDir() { return usingNewLayout() ? kNewDiagnostics : "/"; }
const char* wpaSecDir() { return usingNewLayout() ? kNewWpaSec : "/"; }
const char* wigleDir() { return usingNewLayout() ? kNewWigle : "/"; }
const char* xpDir() { return usingNewLayout() ? kNewXp : "/"; }
const char* miscDir() { return usingNewLayout() ? kNewMisc : "/"; }
const char* configDir() { return usingNewLayout() ? kNewConfig : "/"; }
const char* metaDir() { return usingNewLayout() ? kNewMeta : "/"; }

const char* configPathSD() { return usingNewLayout() ? kNewConfigPath : kLegacyConfig; }
const char* personalityPathSD() { return usingNewLayout() ? kNewPersonalityPath : kLegacyPersonality; }
const char* wpasecResultsPath() { return usingNewLayout() ? kNewWpasecResults : kLegacyWpasecResults; }
const char* wpasecUploadedPath() { return usingNewLayout() ? kNewWpasecUploaded : kLegacyWpasecUploaded; }
const char* wpasecSentPath() { return usingNewLayout() ? kNewWpasecSent : kLegacyWpasecSent; }
const char* wigleUploadedPath() { return usingNewLayout() ? kNewWigleUploaded : kLegacyWigleUploaded; }
const char* wigleStatsPath() { return usingNewLayout() ? kNewWigleStats : kLegacyWigleStats; }
const char* xpBackupPath() { return usingNewLayout() ? kNewXpBackup : kLegacyXpBackup; }
const char* xpAwardedWpaPath() { return usingNewLayout() ? kNewXpAwardedWpa : kLegacyXpAwardedWpa; }
const char* xpAwardedWiglePath() { return usingNewLayout() ? kNewXpAwardedWigle : kLegacyXpAwardedWigle; }
const char* boarBrosPath() { return usingNewLayout() ? kNewBoarBros : kLegacyBoarBros; }
const char* heapLogPath() { return usingNewLayout() ? kNewHeapLog : kLegacyHeapLog; }
const char* heapWatermarksPath() { return usingNewLayout() ? kNewHeapWatermarks : kLegacyHeapWatermarks; }
const char* wpasecKeyPath() { return usingNewLayout() ? kNewWpasecKey : kLegacyWpasecKey; }
const char* wigleKeyPath() { return usingNewLayout() ? kNewWigleKey : kLegacyWigleKey; }

const char* legacyConfigPath() { return kLegacyConfig; }
const char* legacyPersonalityPath() { return kLegacyPersonality; }
const char* legacyWpasecKeyPath() { return kLegacyWpasecKey; }
const char* legacyWigleKeyPath() { return kLegacyWigleKey; }

void sanitizeSsid(const char* ssid, char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!ssid || ssid[0] == '\0') {
        strncpy(out, "HIDDEN", outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    size_t j = 0;
    const size_t maxChars = (outLen - 1 < 20) ? outLen - 1 : 20;
    for (size_t i = 0; ssid[i] && j < maxChars; i++) {
        char c = ssid[i];
        if ((unsigned char)c < 0x20) continue;  // skip control chars
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        if (c >= 'a' && c <= 'z') c -= 32;  // uppercase
        out[j++] = c;
    }
    // trim trailing spaces and underscores
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '_')) j--;
    if (j == 0) {
        strncpy(out, "HIDDEN", outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    out[j] = '\0';
}

void buildCaptureFilename(char* out, size_t outLen, const char* dir,
                          const char* ssid, const uint8_t bssid[6],
                          const char* suffix) {
    if (!out || outLen == 0) return;
    char sanitized[21];
    sanitizeSsid(ssid, sanitized, sizeof(sanitized));
    snprintf(out, outLen, "%s/%s_%02X%02X%02X%02X%02X%02X%s",
             dir, sanitized,
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5],
             suffix);
}

bool captureBssid(const char* name, char out[13]) {
    if (!name || !out) return false;
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    const char* dot = strrchr(name, '.');
    size_t baseLen = dot ? (size_t)(dot - name) : strlen(name);

    // Strip the type token that precedes the extension, if present.
    static const char* kToks[] = {"_pcap", "_pmkid", "_22000", "_hs"};
    for (const char* t : kToks) {
        size_t tl = strlen(t);
        if (baseLen > tl && strncmp(name + baseLen - tl, t, tl) == 0) { baseLen -= tl; break; }
    }
    if (baseLen < 12) return false;

    // The BSSID is the last 12 chars of the base (works for "SSID_BSSID" and
    // bare "BSSID"); all 12 must be hex.
    const char* start = name + baseLen - 12;
    for (int i = 0; i < 12; i++) {
        char c = start[i];
        if (!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'))) return false;
    }
    for (int i = 0; i < 12; i++) {
        char c = start[i];
        out[i] = (c >= 'a' && c <= 'f') ? (char)(c - 32) : c;   // uppercase
    }
    out[12] = '\0';
    return true;
}

void ensureDirs() {
    bool useNew = usingNewLayout();
    if (!useNew) {
        if (!Storage::fs().exists(kLegacyHandshakes)) Storage::fs().mkdir(kLegacyHandshakes);
        if (!Storage::fs().exists(kLegacyWardriving)) Storage::fs().mkdir(kLegacyWardriving);
        if (!Storage::fs().exists(kLegacyModels)) Storage::fs().mkdir(kLegacyModels);
        if (!Storage::fs().exists(kLegacyLogs)) Storage::fs().mkdir(kLegacyLogs);
        return;
    }

    ensureDir(kNewRoot);
    ensureDir(kNewHandshakes);
    ensureDir(kNewWardriving);
    ensureDir(kNewModels);
    ensureDir(kNewLogs);
    ensureDir(kNewCrash);
    ensureDir(kNewScreenshots);
    ensureDir(kNewDiagnostics);
    ensureDir(kNewWpaSec);
    ensureDir(kNewWigle);
    ensureDir(kNewXp);
    ensureDir(kNewMisc);
    ensureDir(kNewConfig);
    ensureDir(kNewMeta);
}

bool migrateIfNeeded() {
    if (!Storage::fs().exists("/")) {
        setUseNewLayout(false);
        return false;
    }

    if (Storage::fs().exists(kMarker)) {
        setUseNewLayout(true);
        return true;
    }

    if (Storage::fs().exists(kNewRoot) && !isDirEmpty(kNewRoot)) {
        // /m5porkchop exists with data but no marker. Most likely a previous
        // migration completed but the marker file was lost/corrupted.
        // Check for config dir as evidence of completed migration.
        if (Storage::fs().exists(kNewConfig)) {
            Serial.println("[MIGRATE] /m5porkchop/config exists without marker; re-creating marker");
            ensureDir(kNewMeta);
            File marker = Storage::fs().open(kMarker, FILE_WRITE);
            if (marker) {
                marker.println("v1");
                marker.close();
            }
            setUseNewLayout(true);
            return true;
        }
        Serial.println("[MIGRATE] /m5porkchop exists without marker or config; skipping migration");
        setUseNewLayout(false);
        return false;
    }

    if (!hasLegacyData()) {
        ensureDir(kNewRoot);
        ensureDir(kNewMeta);
        File marker = Storage::fs().open(kMarker, FILE_WRITE);
        if (marker) {
            marker.println("v1");
            marker.close();
        }
        setUseNewLayout(true);
        return true;
    }

    uint64_t totalSize = 0;
    const char* legacyDirs[] = {
        kLegacyHandshakes,
        kLegacyWardriving,
        kLegacyModels,
        kLegacyLogs,
        kLegacyCrash,
        kLegacyScreenshots
    };
    const int numDirs = sizeof(legacyDirs) / sizeof(legacyDirs[0]);
    
    const char* legacyFiles[] = {
        kLegacyConfig,
        kLegacyPersonality,
        kLegacyConfigBin,
        kLegacyWpasecResults,
        kLegacyWpasecUploaded,
        kLegacyWpasecSent,
        kLegacyWigleUploaded,
        kLegacyWigleStats,
        kLegacyXpBackup,
        kLegacyXpAwardedWpa,
        kLegacyXpAwardedWigle,
        kLegacyBoarBros,
        kLegacyHeapLog,
        kLegacyHeapWatermarks,
        kLegacyWpasecKey,
        kLegacyWigleKey
    };
    const int numFiles = sizeof(legacyFiles) / sizeof(legacyFiles[0]);

    for (int i = 0; i < numDirs; i++) {
        const char* dir = legacyDirs[i];
        if (Storage::fs().exists(dir)) {
            totalSize += calcPathSize(dir);
        }
        yield(); // Yield between operations
    }
    for (int i = 0; i < numFiles; i++) {
        const char* file = legacyFiles[i];
        if (Storage::fs().exists(file)) {
            File f = Storage::fs().open(file, FILE_READ);
            if (f) {
                totalSize += f.size();
                f.close();
            }
        }
        yield(); // Yield between operations
    }
    std::vector<String> diag;
    diag.reserve(10);
    collectDiagFiles(diag);
    for (const String& path : diag) {
        File f = Storage::fs().open(path, FILE_READ);
        if (f) {
            totalSize += f.size();
            f.close();
        }
        yield(); // Yield between operations
    }

    uint64_t freeBytes = Storage::totalBytes() - Storage::usedBytes();
    const uint64_t headroom = 64ULL * 1024ULL;
    if (freeBytes < (totalSize + headroom)) {
        Serial.printf("[MIGRATE] Not enough space for backup. Need %llu, free %llu\n",
                      (unsigned long long)(totalSize + headroom),
                      (unsigned long long)freeBytes);
        setUseNewLayout(false);
        return false;
    }

    if (!ensureDir("/backup")) {
        Serial.println("[MIGRATE] Failed to create /backup");
        setUseNewLayout(false);
        return false;
    }

    String backupDir;
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    if (t && t->tm_year >= 120) {
        char buf[64];
        snprintf(buf, sizeof(buf), "/backup/porkchop_%04d%02d%02d_%02d%02d%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        backupDir = buf;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "/backup/porkchop_boot_%lu", (unsigned long)millis());
        backupDir = buf;
    }

    if (!ensureDir(backupDir.c_str())) {
        Serial.println("[MIGRATE] Failed to create backup dir");
        setUseNewLayout(false);
        return false;
    }

    Serial.printf("[MIGRATE] Backup to %s (size %llu)\n", backupDir.c_str(), (unsigned long long)totalSize);
    backupLegacy(backupDir.c_str());

    ensureDir(kNewRoot);
    ensureDir(kNewConfig);
    ensureDir(kNewWpaSec);
    ensureDir(kNewWigle);
    ensureDir(kNewXp);
    ensureDir(kNewMisc);
    ensureDir(kNewDiagnostics);
    ensureDir(kNewMeta);

    std::vector<MoveOp> moved;
    moved.reserve(25); // Pre-allocate expected number of moves to reduce allocations

    if (!movePath(kLegacyHandshakes, kNewHandshakes, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWardriving, kNewWardriving, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyModels, kNewModels, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyLogs, kNewLogs, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyCrash, kNewCrash, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyScreenshots, kNewScreenshots, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }

    if (!movePath(kLegacyConfig, kNewConfigPath, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyConfigBin, kNewConfigBin, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyPersonality, kNewPersonalityPath, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWpasecResults, kNewWpasecResults, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWpasecUploaded, kNewWpasecUploaded, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWpasecSent, kNewWpasecSent, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWigleUploaded, kNewWigleUploaded, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWigleStats, kNewWigleStats, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyXpBackup, kNewXpBackup, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyXpAwardedWpa, kNewXpAwardedWpa, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyXpAwardedWigle, kNewXpAwardedWigle, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyBoarBros, kNewBoarBros, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyHeapLog, kNewHeapLog, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyHeapWatermarks, kNewHeapWatermarks, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWpasecKey, kNewWpasecKey, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    if (!movePath(kLegacyWigleKey, kNewWigleKey, moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }

    std::vector<String> diag2;
    diag2.reserve(10);
    collectDiagFiles(diag2);
    for (const String& path : diag2) {
        String name = path;
        if (name.startsWith("/")) name = name.substring(1);
        String dest = String(kNewDiagnostics) + "/" + name;
        if (!movePath(path.c_str(), dest.c_str(), moved)) { rollbackMoves(moved); setUseNewLayout(false); return false; }
    }

    File marker = Storage::fs().open(kMarker, FILE_WRITE);
    if (marker) {
        marker.println("v1");
        marker.close();
    }

    setUseNewLayout(true);
    return true;
}

} // namespace SDLayout