// hal/m5compat.cpp — implementation of the T-Embed compatibility facade.
#include "m5compat.h"
#if defined(PORK_BOARD_TEMBED_CC1101)

#include "board.h"
#include <RotaryEncoder.h>
#include <time.h>
#include <cstring>
#include <Wire.h>

// ---------------------------------------------------------------------------
// Input: rotary encoder + two buttons synthesised into virtual key edges.
//
// NOTE: edge flags in `vkey` are recomputed on every inputPoll() from *new*
// events since the previous poll (encoder position delta, button press edges),
// so they are reported exactly once even if poll runs twice per loop. The
// firmware polls once per loop iteration (M5Cardputer.update() at the top),
// then reads the keys, which is the expected pattern.
// ---------------------------------------------------------------------------
namespace porkhal {

VKey vkey;

static RotaryEncoder* enc = nullptr;
static long lastPos = 0;
static bool ready = false;

struct Btn { uint8_t pin; bool stable; bool lastRead; uint32_t tEdge; };
static Btn btnSel  { PORK_ENC_KEY,  HIGH, HIGH, 0 };   // active-low (pullup)
static Btn btnBack { PORK_BTN_BACK, HIGH, HIGH, 0 };

// Returns true on the falling (press) edge, debounced ~8ms.
static bool pressEdge(Btn& b) {
    bool raw = digitalRead(b.pin);
    if (raw != b.lastRead) { b.lastRead = raw; b.tEdge = millis(); }
    if (millis() - b.tEdge > 8 && b.stable != raw) {
        b.stable = raw;
        if (raw == LOW) return true;
    }
    return false;
}

void inputInit() {
    if (ready) return;
    // T-Embed CC1101 encoder emits 2 transitions per detent; FOUR3 would need
    // two detents per menu move, so use TWO03 (one move per detent).
    enc = new RotaryEncoder(PORK_ENC_A, PORK_ENC_B, RotaryEncoder::LatchMode::TWO03);
    pinMode(PORK_ENC_KEY,  INPUT_PULLUP);
    pinMode(PORK_BTN_BACK, INPUT_PULLUP);
    lastPos = 0;
    ready = true;
}

void inputPoll() {
    if (!ready) inputInit();
    enc->tick();

    VKey v;  // all-false
    long pos = enc->getPosition();
    long d = pos - lastPos;
    if (d != 0) {
        lastPos = pos;
        // Encoder CW (increasing) moves selection down, CCW moves up.
        if (d > 0) v.down = true; else v.up = true;
        v.changed = true;
    }
    if (pressEdge(btnSel))  { v.enter = true; v.changed = true; }
    if (pressEdge(btnBack)) { v.back  = true; v.changed = true; }

    // Long-press BACK (held ~3s) -> one-shot power-off gesture (App::tick acts on it).
    static uint32_t backDownSince = 0;
    static bool backLongFired = false;
    if (digitalRead(PORK_BTN_BACK) == LOW) {
        if (backDownSince == 0) backDownSince = millis();
        else if (!backLongFired && millis() - backDownSince >= 3000) {
            v.backLongPress = true; v.changed = true; backLongFired = true;
        }
    } else {
        backDownSince = 0; backLongFired = false;
    }
    vkey = v;
}

void ledInit() { /* neopixelWrite() initialises the RMT channel lazily */ }

} // namespace porkhal

// ---------------------------------------------------------------------------
// Speaker: square-wave tone over I2S to the MAX98357A amp.
// Install the I2S driver only for the tone, then uninstall so the WS pin (40,
// shared with the display RST net) is released the rest of the time.
// ---------------------------------------------------------------------------
#include <driver/i2s.h>

void SpeakerFacade::tone(uint16_t freq, uint32_t durationMs) {
    if (freq == 0 || durationMs == 0) return;

    const int kSampleRate = 16000;
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = kSampleRate;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = 0;
    cfg.dma_buf_count        = 6;
    cfg.dma_buf_len          = 256;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return;
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PORK_I2S_BCLK;
    pins.ws_io_num    = PORK_I2S_WS;
    pins.data_out_num = PORK_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_NUM_0, &pins);

    const int16_t amp = (int16_t)(40 * _volume);     // _volume(0..255) -> amplitude
    int halfPeriod = (kSampleRate / freq) / 2;        // samples per half square wave
    if (halfPeriod < 1) halfPeriod = 1;
    int totalSamples = (int)((uint64_t)kSampleRate * durationMs / 1000);

    int16_t buf[256];
    int idx = 0, phase = 0;
    int16_t level = amp;
    size_t written;
    for (int n = 0; n < totalSamples; n++) {
        buf[idx++] = level;
        if (++phase >= halfPeriod) { phase = 0; level = (int16_t)-level; }
        if (idx == 256) {
            i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
            idx = 0;
        }
    }
    if (idx) i2s_write(I2S_NUM_0, buf, idx * sizeof(int16_t), &written, portMAX_DELAY);

    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);   // release WS pin (40) so display RST stays idle
}

