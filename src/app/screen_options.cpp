// screen_options.cpp — WiFi setup, wpa-sec/OHC keys, and oink tuning settings.
#include "app.h"
#include "../core/config.h"
#include "../core/net_link.h"
#include <WiFi.h>

namespace ScreenOptions {

enum Field {
    OPT_WIFI, OPT_KEY, OPT_OHC, OPT_NTFY, OPT_NTFYFILE,
    OPT_HOP, OPT_LOCK, OPT_RSSI, OPT_DEAUTH, OPT_RNDMAC, OPT_BURST, OPT_JITTER,
    OPT_BRIGHT, OPT_SOUND,
    OPT_COUNT
};

struct FieldDef {
    const char* label;
    bool   isText;
    bool   isToggle;
    int    minV, maxV, step;
};

static const FieldDef defs[OPT_COUNT] = {
    { "WiFi",      false, false, 0, 0, 0 },   // action: scan -> pick SSID -> password
    { "WPA Key",   true,  false, 0, 0, 0 },
    { "OHC Key",   true,  false, 0, 0, 0 },
    { "Ntfy Topic",true,  false, 0, 0, 0 },
    { "Ntfy File", false, true,  0, 1, 1 },
    { "Ch Hop ms", false, false, 50, 2000, 50 },
    { "Lock ms",   false, false, 1000, 10000, 500 },
    { "Atk RSSI",  false, false, -90, -50, 5 },
    { "Deauth",    false, true,  0, 1, 1 },
    { "Rnd MAC",   false, true,  0, 1, 1 },
    { "Burst",     false, false, 1, 8, 1 },
    { "Jitter ms", false, false, 0, 20, 1 },
    { "Brightness",false, false, 10, 255, 15 },
    { "Sound",     false, true,  0, 1, 1 },
};

static int  sel = 0;
static int  firstVisible = 0;
static constexpr int VISIBLE = 5;
static int  editing = -1;      // numeric field being edited, or -1
static bool dirty = true;

static const char* rowPtrs[OPT_COUNT];
static char        rowBuf[OPT_COUNT][44];

static int  getNum(int f) {
    WiFiConfig& w = Config::wifi();
    switch (f) {
        case OPT_HOP:    return w.channelHopInterval;
        case OPT_LOCK:   return w.lockTime;
        case OPT_RSSI:   return w.attackMinRssi;
        case OPT_DEAUTH: return w.enableDeauth ? 1 : 0;
        case OPT_RNDMAC: return w.randomizeMAC ? 1 : 0;
        case OPT_BURST:  return w.deauthBurstCount;
        case OPT_JITTER: return w.deauthJitterMax;
        case OPT_BRIGHT: return w.displayBrightness;
        case OPT_SOUND:  return w.soundEnabled ? 1 : 0;
        case OPT_NTFYFILE: return w.ntfyAttachFile ? 1 : 0;
    }
    return 0;
}

static void setNum(int f, int v) {
    WiFiConfig& w = Config::wifi();
    if (v < defs[f].minV) v = defs[f].minV;
    if (v > defs[f].maxV) v = defs[f].maxV;
    switch (f) {
        case OPT_HOP:    w.channelHopInterval = (uint16_t)v; break;
        case OPT_LOCK:   w.lockTime = (uint16_t)v;           break;
        case OPT_RSSI:   w.attackMinRssi = (int8_t)v;        break;
        case OPT_DEAUTH: w.enableDeauth = (v != 0);          break;
        case OPT_RNDMAC: w.randomizeMAC = (v != 0);          break;
        case OPT_BURST:  w.deauthBurstCount = (uint8_t)v;    break;
        case OPT_JITTER: w.deauthJitterMax = (uint8_t)v;     break;
        case OPT_BRIGHT: w.displayBrightness = (uint8_t)v;
                         M5.Display.setBrightness((uint8_t)v); break;  // live preview
        case OPT_SOUND:  w.soundEnabled = (v != 0);          break;
        case OPT_NTFYFILE: w.ntfyAttachFile = (v != 0);      break;
    }
}

static char* textBuf(int f) {
    WiFiConfig& w = Config::wifi();
    switch (f) {
        case OPT_KEY:  return w.wpaSecKey;
        case OPT_OHC:  return w.ohcKey;
        case OPT_NTFY: return w.ntfyTopic;
    }
    return nullptr;
}
static size_t textCap(int f) {
    switch (f) {
        case OPT_KEY:  return sizeof(Config::wifi().wpaSecKey);
        case OPT_OHC:  return sizeof(Config::wifi().ohcKey);
        case OPT_NTFY: return sizeof(Config::wifi().ntfyTopic);
    }
    return 0;
}

static void buildRow(int f) {
    const FieldDef& d = defs[f];
    char val[28];
    if (f == OPT_WIFI) {
        const char* s = Config::wifi().otaSSID;
        snprintf(rowBuf[f], sizeof(rowBuf[f]), "WiFi: %.16s", (s && s[0]) ? s : "(not set)");
        rowPtrs[f] = rowBuf[f];
        return;
    }
    if (d.isText) {
        const char* t = textBuf(f);
        if (f == OPT_KEY || f == OPT_OHC)   // keys are sensitive -> hide value
            snprintf(val, sizeof(val), "%s", (t && t[0]) ? "(set)" : "(empty)");
        else                                 // ntfy topic etc. -> show it
            snprintf(val, sizeof(val), "%s", (t && t[0]) ? t : "(empty)");
    } else if (d.isToggle) {
        snprintf(val, sizeof(val), "%s", getNum(f) ? "ON" : "OFF");
    } else {
        snprintf(val, sizeof(val), "%d", getNum(f));
    }
    const char* mark = (editing == f) ? "<" : " ";
    const char* mark2 = (editing == f) ? ">" : "";
    snprintf(rowBuf[f], sizeof(rowBuf[f]), "%s: %s%.12s%s", d.label, mark, val, mark2);
    rowPtrs[f] = rowBuf[f];
}

static void buildAll() { for (int f = 0; f < OPT_COUNT; f++) buildRow(f); }

void enter() {
    sel = 0; firstVisible = 0; editing = -1; dirty = true;
}

static void draw() {
    App::clear();
    App::header("OPTIONS");
    buildAll();
    App::drawList(rowPtrs, OPT_COUNT, sel, firstVisible, VISIBLE);
    App::footer(editing >= 0 ? "turn: adjust  click: ok"
                             : "click: edit   back: save & exit");
}

// Guided WiFi setup: scan -> pick SSID from the list -> enter password -> save
// and try to connect. Replaces the old separate SSID/Pass text fields.
static void wifiSetupFlow() {
    App::clear(); App::header("WIFI"); App::centerMsg("scanning...", TFT_CYAN);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);   // synchronous, include hidden
    if (n <= 0) {
        App::centerMsg("no networks found", TFT_RED);
        App::footer("press back");
        while (true) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
        WiFi.scanDelete();
        return;
    }
    static constexpr int MAXN = 30;
    if (n > MAXN) n = MAXN;
    static char rb[MAXN][36];
    static const char* rp[MAXN];
    for (int i = 0; i < n; i++) {
        String ss = WiFi.SSID(i);
        const char* lock = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*";
        snprintf(rb[i], sizeof(rb[i]), "%s%-17.17s %d", lock,
                 ss.length() ? ss.c_str() : "(hidden)", (int)WiFi.RSSI(i));
        rp[i] = rb[i];
    }

