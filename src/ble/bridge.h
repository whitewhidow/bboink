// bridge.h — BLE GATT bridge so an Android phone can sync the board with no WiFi
// uplink. The board is a NimBLE peripheral; a Web-Bluetooth page (over the phone's
// cellular) pulls captures and pushes back cracked results. See docs/DESIGN-ble-bridge.md.
#pragma once
#include <Arduino.h>

namespace BleBridge {

// Start the NimBLE peripheral (call with WiFi already off). Advertises "BBoink-XXXX".
void start(const char* advName);
void stop();
bool running();

// Pump file streaming / pending work. Call every main-loop tick while in bridge mode.
void loop();

// Status for the on-screen indicator.
bool     connected();
uint16_t filesSent();     // capture files streamed to the phone this session
uint16_t crackedIn();     // cracked entries written back this session
bool     exitRequested(); // phone sent {"c":"done"} -> app should leave bridge mode

} // namespace BleBridge
