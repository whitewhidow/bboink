// Stub: XP — gamification removed in tembed-oink.
// oink.cpp only uses XP::getSession() (writes sess.everDeauthed) and a couple of
// XP::addXP() calls. We keep the SessionStats struct + XPEvent enum verbatim so
// the field/enum references resolve, and make every behaviour a no-op.
#pragma once

#include <Arduino.h>

struct SessionStats {
    uint32_t xp;
    uint32_t networks;
    uint32_t handshakes;
    uint32_t deauths;
    uint32_t distanceM;
    uint32_t blePackets;
    uint32_t startTime;
    uint32_t firstNetworkTime;
    bool gpsLockAwarded;
    bool session30Awarded;
    bool session60Awarded;
    bool session120Awarded;
    bool nightOwlAwarded;
    bool session240Awarded;
    bool earlyBirdAwarded;
    bool weekendWarriorAwarded;
    bool rogueSpotterAwarded;
    uint32_t passiveNetworks;
    uint32_t passivePMKIDs;
    uint32_t passiveTimeStart;
    uint32_t boarBrosThisSession;
    uint32_t mercyCount;
    bool everDeauthed;
};

enum class XPEvent : uint8_t {
    NETWORK_FOUND, NETWORK_HIDDEN, NETWORK_WPA3, NETWORK_OPEN, NETWORK_WEP,
    HANDSHAKE_CAPTURED, PMKID_CAPTURED, DEAUTH_SENT, DEAUTH_SUCCESS, WARHOG_LOGGED,
    DISTANCE_KM, BLE_BURST, BLE_APPLE, BLE_ANDROID, BLE_SAMSUNG, BLE_WINDOWS,
    GPS_LOCK, ML_ROGUE_DETECTED, SESSION_30MIN, SESSION_60MIN, SESSION_120MIN,
    LOW_BATTERY_CAPTURE, DNH_NETWORK_PASSIVE, DNH_PMKID_GHOST, BOAR_BRO_ADDED,
    BOAR_BRO_MERCY, SMOKED_BACON
};

class XP {
public:
    static void addXP(XPEvent) {}
    static void processPendingSave() {}
    static const SessionStats& getSession() {
        static SessionStats session = {};
        return session;
    }
};
