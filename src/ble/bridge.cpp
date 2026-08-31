#include "bridge.h"
#include "../core/storage.h"
#include "../core/sd_layout.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <vector>

namespace BleBridge {

// GATT UUIDs (see docs/DESIGN-ble-bridge.md)
static const char* SVC_UUID = "b0070000-b0b0-4b0a-9c5e-000000000000";
static const char* RX_UUID  = "b0070001-b0b0-4b0a-9c5e-000000000000";  // phone -> board (write)
static const char* TX_UUID  = "b0070002-b0b0-4b0a-9c5e-000000000000";  // board -> phone (notify)

// TX frame types (1-byte prefix)
static const uint8_t T_TEXT = 0x01;   // UTF-8 JSON
static const uint8_t T_BIN  = 0x02;   // raw file bytes

static NimBLEServer*         s_server = nullptr;
static NimBLECharacteristic* s_tx = nullptr;
static bool     s_running = false;
static bool     s_connected = false;
static uint16_t s_mtu = 23;
static uint16_t s_filesSent = 0;
static uint16_t s_crackedIn = 0;
static bool     s_exit = false;

// File-streaming state (pumped from loop())
static bool   s_streaming = false;
static File   s_file;
static size_t s_size = 0, s_sent = 0;
static bool   s_crkStarted = false;   // truncate the cracked cache on the first {"c":"crk"}

static uint16_t chunkSize() { return s_mtu > 23 ? (uint16_t)(s_mtu - 4) : 20; }

static void notifyText(const String& json) {
    if (!s_tx || !s_connected) return;
    std::vector<uint8_t> buf;
    buf.reserve(json.length() + 1);
    buf.push_back(T_TEXT);
    for (size_t i = 0; i < json.length(); i++) buf.push_back((uint8_t)json[i]);
    s_tx->setValue(buf.data(), buf.size());
    s_tx->notify();
}

// Build + send the capture manifest ({"t":"list","files":[{name,size,kind}]}).
static void sendList() {
    JsonDocument doc;
    doc["t"] = "list";
    JsonArray arr = doc["files"].to<JsonArray>();
    File d = Storage::fs().open(SDLayout::handshakesDir());
    if (d && d.isDirectory()) {
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            if (!f.isDirectory()) {
                const char* n = f.name(); const char* sl = strrchr(n, '/'); if (sl) n = sl + 1;
                size_t L = strlen(n);
                const char* kind = nullptr;
                if (L > 6 && strcmp(n + L - 6, ".22000") == 0) kind = "22000";
                else if (L > 5 && strcmp(n + L - 5, ".pcap") == 0) kind = "pcap";
                if (kind) {
                    JsonObject o = arr.add<JsonObject>();
                    o["name"] = n; o["size"] = (uint32_t)f.size(); o["kind"] = kind;
                }
            }
            f.close();
        }
        d.close();
    }
    String out; serializeJson(doc, out);
    notifyText(out);
}

static void startFile(const char* name) {
    char path[176]; snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), name);
    s_file = Storage::fs().open(path, FILE_READ);
    if (!s_file) { notifyText(String("{\"t\":\"err\",\"e\":\"open\"}")); return; }
    s_size = s_file.size(); s_sent = 0;
    const char* sl = strrchr(name, '.'); const char* kind = (sl && !strcmp(sl, ".pcap")) ? "pcap" : "22000";
    JsonDocument doc; doc["t"] = "begin"; doc["name"] = name; doc["size"] = (uint32_t)s_size; doc["kind"] = kind;
    String out; serializeJson(doc, out); notifyText(out);
    s_streaming = true;
}

