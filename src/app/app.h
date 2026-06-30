// app.h — tembed-oink UI state machine + shared drawing helpers.
#pragma once

#include <Arduino.h>
#include "../hal/m5compat.h"
#include "../hal/board.h"

namespace App {

enum class Screen { MENU, CAPTURE, MANAGE, OHC, OPTIONS };

// One frame's worth of debounced encoder/button edges (true for one tick).
struct Input {
    bool up    = false;   // encoder CCW
    bool down  = false;   // encoder CW
    bool enter = false;   // encoder click
    bool back  = false;   // side button
    bool any() const { return up || down || enter || back; }
};

extern Screen screen;

void begin();                 // after hardware is up
void tick();                  // once per loop(), after M5Cardputer.update()
void go(Screen s);            // switch screen (runs the target's enter hook)
void powerOff();              // deep-sleep until a button wakes it (global)
Input readInput();            // snapshot porkhal::vkey for this frame

// Shared plain-UI primitives (app.cpp). Colours come from LovyanGFX via m5compat.
void clear(uint16_t bg = 0x0000);
void header(const char* title);
void footer(const char* hint);
// Draw a vertical list with one row highlighted. rows[i] is the full row text.
void drawList(const char* const* rows, int count, int selected,
              int firstVisible, int visibleRows, int textSize = 2);
// Centered single-line status (used for "scanning", "no captures", etc.).
void centerMsg(const char* msg, uint16_t color = 0xFFFF);
// Human-readable byte size into a shared static buffer ("3.2G" / "47M" / "812K").
const char* fmtBytes(uint64_t bytes);

} // namespace App

// Per-screen hooks.
namespace ScreenMenu    { void enter(); void tick(const App::Input&); }
namespace ScreenCapture { void enter(); void tick(const App::Input&); }
namespace ScreenManage  { void enter(); void tick(const App::Input&); }
namespace ScreenOHC     { void enter(); void tick(const App::Input&); }
namespace ScreenOptions { void enter(); void tick(const App::Input&); }
