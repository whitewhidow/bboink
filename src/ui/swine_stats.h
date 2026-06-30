// Stub: SwineStats — the original applied class buffs/debuffs on top of config.
// In tembed-oink these getters return the raw tuning values from Config so the
// Options screen's settings directly drive the capture engine.
#pragma once

#include <Arduino.h>
#include "../core/config.h"

class SwineStats {
public:
    static uint8_t  getDeauthBurstCount()    { return Config::wifi().deauthBurstCount; }
    static uint8_t  getDeauthJitterMax()     { return Config::wifi().deauthJitterMax; }
    static uint16_t getChannelHopInterval()  { return Config::wifi().channelHopInterval; }
    static uint32_t getLockTime()            { return Config::wifi().lockTime; }
};