// Write one cracked entry (bssid:ssid:password) to the wpa-sec cracked cache. The
// relay returns the FULL cracked list every sync, so the first entry of each new
// sequence TRUNCATES the cache (refresh, not append) and resets the session counter;
// {"c":"crkdone"} then re-arms the truncate for the next sync.
static void writeCracked(const char* b, const char* ssid, const char* pass) {
    if (!pass || !pass[0]) return;
    if (!Storage::fs().exists(SDLayout::miscDir())) Storage::fs().mkdir(SDLayout::miscDir());
    if (!s_crkStarted) s_crackedIn = 0;   // new cracked sequence -> fresh count
    File f = Storage::fs().open(SDLayout::wpasecResultsPath(), s_crkStarted ? FILE_APPEND : FILE_WRITE);
    s_crkStarted = true;
    if (f) { f.printf("%s:%s:%s\n", b ? b : "", ssid ? ssid : "", pass); f.close(); s_crackedIn++; }
}

static void handleCommand(const uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;
    const char* c = doc["c"] | "";
    if (!strcmp(c, "list"))        sendList();
    else if (!strcmp(c, "get"))    { const char* n = doc["name"] | ""; if (n[0]) startFile(n); }
    else if (!strcmp(c, "crk"))    { writeCracked(doc["b"] | "", doc["s"] | "", doc["p"] | ""); notifyText("{\"t\":\"ok\"}"); }
    else if (!strcmp(c, "crkdone")){ s_crkStarted = false; notifyText(String("{\"t\":\"ok\",\"n\":") + s_crackedIn + "}"); }
    else if (!strcmp(c, "done"))   { s_exit = true; notifyText("{\"t\":\"bye\"}"); }
}

class RxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        NimBLEAttValue v = c->getValue();
        if (v.length()) handleCommand(v.data(), v.length());
    }
};

class SrvCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override { s_connected = true; }
    void onDisconnect(NimBLEServer* srv, NimBLEConnInfo&, int) override {
        s_connected = false; s_streaming = false; if (s_file) s_file.close();
        NimBLEDevice::startAdvertising();   // allow reconnection
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { s_mtu = mtu; }
};

static RxCB  s_rxcb;
static SrvCB s_srvcb;

void start(const char* advName) {
    if (s_running) return;
    s_filesSent = s_crackedIn = 0; s_connected = s_exit = s_streaming = s_crkStarted = false; s_mtu = 23;

    NimBLEDevice::init(advName);
    NimBLEDevice::setMTU(247);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_srvcb);
    NimBLEService* svc = s_server->createService(SVC_UUID);
    s_tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic* rx = svc->createCharacteristic(
        RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(&s_rxcb);
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SVC_UUID);
    adv->setName(advName);
    adv->enableScanResponse(true);
    NimBLEDevice::startAdvertising();
    s_running = true;
}

void stop() {
    if (!s_running) return;
    if (s_file) s_file.close();
    s_streaming = false;
    NimBLEDevice::deinit(true);
    s_server = nullptr; s_tx = nullptr; s_running = false; s_connected = false;
}

void loop() {
    if (!s_running || !s_streaming || !s_connected) return;
    // Send one chunk per call (paced by the main loop) so we don't flood NimBLE.
    uint16_t cs = chunkSize();
    static uint8_t buf[256];
    if (cs > sizeof(buf) - 1) cs = sizeof(buf) - 1;
    buf[0] = T_BIN;
    size_t toRead = s_size - s_sent; if (toRead > cs) toRead = cs;
    size_t rd = s_file.read(buf + 1, toRead);
    if (rd > 0) {
        s_tx->setValue(buf, rd + 1);
        s_tx->notify();
        s_sent += rd;
    }
    if (s_sent >= s_size || rd == 0) {
        s_file.close();
        s_streaming = false;
        notifyText("{\"t\":\"end\"}");
        s_filesSent++;
    }
}

bool     running()       { return s_running; }
bool     connected()     { return s_connected; }
uint16_t filesSent()     { return s_filesSent; }
uint16_t crackedIn()     { return s_crackedIn; }
bool     exitRequested() { return s_exit; }

} // namespace BleBridge
