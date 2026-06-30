// Stub: Avatar — the animated piglet/grass graphics are removed in tembed-oink.
// All methods are no-ops.
#pragma once

#include <Arduino.h>

class Avatar {
public:
    static void sniff() {}
    static void setGrassMoving(bool moving, bool directionRight = true) { (void)moving; (void)directionRight; }
    static void setGrassSpeed(uint16_t ms) { (void)ms; }
};
