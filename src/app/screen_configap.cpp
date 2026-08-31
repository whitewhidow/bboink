// screen_configap.cpp — "Config AP" mode: bring up a SoftAP + a plain HTTP
// server (NOT a captive portal) so a phone/laptop can configure credentials and
// download captured handshakes over the browser at http://192.168.4.1/.
//
// This is the primary data-entry path on the T-Display C5 (touch-only, no
// keyboard), but it is board-agnostic and compiles/works on every board. It is
// mutually exclusive with capture: entering stops the OinkMode engine, and
// exiting tears the AP + server down.
#include "app.h"
#include "../core/config.h"
#include "../core/storage.h"
#include "../core/sd_layout.h"
#include "../core/net_link.h"
#include "../modes/oink.h"
#include "../web/wpasec.h"
#include "../web/cracks.h"
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>

namespace ScreenConfigAP {

static WebServer* server = nullptr;
static bool   active = false;
static char   apSsid[32] = {0};
static char   apPass[16] = {0};   // "" => open network
static char   syncMsg[64] = {0};

// --- HTML helpers ----------------------------------------------------------

static void sendPageHead(String& h, const char* title) {
    h += "<!doctype html><html><head><meta charset='utf-8'>";
    h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h += "<title>";
    h += title;
    h += "</title><style>"
         "body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px}"
         "h1{color:#4dd0e1;font-size:20px}h2{color:#4dd0e1;font-size:16px;margin-top:24px}"
         "a{color:#4dd0e1}"
         ".card{background:#1c1c1c;border:1px solid #333;border-radius:8px;padding:14px;margin:12px 0}"
         "label{display:block;margin:10px 0 4px;font-size:13px;color:#aaa}"
         "input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:8px;"
         "background:#000;border:1px solid #444;border-radius:6px;color:#fff;font-size:15px}"
         "button,input[type=submit]{margin-top:14px;padding:10px 16px;background:#00838f;color:#fff;"
         "border:0;border-radius:6px;font-size:15px;cursor:pointer}"
         "table{width:100%;border-collapse:collapse;font-size:13px}"
         "td,th{text-align:left;padding:6px;border-bottom:1px solid #2a2a2a}"
         ".ok{color:#66bb6a}.no{color:#777}.msg{color:#ffd54f}"
         "</style></head><body>";
}

static void sendPageTail(String& h) { h += "</body></html>"; }

// --- Handlers --------------------------------------------------------------

static void handleRoot() {
    WiFiConfig& w = Config::wifi();
    String h;
    sendPageHead(h, "BBoink Config");
    h += "<h1>BBoink Config</h1>";
    if (syncMsg[0]) { h += "<div class='card msg'>"; h += syncMsg; h += "</div>"; }

    h += "<div class='card'><form method='POST' action='/save'>";
    h += "<h2>WiFi (station / uplink)</h2>";
    h += "<label>SSID</label><input type='text' name='ssid' value='";
    h += w.otaSSID; h += "'>";
    h += "<label>Password (blank = unchanged)</label><input type='password' name='pass' placeholder='(unchanged)'>";

    h += "<h2>Cracking service keys (blank = unchanged)</h2>";
    h += "<label>WPA-SEC key</label><input type='password' name='wpa' placeholder='";
    h += (w.wpaSecKey[0] ? "(set)" : "(empty)"); h += "'>";
    h += "<label>OnlineHashCrack key</label><input type='password' name='ohc' placeholder='";
    h += (w.ohcKey[0] ? "(set)" : "(empty)"); h += "'>";
    h += "<label>PwnCrack key</label><input type='password' name='pwn' placeholder='";
    h += (w.pwncrackKey[0] ? "(set)" : "(empty)"); h += "'>";

    h += "<h2>Notifications</h2>";
    h += "<label>ntfy topic</label><input type='text' name='ntfy' value='";
    h += w.ntfyTopic; h += "'>";

    h += "<br><input type='submit' value='Save'></form></div>";

    h += "<div class='card'><a href='/caps'>&#8594; Captured handshakes</a></div>";
    sendPageTail(h);
    server->send(200, "text/html", h);
}

static void handleSave() {
    WiFiConfig& w = Config::wifi();
    auto setIf = [](const String& v, char* dst, size_t cap) {
        if (v.length()) { strncpy(dst, v.c_str(), cap - 1); dst[cap - 1] = '\0'; }
    };
    // SSID: apply as-is (allows clearing intentionally only via device menu; here
    // we only overwrite when non-empty to avoid an accidental wipe).
    setIf(server->arg("ssid"), w.otaSSID,     sizeof(w.otaSSID));
    setIf(server->arg("pass"), w.otaPassword, sizeof(w.otaPassword));
    setIf(server->arg("wpa"),  w.wpaSecKey,   sizeof(w.wpaSecKey));
    setIf(server->arg("ohc"),  w.ohcKey,      sizeof(w.ohcKey));
    setIf(server->arg("pwn"),  w.pwncrackKey, sizeof(w.pwncrackKey));
    // ntfy topic: apply verbatim (empty = disabled, which is a valid choice).
    { String v = server->arg("ntfy"); strncpy(w.ntfyTopic, v.c_str(), sizeof(w.ntfyTopic) - 1);
      w.ntfyTopic[sizeof(w.ntfyTopic) - 1] = '\0'; }
    Config::save();

    server->sendHeader("Location", "/");
    server->send(303, "text/plain", "saved");
}

static const char* contentTypeFor(const char* name) {
    size_t L = strlen(name);
    if (L > 5 && !strcmp(name + L - 5, ".pcap"))  return "application/vnd.tcpdump.pcap";
    if (L > 6 && !strcmp(name + L - 6, ".22000")) return "text/plain";
    return "application/octet-stream";
}

static void handleCaps() {
    String h;
    sendPageHead(h, "BBoink Captures");
    h += "<h1>Captured handshakes</h1><div class='card'>";
    if (!Storage::available()) {
        h += "<p class='no'>No capture filesystem mounted.</p>";
    } else {
        h += "<table><tr><th>File</th><th>Size</th><th>Cracked</th><th></th></tr>";
        File dir = Storage::fs().open(SDLayout::handshakesDir());
        int n = 0;
        if (dir && dir.isDirectory()) {
            for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
                if (f.isDirectory()) { f.close(); continue; }
                const char* nm = f.name();
                // f.name() may include a path prefix; keep the basename only.
                const char* base = strrchr(nm, '/'); base = base ? base + 1 : nm;
                char bssid[13]; bool cracked = false;
                if (SDLayout::captureBssid(base, bssid)) cracked = Cracks::isCracked(bssid);
                h += "<tr><td>"; h += base; h += "</td><td>";
                h += String((uint32_t)f.size()); h += "</td><td>";
                h += cracked ? "<span class='ok'>yes</span>" : "<span class='no'>-</span>";
                h += "</td><td><a href='/dl?f="; h += base; h += "'>download</a></td></tr>";
                n++;
                f.close();
            }
            dir.close();
        }
        h += "</table>";
        if (n == 0) h += "<p class='no'>No captures yet.</p>";
    }
    h += "</div>";
    h += "<div class='card'><form method='POST' action='/sync'>";
    h += "<p>Upload captures to WPA-SEC (connects to the configured WiFi).</p>";
    h += "<input type='submit' value='Sync to WPA-SEC'></form></div>";
    h += "<div class='card'><a href='/'>&#8592; Back to config</a></div>";
    sendPageTail(h);
    server->send(200, "text/html", h);
}

