# tembed-oink

Minimal WiFi handshake-capture firmware for the **LilyGo T-Embed CC1101**
(ESP32-S3, rotary encoder + side button, ST7789 320×170). Does only the "oink"
part of [M5PORKCHOP](../M5PORKCHOP) — capture EAPOL handshakes / PMKIDs — plus
uploading captures to [wpa-sec.stanev.org](https://wpa-sec.stanev.org/). The
CC1101 sub-GHz radio is not used.

## Menu
- **Capture** — channel-hops, auto-targets APs, sends deauth bursts to force
  handshakes, captures EAPOL M1–M4 + PMKID, auto-saves `.pcap`/`.22000` to SD.
  Live counters; side button stops & exits.
- **Manage** — lists captures on SD; "SYNC TO WPA-SEC" connects WiFi STA and
  uploads pending captures, then downloads the cracked potfile.
- **Options** — WiFi SSID/password (for upload), wpa-sec key (32 hex), and oink
  tuning: channel-hop interval, lock/discovery time, attack min-RSSI, deauth
  on/off, deauth burst count, deauth jitter. Saved to SD on exit.

## Controls
Rotary encoder turn = move/adjust, click = select/confirm, side button = back /
stop. Text fields (SSID/pass/key) use the on-screen encoder char-picker.

## Build / flash
PlatformIO via pipx (not on PATH):
```bash
~/.local/bin/pio run -e t-embed-cc1101            # build
~/.local/bin/pio run -e t-embed-cc1101 -t upload  # flash
~/.local/bin/pio device monitor -b 115200         # serial
```

## Provenance
The capture engine (`src/modes/oink.*`, `src/core/network_recon.*`,
`src/core/wsl_bypasser.*`), the wpa-sec uploader (`src/web/wpasec.*`), the heap
helpers, config, and the T-Embed HAL (`src/hal/`) are lifted essentially verbatim
from M5PORKCHOP's `tembed-port` branch. The porkchop gamification/graphics systems
(Mood/Avatar/XP/SwineStats/Display/SDLog/Warhog) are replaced by no-op stub
headers at their original include paths, so the copied engine needs no edits. The
`src/app/` UI (plain 3-screen menu) is new. `SwineStats` getters route the oink
tuning straight from `Config`, so Options settings drive the engine.

## Storage (no SD card required)
Hybrid backend via `src/core/storage.h`: `Config::init()` mounts the **SD card if
present**, else falls back to an **internal ~8.4 MB LittleFS partition**
(format-on-mount) — so captures + wpa-sec work with or without a card. All
capture/upload file I/O goes through `Storage::fs()`. Personality settings still
use SPIFFS. The LittleFS partition is defined in `partitions.csv` (label
`littlefs`, fills the unused top of the 16 MB flash). LittleFS holds ~a few
thousand handshakes; sync to wpa-sec to clear them.

## ⚠️ Open items
- **Pins unverified.** `src/hal/board.h` pin numbers come from LilyGo/Bruce
  reference designs, not a confirmed schematic of this unit. First flash must
  verify display offset/rotation, encoder direction, and SD CS=13. (Same caveat
  as porkchop's `TEMBED_PORT.md`.) SD shares the display SPI bus.
- Deauth actively transmits — use only on networks you own or are authorized to test.