namespace porkhal {
// (porkhal namespace continues below)

// ---------------------------------------------------------------------------
// charPicker — blocking encoder-driven text entry modal.
// ---------------------------------------------------------------------------
static const char CP_CHARSET[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "-_.:/@!#$%&*+= ";

bool charPicker(const char* prompt, char* buf, size_t bufSize, size_t limit) {
    if (!buf || bufSize == 0) return false;

    const int charsetLen = (int)(sizeof(CP_CHARSET) - 1);
    const int IDX_DEL = charsetLen;       // [DEL]
    const int IDX_OK  = charsetLen + 1;   // [OK]
    const int IDX_CXL = charsetLen + 2;   // [CXL]
    const int N       = charsetLen + 3;

    size_t cap = bufSize - 1;
    if (limit > 0 && limit < cap) cap = limit;

    size_t len = strnlen(buf, bufSize);
    if (len > cap) { len = cap; buf[len] = '\0'; }

    int cursor = 0;
    bool dirty = true;

    const int COLS  = 16;
    const int CW    = 20;
    const int CH    = 18;
    const int GRIDX = 4;
    const int GRIDY = 44;

    auto& D = M5.Display;
    D.setTextSize(1);

    for (;;) {
        M5.update();                      // refreshes porkhal::vkey
        const VKey v = vkey;

        if (v.back) return false;
        if (v.up)   { cursor = (cursor + N - 1) % N; dirty = true; }
        if (v.down) { cursor = (cursor + 1) % N;     dirty = true; }
        if (v.enter) {
            if (cursor < charsetLen) {
                if (len < cap) { buf[len++] = CP_CHARSET[cursor]; buf[len] = '\0'; }
            } else if (cursor == IDX_DEL) {
                if (len > 0) buf[--len] = '\0';
            } else if (cursor == IDX_OK) {
                return true;
            } else { // IDX_CXL
                return false;
            }
            dirty = true;
        }

        if (dirty) {
            D.startWrite();
            D.fillScreen(TFT_BLACK);

            // Prompt + current value.
            D.setTextDatum(top_left);
            D.setTextColor(TFT_WHITE, TFT_BLACK);
            D.setTextSize(2);
            D.drawString(prompt ? prompt : "ENTER TEXT", 4, 2);
            D.setTextSize(2);
            char shown[96];
            snprintf(shown, sizeof(shown), "%s_", buf);
            D.setTextColor(TFT_CYAN, TFT_BLACK);
            D.drawString(shown, 4, 22);

            // Character grid.
            D.setTextSize(2);
            D.setTextDatum(middle_center);
            for (int i = 0; i < charsetLen; ++i) {
                int col = i % COLS, row = i / COLS;
                int x = GRIDX + col * CW, y = GRIDY + row * CH;
                bool sel = (i == cursor);
                if (sel) D.fillRect(x, y, CW - 1, CH - 1, TFT_WHITE);
                D.setTextColor(sel ? TFT_BLACK : TFT_WHITE, sel ? TFT_WHITE : TFT_BLACK);
                char cs[2] = { CP_CHARSET[i], 0 };
                D.drawString(cs, x + CW / 2, y + CH / 2);
            }

            // Action cells.
            struct { int idx; const char* label; int x; } acts[] = {
                { IDX_DEL, "DEL", 6   },
                { IDX_OK,  "OK",  120 },
                { IDX_CXL, "CXL", 220 },
            };
            int ay = 150, aw = 92, ah = 18;
            for (auto& a : acts) {
                bool sel = (a.idx == cursor);
                if (sel) D.fillRect(a.x, ay, aw, ah, TFT_WHITE);
                else     D.drawRect(a.x, ay, aw, ah, TFT_WHITE);
                D.setTextColor(sel ? TFT_BLACK : TFT_WHITE, sel ? TFT_WHITE : TFT_BLACK);
                D.drawString(a.label, a.x + aw / 2, ay + ah / 2);
            }

            D.endWrite();
            D.setTextDatum(top_left);
            dirty = false;
        }
        delay(8);
    }
}

}  // namespace porkhal

// ---------------------------------------------------------------------------
// Keyboard_Class
// ---------------------------------------------------------------------------
bool Keyboard_Class::isPressed() const {
    const auto& v = porkhal::vkey;
    return v.up || v.down || v.enter || v.back;
}

bool Keyboard_Class::isChange() const { return porkhal::vkey.changed; }

bool Keyboard_Class::isKeyPressed(char c) const {
    const auto& v = porkhal::vkey;
    switch (c) {
        case ';': return v.up;     // menu up
        case '.': return v.down;   // menu down
        // Map confirm-dialog keys onto the encoder: click = yes, back = no.
        case 'Y': case 'y': return v.enter;
        case 'N': case 'n': return v.back;
        // Backtick is the "escape / exit mode to menu" key in porkchop.cpp;
        // the back button must trigger it too, alongside BACKSPACE.
        case '`': return v.back;
        default:  return false;    // other action keys: handled in a later UX pass
    }
}

bool Keyboard_Class::isKeyPressed(int keycode) const {
    const auto& v = porkhal::vkey;
    if (keycode == KEY_ENTER)     return v.enter;
    if (keycode == KEY_BACKSPACE) return v.back;
    return false;
}

Keyboard_Class::KeysState Keyboard_Class::keysState() const {
    const auto& v = porkhal::vkey;
    KeysState k;
    k.enter = v.enter;
    k.del   = v.back;
    if (v.up)   k.word.push_back(';');
    if (v.down) k.word.push_back('.');
    return k;
}

// ---------------------------------------------------------------------------
// PowerFacade
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// BQ27220 fuel gauge over I2C (graceful fallback if the chip isn't reachable —
// pins/address still need on-device confirmation, see TEMBED_PORT.md).
// ---------------------------------------------------------------------------
namespace {
    bool g_gaugeProbed  = false;
    bool g_gaugePresent = false;

