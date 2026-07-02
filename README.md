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
internal storage) · connected WiFi SSID (green) · battery %. The version
(`vX.Y.Z`) is shown on the boot splash and on the Options *Update FW* row.

## Menu
- **CAPTURE** — channel-hops, auto-targets eligible APs, sends deauth bursts,
  captures EAPOL M1–M4 + PMKID, and auto-saves to storage. Live stats: channel,
  networks, packets, handshakes, PMKIDs, deauths, current target + clients, a
  **`last:`** line (network of the most recent saved capture), and an always-on
  **pool breakdown** — `work` (eligible, being attacked right now) · `cool`
  (waiting out an attack cooldown) · `pmf` (protected) · `done` (captured/ignored)
  · `weak` (below Atk RSSI) · `open` · `idle` (gave up after Max Tries). Each new
  capture **beeps** (I2S amp) and flashes the onboard LED green. Excluded networks
  (see CAPTURES) are neither attacked **nor** passively saved. Side button stops,
  restores WiFi and exits — and on exit, if you captured something new this session
  and an **ntfy** topic is set, it pushes an alert to your phone (with the latest
  capture file attached when `Ntfy File` is on). The push happens on exit, not
  mid-capture, because capture runs promiscuous (no STA uplink).
- **CAPTURE TARGETED** — scan, pick one AP, and capture **only that BSSID** (the
  engine locks to it and ignores everything else, even already-captured/excluded
  ones, and ignores the Max-Tries give-up); the Capture header shows `TGT <ssid>`.
  Back to exit; the normal CAPTURE item clears the lock.
- **WPASEC SYNC** — lists `.pcap` captures with status tags (`CRK`/`UP`/`-`),
  counts, and free storage. **SYNC** connects WiFi STA, **bulk-uploads** pending
  captures, and downloads the cracked potfile (with optional *Purge Crk*, below).
  Click a capture for a detail view (SSID/BSSID, status, recovered password) and
  delete. Also has a **WIFI SCAN** diagnostic.
- **OHC SYNC** — lists `.22000` captures with the same status tags/counts and free
  storage. Uploads are **per-capture** (not bulk — avoids re-submitting the whole
  set and flooding OHC with duplicates): click a capture → a detail view with an
  **UPLOAD TO OHC / DELETE** selector. Crack status is shared from the wpa-sec
  potfile (OHC's API masks hashes, so per-file results can't be read back from it).
- **CAPTURES** — the persistent network **registry**, and the single source of
  capture exclusion. It holds every **captured** network (`C`, registered on save —
  so it stays listed and excluded **even after you delete the `.pcap`/`.22000`**)
  plus **manual** never-attack entries (`M`, add via *ADD IGNORE → scan → pick*).
  Exclusion matches **by SSID name**, not just BSSID — ignoring one AP ignores
  every radio broadcasting that name (mesh nodes, extenders, dual-radio routers).
  Click a network for detail: SSID, BSSID, type, **seen-time** (NTP), the
  **recovered password** if cracked, and a **SHOW WIFI QR** action for cracked
  networks (renders a `WIFI:` join QR you scan with a phone). **DELETE = forget**
  (re-capturable again). Rows show `C`/`M` (captured/manual), `W`/`O` (uploaded to
  wpa-sec / OHC), `K` (cracked). Backed by an extended boar-bros file
  (`BSSID,flags,ts,SSID`).
- **STATS** — current inventory: `.pcap` / `.22000` counts on storage, cracked
  count (from the wpa-sec potfile), storage backend + free space.
- **OPTIONS** — see below.
- **REBOOT** / **POWER OFF** — soft restart / deep sleep (wake via any button).
  POWER OFF is also triggered by **holding the side (BACK) button ~3 s** from
  anywhere. The backlight **auto-dims after 30 s idle** and restores on any input.

The **capture detail view** (on the sync screens) also shows a hash-quality line:
for a `.22000` it verifies a crackable `WPA*02*` (EAPOL) / `WPA*01*` (PMKID) line
is present; `.pcap` is validated server-side on upload.

## Options
- **WiFi** — guided flow: scan → pick an SSID → enter password → save + connect.
- **WPA Key** (wpa-sec, 32 hex) · **OHC Key** (`sk_…`).
- **Ntfy Topic** (ntfy.sh push topic; empty = off, default `capture_alert`) ·
  **Ntfy File** (attach the capture file to the push).
- Oink tuning: **Ch Hop ms**, **Lock ms**, **Atk RSSI**, **Deauth** on/off,
  **Rnd MAC** on/off, **Burst**, **Jitter ms**, **Max Tries** (attempts per
  network before giving up, 1–15), **Idle Retry** (re-arm a given-up network
  after N minutes without waiting for it to age out and reappear; 0 = off).
- **Brightness** · **Sound** on/off.
- **Purge Crk** — after a WPA-SEC sync, delete local `.pcap`/`.22000` files for
  networks that are now cracked (the password stays in the potfile cache + the
  CAPTURES registry).
- **OTA Path** — SD path the launcher loads / the SD self-update writes to.
- **Update FW** / **Update→SD** — self-update over WiFi (see below).

Settings are saved on exit to a versioned, size-tolerant config blob.

## Firmware updates (OTA)
BBoink can update itself over WiFi from its GitHub releases — no cable. Two paths,
because it can run two ways:

- **Update FW** (*flash OTA*, for a **standalone** install flashed at 0x0) —
  downloads the app image straight into the spare OTA partition (`ota_0`/`ota_1`),
  validates the image (magic + checksum), marks it bootable, and reboots into it.
  No SD, no launcher, no PC.
- **Update→SD** (for a **launcher** install) — downloads the app image to the
  configured **OTA Path** on the SD card and reboots so the launcher loads it.

Both pull the **app-only** asset (`bboink-app-t-embed-cc1101.bin`) from the
latest release. A launcher must be given the `-app-` image (not the merged 0x0
image), and its app slot must be ≥ ~1.3 MB.

## Releases / CI
GitHub Actions (`.github/workflows/build.yml`) builds on every push and, on a
`vX.Y.Z` tag, publishes a Release with two raw assets:

| Asset | Use |
|---|---|
| `bboink-t-embed-cc1101.bin` | merged image, full flash at `0x0` (esptool) |
| `bboink-app-t-embed-cc1101.bin` | app-only image, for a launcher / the self-updater |

Release flow: bump `BBOINK_VERSION` in `src/version.h`, `git tag vX.Y.Z`, push the
tag — CI builds and attaches both assets, and every device sees it via *Update FW*.

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
lists only the file type it can use. Note: wpa-sec's results only list **cracked**
handshakes — a freshly uploaded pcap won't "appear" until it's cracked.

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
`partitions.csv` (two 3 MB `ota_0`/`ota_1` app slots for flash OTA; the `littlefs`
partition fills the top of flash). Serial is the ESP32-S3 native USB-Serial-JTAG,
which re-enumerates on reset.

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
The `src/app/` UI, the OnlineHashCrack client (`src/web/ohc.*`), the ntfy client
(`src/web/ntfy.*`) and the self-updater (`src/web/updater.*`) are new.
`SwineStats` getters route the oink tuning straight from `Config`, so Options
settings drive the engine.
