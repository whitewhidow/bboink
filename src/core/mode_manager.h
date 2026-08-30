// mode_manager.h — the two-mode (CAPTURE / MANAGEMENT) coordinator.
//
// BBoink has exactly one radio degree of freedom: promiscuous capture and a
// normal WiFi stack are mutually exclusive (promiscuous drops the STA/AP netif).
// ModeManager owns that single axis so every board — from the one-button
// Waveshare C5-LCD up to the encoder T-Embed — shares one transition path:
//
//   CAPTURE     : engine channel-hops / sniffs / deauths, no IP stack.
//   MANAGEMENT  : engine stopped, radio back to normal WiFi (STA reconnect for
//                 now; AP+STA + web UI land in later milestones).
//
// The transitions centralised here are the canonical ones used by the boot
// decision, the on-device menu, and (later) the single-button toggle + web UI.
// See docs/DESIGN-mode-webui.md.
#pragma once
#include <Arduino.h>

namespace ModeManager {

enum class Mode : uint8_t { CAPTURE, MANAGEMENT };

// Decide the boot mode from config policy (auto/capture/management) and enter it.
// Called once from App::begin() in place of the old go(MENU).
void begin();

Mode        current();
const char* currentName();      // "CAPTURE" / "MANAGEMENT"
bool        inCapture();

// Canonical transitions (radio teardown/bringup + screen change live here).
//   enterCapture(clearLock=true) : clears any target lock unless told otherwise,
//   marks the device captureReady (persisted once), shows the capture screen.
void enterCapture(bool clearLock = true);
//   enterManagement() : if leaving CAPTURE, stops the engine, drops promiscuous,
//   restores the STA uplink and fires the per-network ntfy alerts, then shows the
//   management surface (the menu, for now).
void enterManagement();

// Single-button action: swap to the other mode. Wired to Input::toggle; on
// multi-button boards the same transitions are reached via the menu + back.
void toggle();

bool captureReady();            // has the device been provisioned for capture?

} // namespace ModeManager
