// screen_options.cpp — WiFi creds, wpa-sec key, and oink tuning settings.
#include "app.h"
#include "../core/config.h"

namespace ScreenOptions {

enum Field {
    OPT_SSID, OPT_PASS, OPT_KEY, OPT_OHC,
    OPT_HOP, OPT_LOCK, OPT_RSSI, OPT_DEAUTH, OPT_BURST, OPT_JITTER,
    OPT_COUNT
};

struct FieldDef {
    const char* label;
    bool   isText;
    bool   isToggle;
    int    minV, maxV, step;
};

static const FieldDef defs[OPT_COUNT] = {
    { "WiFi SSID", true,  false, 0, 0, 0 },
    { "WiFi Pass", true,  false, 0, 0, 0 },
    { "WPA Key",   true,  false, 0, 0, 0 },
    { "OHC Key",   true,  false, 0, 0, 0 },
    { "Ch Hop ms", false, false, 50, 2000, 50 },
    { "Lock ms",   false, false, 1000, 10000, 500 },
    { "Atk RSSI",  false, false, -90, -50, 5 },
    { "Deauth",    false, true,  0, 1, 1 },
    { "Burst",     false, false, 1, 8, 1 },
    { "Jitter ms", false, false, 0, 20, 1 },
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
        case OPT_BURST:  return w.deauthBurstCount;
        case OPT_JITTER: return w.deauthJitterMax;
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
        case OPT_BURST:  w.deauthBurstCount = (uint8_t)v;    break;
        case OPT_JITTER: w.deauthJitterMax = (uint8_t)v;     break;
    }
}

static char* textBuf(int f) {
    WiFiConfig& w = Config::wifi();
    switch (f) {
        case OPT_SSID: return w.otaSSID;
        case OPT_PASS: return w.otaPassword;
        case OPT_KEY:  return w.wpaSecKey;
        case OPT_OHC:  return w.ohcKey;
    }
    return nullptr;
}
static size_t textCap(int f) {
    switch (f) {
        case OPT_SSID: return sizeof(Config::wifi().otaSSID);
        case OPT_PASS: return sizeof(Config::wifi().otaPassword);
        case OPT_KEY:  return sizeof(Config::wifi().wpaSecKey);
        case OPT_OHC:  return sizeof(Config::wifi().ohcKey);
    }
    return 0;
}

static void buildRow(int f) {
    const FieldDef& d = defs[f];
    char val[28];
    if (d.isText) {
        const char* t = textBuf(f);
        if (f == OPT_PASS || f == OPT_KEY || f == OPT_OHC)
            snprintf(val, sizeof(val), "%s", (t && t[0]) ? "(set)" : "(empty)");
        else
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