static void handleDownload() {
    String f = server->arg("f");
    // Reject path traversal — basenames only.
    if (f.length() == 0 || f.indexOf('/') >= 0 || f.indexOf("..") >= 0 || !Storage::available()) {
        server->send(400, "text/plain", "bad request");
        return;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", SDLayout::handshakesDir(), f.c_str());
    File file = Storage::fs().open(path, FILE_READ);
    if (!file) { server->send(404, "text/plain", "not found"); return; }
    server->sendHeader("Content-Disposition", String("attachment; filename=\"") + f + "\"");
    server->streamFile(file, contentTypeFor(f.c_str()));
    file.close();
}

// Sync to WPA-SEC. Brings up STA alongside the AP (AP_STA), reuses the shared
// NetLink helper, runs a full sync, then reports the outcome on the next page.
static void handleSync() {
    syncMsg[0] = '\0';
    if (!WPASec::hasApiKey()) {
        strncpy(syncMsg, "No WPA-SEC key configured.", sizeof(syncMsg) - 1);
    } else if (Config::wifi().otaSSID[0] == '\0') {
        strncpy(syncMsg, "No WiFi SSID configured.", sizeof(syncMsg) - 1);
    } else {
        WiFi.mode(WIFI_AP_STA);   // keep the AP up while we associate as a station
        if (!NetLink::connectConfigured()) {
            strncpy(syncMsg, "WiFi connect failed.", sizeof(syncMsg) - 1);
        } else {
            WPASecSyncResult r = WPASec::syncCaptures(nullptr);
            snprintf(syncMsg, sizeof(syncMsg), "Sync: %u up, %u skip, %u fail, %u cracked",
                     r.uploaded, r.skipped, r.failed, r.cracked);
        }
    }
    server->sendHeader("Location", "/");
    server->send(303, "text/plain", "sync done");
}

// --- Screen lifecycle ------------------------------------------------------

static void drawInfo() {
    App::clear();
    App::header("CONFIG AP");
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 34;
    char line[64];
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Join this WiFi:", 8, y); y += 16;
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "SSID: %s", apSsid); M5.Display.drawString(line, 8, y); y += 14;
    snprintf(line, sizeof(line), "Pass: %s", apPass[0] ? apPass : "(open)"); M5.Display.drawString(line, 8, y); y += 14;
    snprintf(line, sizeof(line), "URL : http://%s/", WiFi.softAPIP().toString().c_str());
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(line, 8, y); y += 18;
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.drawString("Set creds + download captures", 8, y);
    App::footer("back: stop AP & exit");
}

void enter() {
    // Mutually exclusive with capture — stop the engine first.
    OinkMode::stop();

    // Unique-ish SSID from the low 24 bits of the eFuse MAC.
    uint32_t id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    snprintf(apSsid, sizeof(apSsid), "BBoink-%06X", id);
    // Open network for easy access (task: open or WPA2). Keep it open so a
    // phone joins without a keyboard; there is no sensitive service exposed.
    apPass[0] = '\0';

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid);   // open AP at 192.168.4.1
    delay(100);

    if (!server) server = new WebServer(80);
    server->on("/",     HTTP_GET,  handleRoot);
    server->on("/save", HTTP_POST, handleSave);
    server->on("/caps", HTTP_GET,  handleCaps);
    server->on("/dl",   HTTP_GET,  handleDownload);
    server->on("/sync", HTTP_POST, handleSync);
    server->onNotFound([]() { server->sendHeader("Location", "/"); server->send(303, "text/plain", ""); });
    server->begin();
    active = true;
    syncMsg[0] = '\0';

    drawInfo();
}

static void teardown() {
    if (server) { server->stop(); }
    active = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);   // restore station mode for the rest of the firmware
}

void tick(const App::Input& in) {
    if (in.back) { teardown(); App::go(App::Screen::MENU); return; }
    if (active && server) server->handleClient();
}

} // namespace ScreenConfigAP
