// cracks.h — unified cracked-status across all cracking services. A network is
// "cracked" if ANY service (wpa-sec or PwnCrack) has recovered its password; the
// password is taken from whichever has it. OnlineHashCrack masks hashes, so it
// can't report crack status and isn't consulted here.
#pragma once

#include "wpasec.h"
#include "pwncrack.h"

namespace Cracks {

inline void loadAll() { WPASec::loadCache(); PwnCrack::loadCache(); }

inline bool isCracked(const char* bssid) {
    return WPASec::isCracked(bssid) || PwnCrack::isCracked(bssid);
}

inline const char* getPassword(const char* bssid) {
    const char* p = WPASec::getPassword(bssid);
    if (p && p[0]) return p;
    return PwnCrack::getPassword(bssid);
}

} // namespace Cracks