    int s = 0, first = 0; constexpr int VIS = 7; bool redraw = true;
    while (true) {
        M5Cardputer.update();
        if (porkhal::vkey.back) { WiFi.scanDelete(); return; }      // cancel
        if (porkhal::vkey.up)   { s = (s + n - 1) % n; redraw = true; }
        if (porkhal::vkey.down) { s = (s + 1) % n;     redraw = true; }
        if (s < first) first = s;
        if (s >= first + VIS) first = s - VIS + 1;
        if (porkhal::vkey.enter) break;
        if (redraw) {
            App::clear(); App::header("PICK NETWORK");
            App::drawList(rp, n, s, first, VIS, 1);
            App::footer("turn: pick  click: ok  back: cancel");
            redraw = false;
        }
        delay(20);
    }

    char ssid[33];
    strncpy(ssid, WiFi.SSID(s).c_str(), sizeof(ssid) - 1); ssid[sizeof(ssid) - 1] = '\0';
    bool open = (WiFi.encryptionType(s) == WIFI_AUTH_OPEN);
    WiFi.scanDelete();
    if (ssid[0] == '\0') return;   // hidden/blank SSID -> can't type it here

    char pass[65] = {0};
    if (!open && !porkhal::charPicker("Password", pass, sizeof(pass), 63)) return;  // cancelled

    WiFiConfig& w = Config::wifi();
    strncpy(w.otaSSID, ssid, sizeof(w.otaSSID) - 1);     w.otaSSID[sizeof(w.otaSSID) - 1] = '\0';
    strncpy(w.otaPassword, pass, sizeof(w.otaPassword) - 1); w.otaPassword[sizeof(w.otaPassword) - 1] = '\0';
    Config::save();

    App::clear(); App::header("WIFI"); App::centerMsg("connecting...", TFT_CYAN);
    bool ok = NetLink::connectConfigured();
    App::centerMsg(ok ? "connected" : "saved (no link yet)", ok ? TFT_GREEN : TFT_YELLOW);
    App::footer("press back");
    uint32_t t = millis();
    while (millis() - t < 2000) { M5Cardputer.update(); if (porkhal::vkey.back || porkhal::vkey.enter) break; delay(20); }
}

void tick(const App::Input& in) {
    if (editing >= 0) {
        // numeric edit mode
        if (in.up)   { setNum(editing, getNum(editing) + defs[editing].step); dirty = true; }
        if (in.down) { setNum(editing, getNum(editing) - defs[editing].step); dirty = true; }
        if (in.enter || in.back) { editing = -1; dirty = true; }
        if (dirty) { draw(); dirty = false; }
        return;
    }

    if (in.back) {
        Config::save();
        App::go(App::Screen::MENU);
        return;
    }
    if (in.up)   { sel = (sel + OPT_COUNT - 1) % OPT_COUNT; dirty = true; }
    if (in.down) { sel = (sel + 1) % OPT_COUNT;           dirty = true; }
    if (sel < firstVisible) firstVisible = sel;
    if (sel >= firstVisible + VISIBLE) firstVisible = sel - VISIBLE + 1;

    if (in.enter) {
        if (sel == OPT_WIFI) { wifiSetupFlow(); dirty = true; if (dirty) { draw(); dirty = false; } return; }
        const FieldDef& d = defs[sel];
        if (d.isText) {
            char* buf = textBuf(sel);
            char tmp[80];
            strncpy(tmp, buf ? buf : "", sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            if (porkhal::charPicker(d.label, tmp, sizeof(tmp), textCap(sel) - 1)) {
                strncpy(buf, tmp, textCap(sel) - 1);
                buf[textCap(sel) - 1] = '\0';
            }
            dirty = true;
        } else if (d.isToggle) {
            setNum(sel, getNum(sel) ? 0 : 1);
            dirty = true;
        } else {
            editing = sel;
            dirty = true;
        }
    }

    if (dirty) { draw(); dirty = false; }
}

} // namespace ScreenOptions
