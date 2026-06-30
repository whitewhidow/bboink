// hal/m5compat.h — M5Cardputer/M5Unified compatibility facade for the T-Embed.
//
// On the T-Embed build we do NOT link M5Unified/M5Cardputer. Instead, stub
// headers named M5Unified.h / M5Cardputer.h (in hal/tembed_shims/, added to the
// include path only for the T-Embed env) redirect here. This file provides:
//
//   * `M5Canvas`            aliased to lgfx::LGFX_Sprite (same drawing API)
//   * a global `M5`         facade: .Display (real LovyanGFX), .Speaker, .Power
//   * a global `M5Cardputer` facade with a .Keyboard that is *synthesised from
//     the rotary encoder + buttons* — it reports the exact key codes the menus
//     already consume (';' up, '.' down, ENTER select, BACKSPACE back), so the
//     existing navigation code runs unchanged.
//
// Text entry of arbitrary characters can't be faked from an encoder; those
// call sites get a dedicated on-screen char-picker in a later step.
#pragma once
#if defined(PORK_BOARD_TEMBED_CC1101)

#include <Arduino.h>
#include <vector>
#include "tembed_lgfx.h"

// Bring in only the LovyanGFX colour constants (TFT_BLACK, TFT_RED, ...) and
// text datums (top_left, middle_center, ...) that the UI code uses unqualified.
// NOT the whole lgfx namespace — that would drag in lgfx::v1::millis()/delay()
// and make every Arduino millis()/delay() call ambiguous.
using namespace lgfx::colors;
using namespace lgfx::textdatum;
using M5Canvas = lgfx::LGFX_Sprite;
// (LovyanGFX already exposes the `fonts` namespace globally, so `fonts::Font0`
//  resolves without any alias here.)

// Key code sentinels matching M5Cardputer's values.
static constexpr int KEY_ENTER     = 0x28;
static constexpr int KEY_BACKSPACE = 0x2a;
static constexpr int KEY_TAB       = 0x2b;

// ---------------------------------------------------------------------------
// Internal HAL state shared between the facade and m5compat.cpp.
// ---------------------------------------------------------------------------
namespace porkhal {
    // Virtual key state, recomputed each update() from the encoder + buttons.
    // Each flag is edge-true for exactly one frame.
    struct VKey {
        bool up      = false;   // ';'  (encoder CCW)
        bool down    = false;   // '.'  (encoder CW)
        bool enter    = false;  // encoder push (short)
        bool back    = false;   // side button  -> BACKSPACE
        bool backLongPress = false;  // side button held ~3s (power-off gesture)
        bool changed = false;   // any transition this frame
    };
    extern VKey vkey;

    void inputInit();
    void inputPoll();           // called from M5(.Cardputer).update()
    void ledInit();

    // Blocking on-screen char-picker (the keyboard replacement for text entry).
    // `buf` is pre-filled with the current value and edited in place (kept
    // NUL-terminated, never exceeding min(bufSize-1, limit)). Encoder rotates
    // through the character grid + DEL/OK/CANCEL cells; click activates; the
    // back button cancels. Returns true on OK, false on cancel.
    bool charPicker(const char* prompt, char* buf, size_t bufSize, size_t limit);
}

// ---------------------------------------------------------------------------
// M5Cardputer.Keyboard facade — name and nested KeysState match M5Cardputer so
// existing code (`Keyboard_Class::KeysState state = ...`) compiles unchanged.
// ---------------------------------------------------------------------------
class Keyboard_Class {
public:
    struct KeysState {
        std::vector<char> word;     // printable chars pressed this frame
        bool del   = false;         // backspace / back
        bool enter = false;
        bool space = false;
        bool tab   = false;
        bool fn    = false;
    };
    bool isPressed() const;
    bool isChange() const;
    bool isKeyPressed(char c) const;
    bool isKeyPressed(int keycode) const;
    KeysState keysState() const;
};

// ---------------------------------------------------------------------------
// M5 / M5Cardputer top-level facades
// ---------------------------------------------------------------------------
class SpeakerFacade {
public:
    // Drive the MAX98357A I2S amp to emit a square-wave tone (blocking for the
    // tone's duration). Implemented in m5compat.cpp.
    void tone(uint16_t freq, uint32_t durationMs);
    void stop() {}
    bool begin() { return true; }
    void setVolume(uint8_t v) { _volume = v; }
    void setAllChannelVolume(uint8_t v) { _volume = v; }
    uint8_t _volume = 160;   // 0..255 amplitude scale
};

// board_t / Power_Class compatibility (main.cpp checks board_M5CardputerADV;
// charging.cpp & mood.cpp compare against Power_Class::is_charging_t).
namespace m5 {
    enum class board_t { board_unknown, board_M5Cardputer, board_M5CardputerADV };
    class Power_Class {
    public:
        // Unscoped (like M5Unified) so it converts to bool for `!isCharging()`
        // while still allowing qualified `is_charging_t::is_charging` access.
        enum is_charging_t : int8_t {
            charge_unknown = -1,
            is_discharging = 0,
            is_charging    = 1,
        };
    };
}

class PowerFacade {
public:
    int  getBatteryLevel();         // BQ27220 StateOfCharge % (fallback 100)
    int  getBatteryVoltage();       // BQ27220 Voltage mV (fallback 4000)
    m5::Power_Class::is_charging_t isCharging() {
        return m5::Power_Class::is_charging_t::charge_unknown;
    }
    int16_t getVBUSVoltage() { return -1; }   // <0 => VBUS sensing unsupported
    void setLed(uint8_t) {}
};

// RTC facade — the T-Embed has no battery-backed RTC chip wired by default, so
// we surface the ESP32 system clock (set via NTP/SNTP once online) in the same
// rtc_datetime_t shape M5Unified uses.
struct rtc_date_t { int year = 1970; int month = 1; int date = 1; int weekDay = 0; };
struct rtc_time_t { int hours = 0; int minutes = 0; int seconds = 0; };
struct rtc_datetime_t { rtc_date_t date; rtc_time_t time; };

class RtcFacade {
public:
    rtc_datetime_t getDateTime();
};

// IMU facade — T-Embed has no IMU; report "no data" so callers fall back.
class ImuFacade {
public:
    bool getAccel(float* ax, float* ay, float* az) {
        if (ax) *ax = 0; if (ay) *ay = 0; if (az) *az = 0;
        return false;
    }
};

struct M5Config {};   // opaque result of M5.config()

class M5Facade {
public:
    LGFX_TEmbed   Display;
    SpeakerFacade Speaker;
    PowerFacade   Power;
    RtcFacade     Rtc;
    ImuFacade     Imu;

    M5Config config() { return M5Config{}; }
    void begin(M5Config&) { begin(); }
    void begin();
    void update();
    m5::board_t getBoard() { return m5::board_t::board_unknown; }
};
extern M5Facade M5;

class M5CardputerFacade {
public:
    Keyboard_Class Keyboard;
    // Same physical panel as M5.Display; bound at construction (M5 is defined
    // before M5Cardputer in m5compat.cpp, so its Display already exists).
    LGFX_TEmbed& Display;
    M5CardputerFacade();
    void begin(M5Config&, bool /*enableKeyboard*/ = true);
    void update();
};
extern M5CardputerFacade M5Cardputer;

#endif // PORK_BOARD_TEMBED_CC1101
