// Firmware-switch targets: the OTHER apps' latest-release app-bin for THIS board.
// The switch mesh is the two S3 16MB boards (T-Embed, T-Dongle), which share an
// identical A/B partition table with the PoC and BBportal. Empty elsewhere.
// A gitignored core/dev_secrets.h may define DEV_OTHER_FW_URL to override target 0.
#pragma once

struct SwitchTarget { const char* name; const char* url; };

#define GH_ "https://github.com/whitewhidow/"

#if defined(PORK_BOARD_TDONGLE_S3)
static const SwitchTarget SWITCH_TARGETS[] = {
#if defined(DEV_OTHER_FW_URL)
  { "PoC",      DEV_OTHER_FW_URL },
#else
  { "PoC",      GH_ "hid-ble-poc/releases/latest/download/hid-ble-poc-app-tdongle.bin" },
#endif
  { "BBportal", GH_ "bb-portal/releases/latest/download/bb-portal-app-tdongle-s3.bin" },
};
static const int SWITCH_TARGET_COUNT = (int)(sizeof(SWITCH_TARGETS) / sizeof(SWITCH_TARGETS[0]));
#elif defined(PORK_BOARD_TEMBED_CC1101)
static const SwitchTarget SWITCH_TARGETS[] = {
#if defined(DEV_OTHER_FW_URL)
  { "PoC",      DEV_OTHER_FW_URL },
#else
  { "PoC",      GH_ "hid-ble-poc/releases/latest/download/hid-ble-poc-app-tembed.bin" },
#endif
  { "BBportal", GH_ "bb-portal/releases/latest/download/bb-portal-app-tembed-cc1101.bin" },
};
static const int SWITCH_TARGET_COUNT = (int)(sizeof(SWITCH_TARGETS) / sizeof(SWITCH_TARGETS[0]));
#else
static const SwitchTarget SWITCH_TARGETS[1] = { { "", "" } };   // not in the mesh on this board
static const int SWITCH_TARGET_COUNT = 0;
#endif

#undef GH_
