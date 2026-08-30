// webui.cpp — MANAGEMENT web server (see webui.h).
#include "webui.h"
#include "../core/mode_manager.h"
#include "../core/config.h"
#include "../modes/oink.h"
#include "version.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

namespace WebUI {

static WebServer  server(80);
static DNSServer  dns;
static bool       up = false;

// Minimal SPA shell: shows live device status polled from /api/status. The full
// tabbed UI (captures, sync, config, OTA) lands in M4. Kept tiny + inlined.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BBoink</title><style>
:root{color-scheme:dark}body{margin:0;background:#0b0f14;color:#e6edf3;
font:15px/1.5 system-ui,sans-serif}header{background:#11202e;padding:14px 18px;
border-bottom:2px solid #2dd4bf}h1{margin:0;font-size:20px;color:#2dd4bf}
.wrap{padding:18px;max-width:520px;margin:0 auto}
.card{background:#111820;border:1px solid #22303c;border-radius:10px;padding:14px;margin:12px 0}
.row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #1b2530}
.row:last-child{border:0}.k{color:#8aa0b2}.v{font-weight:600}
.on{color:#34d399}.off{color:#f87171}small{color:#6b7d8f}
</style></head><body>
<header><h1>BBoink</h1></header>
<div class="wrap">
<div class="card" id="status"><div class="row"><span class="k">loading…</span></div></div>
<div class="card"><small>Management console — read-only preview (M3). Captures, sync,
config &amp; firmware update arrive in the next step.</small></div>
</div>
<script>
async function tick(){
 try{const r=await fetch('/api/status');const d=await r.json();
  const sta=d.sta.connected?`<span class="on">${d.sta.ssid} (${d.sta.ip})</span>`:'<span class="off">not connected</span>';
  document.getElementById('status').innerHTML=
   `<div class="row"><span class="k">version</span><span class="v">v${d.version}</span></div>`+
   `<div class="row"><span class="k">mode</span><span class="v">${d.mode}</span></div>`+
   `<div class="row"><span class="k">SoftAP</span><span class="v">${d.ap.ssid}</span></div>`+
   `<div class="row"><span class="k">AP clients</span><span class="v">${d.ap.clients}</span></div>`+
   `<div class="row"><span class="k">uplink (STA)</span><span class="v">${sta}</span></div>`+
   `<div class="row"><span class="k">captures</span><span class="v">${d.captures}</span></div>`+
   `<div class="row"><span class="k">free heap</span><span class="v">${(d.heap/1024|0)} KB</span></div>`;
 }catch(e){}
}
tick();setInterval(tick,2000);
</script></body></html>)HTML";

static void noKeepAlive() { server.sendHeader("Connection", "close"); }

static void sendStatus() {
    noKeepAlive();
    char buf[512];
    bool sta = (WiFi.status() == WL_CONNECTED);
    String staSsid = sta ? WiFi.SSID() : String();
    String staIp   = sta ? WiFi.localIP().toString() : String("0.0.0.0");
    int captures = OinkMode::getExcludedCount();   // registry entries (persisted captures)
    snprintf(buf, sizeof(buf),
        "{\"version\":\"%s\",\"mode\":\"%s\",\"heap\":%u,"
        "\"ap\":{\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%d},"
        "\"sta\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
        "\"captures\":%d}",
        BBOINK_VERSION, ModeManager::currentName(), (unsigned)ESP.getFreeHeap(),
        ModeManager::apSSID(), WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum(),
        sta ? "true" : "false", staSsid.c_str(), staIp.c_str(),
        captures);
    server.send(200, "application/json", buf);
}

static void sendIndex() {
    noKeepAlive();
    server.send_P(200, "text/html", INDEX_HTML);
}

void begin() {
    if (up) return;
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());          // captive portal: all names -> AP IP

    server.on("/api/status", HTTP_GET, sendStatus);
    server.on("/", HTTP_GET, sendIndex);
    // OS captive-portal probes -> serve the page so it pops automatically.
    server.on("/generate_204", HTTP_GET, sendIndex);      // Android
    server.on("/hotspot-detect.html", HTTP_GET, sendIndex); // iOS/macOS
    server.on("/connecttest.txt", HTTP_GET, sendIndex);   // Windows
    server.onNotFound(sendIndex);                          // anything else -> app
    server.begin();
    up = true;
    Serial.printf("[WEBUI] up on http://%s\n", WiFi.softAPIP().toString().c_str());
}

void stop() {
    if (!up) return;
    server.stop();
    dns.stop();
    up = false;
    Serial.println("[WEBUI] stopped");
}

void loop() {
    if (!up) return;
    dns.processNextRequest();
    server.handleClient();
}

bool running() { return up; }

} // namespace WebUI
