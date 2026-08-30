// webui.cpp — MANAGEMENT web server (see webui.h).
#include "webui.h"
#include "../app/app.h"
#include "../core/boot_sync.h"
#include "../core/mode_manager.h"
#include "../core/config.h"
#include "../core/net_link.h"
#include "../modes/oink.h"
#include "../core/sd_layout.h"
#include "../core/storage.h"
#include "wpasec.h"
#include "ohc.h"
#include "pwncrack.h"
#include "cracks.h"
#include <FS.h>
#include "version.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

namespace WebUI {

static WebServer  server(80);
static DNSServer  dns;
static bool       up = false;
static int        g_pendingSync = 0;   // 1 = wpa-sec (AP-drop, HTTP)

// Tabbed SPA: Status + Config. Secrets are write-only (GET returns only presence;
// the form sends a secret field only when you type a new value). Inlined PROGMEM.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BBoink</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#0b0f14;color:#e6edf3;
font:15px/1.5 system-ui,sans-serif}header{background:#11202e;padding:12px 18px;border-bottom:2px solid #2dd4bf}
h1{margin:0;font-size:20px;color:#2dd4bf}.wrap{padding:16px;max-width:560px;margin:0 auto}
.tabs{display:flex;gap:8px;margin-bottom:12px}.tab{flex:1;padding:9px;text-align:center;background:#111820;
border:1px solid #22303c;border-radius:8px;cursor:pointer;color:#8aa0b2}.tab.on{background:#16303f;color:#2dd4bf;border-color:#2dd4bf}
.card{background:#111820;border:1px solid #22303c;border-radius:10px;padding:14px;margin:12px 0}
.row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #1b2530}.row:last-child{border:0}
.k{color:#8aa0b2}.v{font-weight:600}.on2{color:#34d399}.off2{color:#f87171}small{color:#6b7d8f}
label{display:block;margin:10px 0 3px;color:#8aa0b2;font-size:13px}
input[type=text],input[type=password],input[type=number]{width:100%;padding:8px;border-radius:7px;
border:1px solid #2a3947;background:#0c141c;color:#e6edf3;font:14px system-ui}
.chk{display:flex;align-items:center;gap:8px;margin:8px 0}.chk input{width:18px;height:18px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
button{margin-top:14px;width:100%;padding:11px;border:0;border-radius:8px;background:#2dd4bf;color:#04211d;
font-weight:700;font-size:15px;cursor:pointer}#msg{text-align:center;margin-top:8px;font-weight:600}
h3{margin:14px 0 2px;color:#2dd4bf;font-size:14px}
.srow{display:flex;align-items:center;gap:10px;margin:12px 0}.srow>span:first-child{flex:1}
.sbtn{width:auto;margin:0;padding:8px 14px}.sres{font-size:13px;min-width:70px;text-align:right;color:#8aa0b2}
.cap{border-bottom:1px solid #1b2530;padding:8px 0}.cap:last-child{border:0}.tag{display:inline-block;font-size:11px;padding:1px 5px;border-radius:4px;margin-right:3px;background:#1b2a36;color:#8aa0b2}.tag.k{background:#134e2e;color:#34d399}.cn{font-weight:600}.cb{color:#6b7d8f;font-size:12px}.del{float:right;background:#3a1720;color:#f87171;border:0;border-radius:5px;padding:3px 8px;cursor:pointer;font-size:12px}.pw{color:#facc15;font-size:13px}
</style></head><body>
<header><h1>BBoink</h1></header>
<div class="wrap">
<div class="tabs"><div class="tab on" id="t_status" onclick="show('status')">Status</div>
<div class="tab" id="t_config" onclick="show('config')">Config</div>
<div class="tab" id="t_sync" onclick="show('sync')">Sync</div>
<div class="tab" id="t_caps" onclick="show('caps')">Captures</div></div>

<div id="status"><div class="card" id="statusCard"><div class="row"><span class="k">loading…</span></div></div></div>

<div id="config" hidden><div class="card">
<h3>Management AP</h3>
<label>SoftAP SSID <small>(blank = BBoink-XXXX)</small></label><input type="text" id="ap_ssid">
<h3>Uplink WiFi (STA)</h3>
<label>SSID</label><input type="text" id="wifi_ssid">
<label>Password <small id="p_wifi_pass"></small></label><input type="password" id="wifi_pass" placeholder="(unchanged)">
<h3>Crack service keys</h3>
<label>wpa-sec key <small id="p_wpa_key"></small></label><input type="password" id="wpa_key" placeholder="(unchanged)">
<label>OnlineHashCrack key <small id="p_ohc_key"></small></label><input type="password" id="ohc_key" placeholder="(unchanged)">
<label>PwnCrack key <small id="p_pwn_key"></small></label><input type="password" id="pwn_key" placeholder="(unchanged)">
<h3>ntfy</h3>
<label>Topic</label><input type="text" id="ntfy_topic">
<div class="chk"><input type="checkbox" id="ntfy_attach"><label style="margin:0">attach capture file</label></div>
<h3>Capture</h3>
<div class="grid">
<div><label>ch hop ms</label><input type="number" id="ch_hop_ms"></div>
<div><label>lock ms</label><input type="number" id="lock_ms"></div>
<div><label>attack RSSI</label><input type="number" id="atk_rssi"></div>
<div><label>max tries</label><input type="number" id="max_tries"></div>
<div><label>burst</label><input type="number" id="burst"></div>
<div><label>jitter ms</label><input type="number" id="jitter"></div>
<div><label>idle retry min</label><input type="number" id="idle_retry_min"></div>
<div><label>brightness</label><input type="number" id="brightness"></div>
</div>
<div class="chk"><input type="checkbox" id="deauth"><label style="margin:0">deauth</label></div>
<div class="chk"><input type="checkbox" id="rnd_mac"><label style="margin:0">randomize MAC</label></div>
<div class="chk"><input type="checkbox" id="cracked_fallback"><label style="margin:0">cracked-AP uplink fallback</label></div>
<div class="chk"><input type="checkbox" id="auto_purge"><label style="margin:0">purge cracked after sync</label></div>
<div class="chk"><input type="checkbox" id="sound"><label style="margin:0">sound</label></div>
<div class="chk"><input type="checkbox" id="pmkid"><label style="margin:0">capture PMKID (off = keep attacking for handshake)</label></div>
<button onclick="save()">Save</button><div id="msg"></div>
</div></div>

<div id="sync" hidden><div class="card">
<h3>Upload captures to crack services</h3>
<small>Uses the STA uplink. May take a moment while it uploads &amp; fetches results.</small>
<div class="srow"><span>wpa-sec</span><button class="sbtn" onclick="doSync('wpasec',this)">Upload</button><span class="sres" id="r_wpasec"></span></div>
<div class="srow"><span>OnlineHashCrack</span><button class="sbtn" onclick="doSync('ohc',this)">Upload</button><span class="sres" id="r_ohc"></span></div>
<div class="srow"><span>PwnCrack</span><button class="sbtn" onclick="doSync('pwncrack',this)">Upload</button><span class="sres" id="r_pwncrack"></span></div>
</div></div>

<div id="caps" hidden><div class="card">
<div style="display:flex;justify-content:space-between;align-items:center">
<h3 style="margin:0">Capture registry</h3><button class="sbtn" style="width:auto;margin:0" onclick="loadCaps()">Refresh</button></div>
<small>C captured · M manual · W/O/P uploaded (wpa-sec/OHC/PwnCrack) · K cracked</small>
<div id="capsList"></div>
</div></div>
</div>
<script>
const NUM=['ch_hop_ms','lock_ms','atk_rssi','max_tries','burst','jitter','idle_retry_min','brightness'];
const BOOL=['ntfy_attach','deauth','rnd_mac','cracked_fallback','auto_purge','sound','pmkid'];
const SEC=['wifi_pass','wpa_key','ohc_key','pwn_key'];
function show(w){for(const x of ['status','config','sync','caps']){document.getElementById(x).hidden=(x!=w);
 document.getElementById('t_'+x).classList.toggle('on',x==w);}if(w=='config')loadCfg();if(w=='caps')loadCaps();}
async function st(){try{const d=await (await fetch('/api/status')).json();
 const sta=d.sta.connected?`<span class="on2">${d.sta.ssid} (${d.sta.ip})</span>`:'<span class="off2">not connected</span>';
 document.getElementById('statusCard').innerHTML=
  `<div class="row"><span class="k">version</span><span class="v">v${d.version}</span></div>`+
  `<div class="row"><span class="k">mode</span><span class="v">${d.mode}</span></div>`+
  `<div class="row"><span class="k">SoftAP</span><span class="v">${d.ap.ssid}</span></div>`+
  `<div class="row"><span class="k">AP clients</span><span class="v">${d.ap.clients}</span></div>`+
  `<div class="row"><span class="k">uplink (STA)</span><span class="v">${sta}</span></div>`+
  `<div class="row"><span class="k">captures</span><span class="v">${d.captures}</span></div>`+
  `<div class="row"><span class="k">free heap</span><span class="v">${(d.heap/1024|0)} KB</span></div>`+
   `<div class="row"><span class="k">last sync</span><span class="v">${d.last_sync||'-'}</span></div>`;
 }catch(e){}}
async function loadCfg(){try{const c=await (await fetch('/api/config')).json();
 document.getElementById('wifi_ssid').value=c.wifi_ssid||'';
 document.getElementById('ntfy_topic').value=c.ntfy_topic||'';
 document.getElementById('ap_ssid').value=c.ap_ssid||'';
 for(const n of NUM)document.getElementById(n).value=c[n];
 for(const b of BOOL)document.getElementById(b).checked=!!c[b];
 const pres={wifi_pass:c.has_wifi_pass,wpa_key:c.has_wpa_key,ohc_key:c.has_ohc_key,pwn_key:c.has_pwn_key};
 for(const s of SEC)document.getElementById('p_'+s).textContent=pres[s]?'(set)':'(unset)';
 }catch(e){}}
async function save(){const b={wifi_ssid:document.getElementById('wifi_ssid').value,
 ntfy_topic:document.getElementById('ntfy_topic').value,
 ap_ssid:document.getElementById('ap_ssid').value};
 for(const n of NUM)b[n]=parseInt(document.getElementById(n).value);
 for(const x of BOOL)b[x]=document.getElementById(x).checked;
 for(const s of SEC){const v=document.getElementById(s).value;if(v)b[s]=v;}
 const m=document.getElementById('msg');m.textContent='saving…';m.style.color='#8aa0b2';
 try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
  if(r.ok){m.textContent='saved ✓';m.style.color='#34d399';for(const s of SEC)document.getElementById(s).value='';loadCfg();}
  else{m.textContent='save failed';m.style.color='#f87171';}}catch(e){m.textContent='save failed';m.style.color='#f87171';}}
async function doSync(svc,btn){const res=document.getElementById('r_'+svc);
 btn.disabled=true;res.textContent='syncing…';res.style.color='#8aa0b2';
 try{const r=await fetch('/api/sync/'+svc,{method:'POST'});
  if(!r.ok){const d=await r.json();res.textContent=d.error||'failed';res.style.color='#f87171';}
  else{const d=await r.json();
   if(d.apdrop){res.textContent='uploading — AP drops ~5s, rejoin then check Status';res.style.color='#facc15';}
   else if(d.rebooting){res.textContent='rebooting to upload — rejoin AP in ~15s, see Status';res.style.color='#facc15';}
   else{res.textContent=`up ${d.uploaded} skip ${d.skipped} crk ${d.cracked}`;res.style.color=d.ok?'#34d399':'#f87171';}
  }
 }catch(e){res.textContent='rebooting/failed — rejoin & check Status';res.style.color='#facc15';}
 btn.disabled=false;}
async function loadCaps(){const el=document.getElementById('capsList');el.innerHTML='<div class="cb">loading…</div>';
 try{const rows=await (await fetch('/api/captures')).json();
  if(!rows.length){el.innerHTML='<div class="cb">no captures yet</div>';return;}
  el.innerHTML=rows.map(r=>{let t='';if(r.captured)t+='<span class="tag">C</span>';if(r.manual)t+='<span class="tag">M</span>';
   if(r.w)t+='<span class="tag">W</span>';if(r.o)t+='<span class="tag">O</span>';if(r.p)t+='<span class="tag">P</span>';
   if(r.k)t+='<span class="tag k">K</span>';
   const pw=r.k&&r.pass?`<div class="pw">pass: ${r.pass}</div>`:'';
   return `<div class="cap"><button class="del" onclick="delCap('${r.bssid}',this)">delete</button>`+
    `<div class="cn">${r.ssid||'(hidden)'}</div><div class="cb">${r.bssid}</div>${t}${pw}</div>`;}).join('');
 }catch(e){el.innerHTML='<div class="cb">failed to load</div>';}}
async function delCap(b,el){if(!confirm('Delete this capture (removes saved files, re-enables attack)?'))return;
 try{const r=await fetch('/api/del_capture',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({bssid:b})});
  if(r.ok){const row=el&&el.closest('.cap'); if(row)row.remove();}
 }catch(e){}}
st();setInterval(()=>{if(!document.getElementById('status').hidden)st();},2000);
</script></body></html>)HTML";

static void noKeepAlive() { server.sendHeader("Connection", "close"); }

static void sendStatus() {
    noKeepAlive();
    char buf[512];
    bool sta = (WiFi.status() == WL_CONNECTED);
    String staSsid = sta ? WiFi.SSID() : String();
    String staIp   = sta ? WiFi.localIP().toString() : String("0.0.0.0");
    snprintf(buf, sizeof(buf),
        "{\"version\":\"%s\",\"mode\":\"%s\",\"heap\":%u,"
        "\"ap\":{\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%d},"
        "\"sta\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
        "\"captures\":%d,\"last_sync\":\"%s\"}",
        BBOINK_VERSION, ModeManager::currentName(), (unsigned)ESP.getFreeHeap(),
        ModeManager::apSSID(), WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum(),
        sta ? "true" : "false", staSsid.c_str(), staIp.c_str(),
        OinkMode::getExcludedCount(), bootSyncResult);
    server.send(200, "application/json", buf);
}

static void sendConfig() {
    noKeepAlive();
    const WiFiConfig& w = Config::wifi();
    JsonDocument doc;
    doc["wifi_ssid"]     = w.otaSSID;
    doc["has_wifi_pass"] = w.otaPassword[0] != 0;
    doc["has_wpa_key"]   = w.wpaSecKey[0] != 0;
    doc["has_ohc_key"]   = w.ohcKey[0] != 0;
    doc["has_pwn_key"]   = w.pwncrackKey[0] != 0;
    doc["ntfy_topic"]    = w.ntfyTopic;
    doc["ntfy_attach"]   = w.ntfyAttachFile;
    doc["ch_hop_ms"]     = w.channelHopInterval;
    doc["lock_ms"]       = w.lockTime;
    doc["atk_rssi"]      = w.attackMinRssi;
    doc["max_tries"]     = w.maxAttackAttempts;
    doc["burst"]         = w.deauthBurstCount;
    doc["jitter"]        = w.deauthJitterMax;
    doc["idle_retry_min"]= w.idleRetryMins;
    doc["brightness"]    = w.displayBrightness;
    doc["deauth"]        = w.enableDeauth;
    doc["rnd_mac"]       = w.randomizeMAC;
    doc["cracked_fallback"] = w.crackedFallback;
    doc["auto_purge"]    = w.autoPurgeCracked;
    doc["sound"]         = w.soundEnabled;
    doc["pmkid"]         = w.pmkidEnabled;
    doc["ap_ssid"]       = w.apSSID;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// POST /api/config — partial update (PATCH semantics). Only keys present are
// applied; secret keys are applied only when non-empty (so omitting/blanking them
// leaves the stored value intact — write-only).
static void saveConfig() {
    noKeepAlive();
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    WiFiConfig w = Config::wifi();
    bool wifiChanged = false;

    auto setStr = [&](const char* key, char* dst, size_t n) {
        if (doc[key].is<const char*>()) { strncpy(dst, doc[key].as<const char*>(), n - 1); dst[n - 1] = 0; }
    };
    auto setSecret = [&](const char* key, char* dst, size_t n) {
        if (doc[key].is<const char*>()) {
            const char* v = doc[key].as<const char*>();
            if (v && v[0]) { strncpy(dst, v, n - 1); dst[n - 1] = 0; }   // non-empty only
        }
    };

    if (doc["wifi_ssid"].is<const char*>()) { setStr("wifi_ssid", w.otaSSID, sizeof(w.otaSSID)); wifiChanged = true; }
    if (doc["wifi_pass"].is<const char*>() && doc["wifi_pass"].as<const char*>()[0]) wifiChanged = true;
    setSecret("wifi_pass", w.otaPassword, sizeof(w.otaPassword));
    setSecret("wpa_key",   w.wpaSecKey,   sizeof(w.wpaSecKey));
    setSecret("ohc_key",   w.ohcKey,      sizeof(w.ohcKey));
    setSecret("pwn_key",   w.pwncrackKey, sizeof(w.pwncrackKey));
    setStr("ntfy_topic", w.ntfyTopic, sizeof(w.ntfyTopic));

    if (doc["ntfy_attach"].is<bool>())     w.ntfyAttachFile   = doc["ntfy_attach"];
    if (doc["ch_hop_ms"].is<int>())        w.channelHopInterval = doc["ch_hop_ms"];
    if (doc["lock_ms"].is<int>())          w.lockTime         = doc["lock_ms"];
    if (doc["atk_rssi"].is<int>())         w.attackMinRssi    = doc["atk_rssi"];
    if (doc["max_tries"].is<int>())        w.maxAttackAttempts= doc["max_tries"];
    if (doc["burst"].is<int>())            w.deauthBurstCount = doc["burst"];
    if (doc["jitter"].is<int>())           w.deauthJitterMax  = doc["jitter"];
    if (doc["idle_retry_min"].is<int>())   w.idleRetryMins    = doc["idle_retry_min"];
    if (doc["brightness"].is<int>())       w.displayBrightness= doc["brightness"];
    if (doc["deauth"].is<bool>())          w.enableDeauth     = doc["deauth"];
    if (doc["rnd_mac"].is<bool>())         w.randomizeMAC     = doc["rnd_mac"];
    if (doc["cracked_fallback"].is<bool>())w.crackedFallback  = doc["cracked_fallback"];
    if (doc["auto_purge"].is<bool>())      w.autoPurgeCracked = doc["auto_purge"];
    if (doc["sound"].is<bool>())           w.soundEnabled     = doc["sound"];
    if (doc["pmkid"].is<bool>())           w.pmkidEnabled     = doc["pmkid"];
    setStr("ap_ssid", w.apSSID, sizeof(w.apSSID));

    Config::setWiFi(w);   // sanitizes + persists

    // If uplink creds changed, kick a non-blocking STA (re)connect so the status
    // line reflects it shortly (don't block the HTTP response on it).
    if (wifiChanged && w.otaSSID[0]) {
        WiFi.begin(w.otaSSID, w.otaPassword);   // non-blocking STA (re)connect
    }

    sendConfig();   // echo the updated config back
}

static bool uplinkReady(const char* keyErr, bool hasKey) {
    if (WiFi.status() != WL_CONNECTED) {
        server.send(400, "application/json", "{\"error\":\"no uplink (set WiFi in Config)\"}");
        return false;
    }
    if (!hasKey) {
        char b[64]; snprintf(b, sizeof(b), "{\"error\":\"%s\"}", keyErr);
        server.send(400, "application/json", b);
        return false;
    }
    return true;
}

static void syncWpasec() {
    noKeepAlive();
    if (!uplinkReady("no wpa-sec key", WPASec::hasApiKey())) return;
    server.send(200, "application/json", "{\"rebooting\":true}");
    requestBootSync(1);   // HTTPS upload needs a clean/unfragmented heap -> reboot-to-sync
}
static void syncOhc() {
    noKeepAlive();
    if (!uplinkReady("no OHC key", OHC::hasApiKey())) return;
    server.send(200, "application/json", "{\"rebooting\":true}");
    requestBootSync(2);
}
static void syncPwncrack() {
    noKeepAlive();
    if (!uplinkReady("no PwnCrack key", PwnCrack::hasApiKey())) return;
    server.send(200, "application/json", "{\"rebooting\":true}");
    requestBootSync(3);
}
static void sendSyncStatus() {
    noKeepAlive();
    char b[128];
    snprintf(b, sizeof(b), "{\"busy\":%s,\"last\":\"%s\"}",
             "false", bootSyncResult);
    server.send(200, "application/json", b);
}

static void bssidHex64(uint64_t b, char out[13]) {
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             (uint8_t)(b >> 40), (uint8_t)(b >> 32), (uint8_t)(b >> 24),
             (uint8_t)(b >> 16), (uint8_t)(b >> 8), (uint8_t)b);
}
static void jsonEsc(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o < cap - 2; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') { if (o < cap - 3) out[o++] = '\\'; out[o++] = c; }
        else if ((uint8_t)c >= 0x20) out[o++] = c;   // drop control chars
    }
    out[o] = 0;
}

static void listCaptures() {
    noKeepAlive();
    OinkMode::loadBoarBros();
    // NOTE: deliberately NOT loading the wpa-sec/OHC/PwnCrack potfile caches here —
    // in MANAGEMENT mode (AP+STA+web+DNS all up) heap is very tight, and pulling those
    // potfiles into RAM was leaving too little for the next request, wedging the server
    // (page unreachable after a delete). List the registry only; upload/crack tags come
    // back once the caches can be loaded lazily/safely.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent("[");

    const OinkMode::BoarBro* list = OinkMode::getExcludedList();
    int n = OinkMode::getExcludedCount();
    char hb[13], es[80], obj[220];
    for (int i = 0; i < n; i++) {
        bssidHex64(list[i].bssid, hb);
        jsonEsc(list[i].ssid, es, sizeof(es));
        snprintf(obj, sizeof(obj),
            "%s{\"bssid\":\"%s\",\"ssid\":\"%s\",\"captured\":%s,\"manual\":%s,"
            "\"w\":false,\"o\":false,\"p\":false,\"k\":false,\"pass\":\"\",\"ts\":%lu}",
            i ? "," : "", hb, es,
            (list[i].flags & OinkMode::BB_CAPTURED) ? "true" : "false",
            (list[i].flags & OinkMode::BB_MANUAL) ? "true" : "false",
            (unsigned long)list[i].ts);
        server.sendContent(obj);
    }
    server.sendContent("]");
    server.sendContent("");   // end chunked
}

static void deleteCapture() {
    noKeepAlive();
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) || !doc["bssid"].is<const char*>()) {
        server.send(400, "application/json", "{\"error\":\"bssid required\"}");
        return;
    }
    const char* bhex = doc["bssid"].as<const char*>();
    uint64_t b = strtoull(bhex, nullptr, 16);
    OinkMode::removeBoarBro(b);   // registry entry (persists)

    // Also delete the on-disk capture files for this BSSID — otherwise
    // importCapturedFiles() re-adds it to the exclusion registry on the next
    // capture start (so the network would never be attacked again). Heap-light
    // (fixed char buffers, capped count).
    const char* dir = SDLayout::handshakesDir();
    File d = Storage::fs().open(dir);
    if (d && d.isDirectory()) {
        char paths[8][100]; int nd = 0;
        for (File f = d.openNextFile(); f && nd < 8; f = d.openNextFile()) {
            if (!f.isDirectory()) {
                const char* n = f.name(); const char* sl = strrchr(n, '/'); if (sl) n = sl + 1;
                char fb[13];
                if (SDLayout::captureBssid(n, fb) && strcasecmp(fb, bhex) == 0) {
                    snprintf(paths[nd], sizeof(paths[nd]), "%s/%s", dir, n); nd++;
                }
            }
            f.close();
        }
        d.close();
        for (int i = 0; i < nd; i++) Storage::fs().remove(paths[i]);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void sendIndex() {
    noKeepAlive();
    server.send_P(200, "text/html", INDEX_HTML);
}

void begin() {
    if (up) return;
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());

    server.on("/api/status", HTTP_GET,  sendStatus);
    server.on("/api/config", HTTP_GET,  sendConfig);
    server.on("/api/config", HTTP_POST, saveConfig);
    server.on("/api/sync/wpasec",   HTTP_POST, syncWpasec);
    server.on("/api/sync/ohc",      HTTP_POST, syncOhc);
    server.on("/api/sync/pwncrack", HTTP_POST, syncPwncrack);
    server.on("/api/sync/status",   HTTP_GET,  sendSyncStatus);
    server.on("/api/captures", HTTP_GET,     listCaptures);
    server.on("/api/del_capture", HTTP_POST, deleteCapture);
    server.on("/", HTTP_GET, sendIndex);
    server.on("/generate_204", HTTP_GET, sendIndex);
    server.on("/hotspot-detect.html", HTTP_GET, sendIndex);
    server.on("/connecttest.txt", HTTP_GET, sendIndex);
    server.onNotFound(sendIndex);
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

void servicePendingSync() { /* board-side sync now runs on reboot (boot_sync) */ }

bool running() { return up; }

} // namespace WebUI