    void gaugeEnsureInit() {
        if (g_gaugeProbed) return;
        g_gaugeProbed = true;
        Wire.begin(PORK_I2C_SDA, PORK_I2C_SCL);
        Wire.beginTransmission(PORK_BQ27220_ADDR);
        g_gaugePresent = (Wire.endTransmission() == 0);
    }

    // Standard BQ27220 commands: 0x08 Voltage(mV), 0x2C StateOfCharge(%).
    bool gaugeRead16(uint8_t cmd, uint16_t& out) {
        gaugeEnsureInit();
        if (!g_gaugePresent) return false;
        Wire.beginTransmission(PORK_BQ27220_ADDR);
        Wire.write(cmd);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom((int)PORK_BQ27220_ADDR, 2) != 2) return false;
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        out = (uint16_t)lo | ((uint16_t)hi << 8);
        return true;
    }
}

int PowerFacade::getBatteryLevel() {
    uint16_t soc;
    if (gaugeRead16(0x2C, soc) && soc <= 100) return (int)soc;
    return 100;  // fallback when gauge unavailable
}

int PowerFacade::getBatteryVoltage() {
    uint16_t mv;
    if (gaugeRead16(0x08, mv) && mv > 1000 && mv < 6000) return (int)mv;
    return 4000;  // fallback ~4.0 V
}

// ---------------------------------------------------------------------------
// RtcFacade — surface the ESP32 system clock as an rtc_datetime_t.
// ---------------------------------------------------------------------------
rtc_datetime_t RtcFacade::getDateTime() {
    rtc_datetime_t dt;
    time_t now = time(nullptr);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    dt.date.year    = tmv.tm_year + 1900;
    dt.date.month   = tmv.tm_mon + 1;
    dt.date.date    = tmv.tm_mday;
    dt.date.weekDay = tmv.tm_wday;
    dt.time.hours   = tmv.tm_hour;
    dt.time.minutes = tmv.tm_min;
    dt.time.seconds = tmv.tm_sec;
    return dt;
}

// ---------------------------------------------------------------------------
// M5 / M5Cardputer facades + globals
// ---------------------------------------------------------------------------
M5Facade        M5;
M5CardputerFacade M5Cardputer;

static void bringUpHardware() {
    M5.Display.init();
    M5.Display.setRotation(3);          // 320x170 landscape (T-Embed CC1101: wheel on right)
    M5.Display.setBrightness(200);
    porkhal::inputInit();
    porkhal::ledInit();
}

void M5Facade::begin()  { bringUpHardware(); }
void M5Facade::update() { porkhal::inputPoll(); }

M5CardputerFacade::M5CardputerFacade() : Display(M5.Display) {}
void M5CardputerFacade::begin(M5Config&, bool) { bringUpHardware(); }
void M5CardputerFacade::update()               { porkhal::inputPoll(); }

#endif // PORK_BOARD_TEMBED_CC1101
