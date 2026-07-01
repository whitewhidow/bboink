# BBoink

Minimal WiFi **handshake / PMKID capture** firmware for the **LilyGo T-Embed
CC1101** and **CC1101 PLUS** (ESP32-S3, rotary encoder + side button, ST7789
320×170). It captures EAPOL handshakes and PMKIDs over the air and uploads them
for cloud cracking:

- **`.pcap`** → [wpa-sec.stanev.org](https://wpa-sec.stanev.org/)
- **`.22000`** → [OnlineHashCrack](https://www.onlinehashcrack.com/) (hashcat mode 22000)

It does only the "oink" (capture) part of M5PORKCHOP. The on-board CC1101 sub-GHz
radio is **not** used (but its chip-select is managed — see *SD card* below).

> ⚠️ Deauthentication frames are transmitted to force handshakes. Use only on
> networks you own or are explicitly authorized to test.

## Top bar
Every screen shows: title · **SD** (green = microSD mounted, red `sd` = running on
internal storage) · connected WiFi SSID (green) · battery %.

## Menu
- **CAPTURE** — channel-hops, auto-targets eligible APs, sends deauth bursts,
  captures EAPOL M1–M4 + PMKID, and auto-saves to storage. Live stats (channel,
  networks, packets, handshakes, PMKIDs, deauths, current target + clients) plus a
  **`last:`** line showing the network of the most recent saved capture. **Already
  captured networks are skipped** — both within the session and any with a
  handshake already saved on the card. Each new capture **beeps** (I2S amp) and
  flashes the onboard LED green. Side button stops, restores WiFi and exits — and
  on exit, if an **ntfy** topic is set, pushes an alert to your phone (with the
  latest capture file attached when `Ntfy File` is on). The push happens on exit,
  not mid-capture, because capture runs promiscuous (no STA uplink).
- **WPASEC SYNC** — lists `.pcap` captures with status tags (`CRK`/`UP`/`-`),
  counts, and free storage. **SYNC** connects WiFi STA, uploads pending captures,
  and downloads the cracked potfile. Click a capture for a detail view (SSID/BSSID,
  status, recovered password) and delete. Also: **MAKE TEST CAP** (generates a
  real, crackable WPA2 handshake for end-to-end testing) and **WIFI SCAN**.
- **OHC SYNC** — lists `.22000` captures with the same status tags/counts and free
  storage. **SYNC** submits the WPA hashes to OnlineHashCrack (API v2, algo 22000).
  Same per-capture detail/delete view. Crack status is shared from the wpa-sec
  potfile (OHC's API masks hashes, so per-file results can't be read back from it).
- **SYNC ALL** — runs the WPA-SEC sync then the OnlineHashCrack upload back-to-back
  and shows a combined result (no need to visit both screens).
- **OPTIONS** — `WiFi` (a guided flow: scan → pick an SSID from the list → enter
  the password → saves and connects), `WPA Key` (wpa-sec, 32 hex), `OHC Key`
  (`sk_…`), and oink tuning: `Ch Hop ms`, `Lock ms`, `Atk RSSI`, `Deauth` on/off,
  `Ntfy Topic` (ntfy.sh push topic for capture alerts; empty = off, default
  `capture_alert`), `Ntfy File` on/off (attach the capture file to the push),
  `Rnd MAC` on/off, `Burst`, `Jitter ms`, `Brightness`, `Sound` on/off. Saved on exit.
- **REBOOT** / **POWER OFF** — soft restart / deep sleep (wake via any button).
  POWER OFF is also triggered by **holding the side (BACK) button ~3 s** from
  anywhere. The backlight **auto-dims after 30 s idle** and restores on any input.

## Controls
Rotary encoder turn = move / adjust, click = select / confirm, side button =
back / stop. Text fields (SSID / passwords / keys) use the on-screen encoder
char-picker.

## Capture files
Each capture's type is in its filename so the two upload paths stay separate and
files are distinguishable off-device:

| Source | wpa-sec (`.pcap`) | OnlineHashCrack (`.22000`) |
|---|---|---|
| Handshake | `SSID_BSSID_pcap.pcap` | `SSID_BSSID_22000.22000` |
| PMKID | — | `SSID_BSSID_pmkid.22000` |

wpa-sec ingests packet captures only; OHC takes hashcat `.22000`. Each screen
lists only the file type it can use.

## Storage (no SD card required)
Hybrid backend (`src/core/storage.h`): mounts the **microSD if present**, else
falls back to an **internal ~8.4 MB LittleFS** partition (format-on-mount), so
capture + upload work with or without a card. All capture/upload I/O goes through
`Storage::fs()`.

**Config** (WiFi creds, wpa-sec/OHC keys, tuning) is written to the **SD card**
(authoritative) and mirrored to internal SPIFFS; it is loaded **SD-first**. So a
card-resident config **survives flashing other firmware and flashing BBoink back**.
No credentials are hardcoded — set them on-device via Options (or drop a wpa-sec
key file on the SD).

## SD card on the shared SPI bus
On these boards the **SD, ST7789 display and CC1101 all share one SPI bus**
(`SCLK 11 / MOSI 9 / MISO 10`; SD CS 13, CC1101 CS 12, TFT CS 41). To mount the SD
reliably the firmware:
1. drives the **CC1101 and TFT chip-selects high** (deselect) before SD access;
2. holds the **ST7789 in reset** during the boot-time mount (the un-initialised
   panel otherwise loads the shared bus), then the display driver re-inits it;
3. retries at progressive SPI speeds, and retries **once more after the display is
   up** (`Config::mountSdAfterDisplay`).

This mirrors LilyGo's factory `board_spi_deselect_all()` + shared-bus approach.

## Build / flash
PlatformIO (here installed via pipx, not on `PATH`):
```bash
~/.local/bin/pio run -e t-embed-cc1101                          # build
~/.local/bin/pio run -e t-embed-cc1101 -t upload                # flash
~/.local/bin/pio device monitor -e t-embed-cc1101 -b 115200     # serial (needs a TTY)
```
Selected by `-DPORK_BOARD_TEMBED_CC1101`; 16 MB flash, partition table in
`partitions.csv` (the `littlefs` partition fills the top of flash). Serial is the
ESP32-S3 native USB-Serial-JTAG, which re-enumerates on reset.

## Boot order note
WiFi is associated **before** the display initialises: LovyanGFX's SPI init grabs
a shared GDMA resource that a *fresh* WiFi association also needs, so a
display-first order makes every later STA connect fail (reason 2 / AUTH_EXPIRE).
BBoink brings WiFi up at boot, keeps it alive, then inits the display; uploads
reuse the live connection.

## Provenance
The capture engine (`src/modes/oink.*`, `src/core/network_recon.*`,
`src/core/wsl_bypasser.*`), wpa-sec uploader (`src/web/wpasec.*`), heap helpers,
config and the T-Embed HAL (`src/hal/`) are lifted essentially verbatim from
M5PORKCHOP's `tembed-port` branch. The porkchop gamification/graphics systems
(Mood/Avatar/XP/SwineStats/Display/SDLog/Warhog) are replaced by no-op stub
headers at their original include paths, so the copied engine needs no edits.
The `src/app/` UI and the OnlineHashCrack client (`src/web/ohc.*`) are new.
`SwineStats` getters route the oink tuning straight from `Config`, so Options
settings drive the engine.
