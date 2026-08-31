# BBoink

Minimal WiFi **handshake / PMKID capture** firmware. It channel-hops, deauths eligible
APs, captures EAPOL 4-way handshakes + PMKIDs, and gets them cracked in the cloud
(wpa-sec / OnlineHashCrack / PwnCrack) — then shows the recovered password + a join QR.

### 👉 [**Getting-started guide →**](docs/GETTING-STARTED.md)
Step-by-step setup. Have a button board and just want it working? It's a 4-step path with
no relay and no phone.

Supported boards (one codebase, one build env each):

| Board | Chip | Buttons | How you manage it |
|---|---|---|---|
| **LilyGo T-Embed CC1101 / PLUS** | ESP32-S3 | encoder + side | **on-device MENU** (buttons) — *or* the BLE portal |
| **LilyGo T-Display C5** | ESP32-C5 (dual-band) | 2 | **on-device MENU** (buttons) — *or* the BLE portal |
| **Waveshare ESP32-C5-LCD-1.47** | ESP32-C5 (dual-band) | **1** | **BLE portal only** (no menu, no AP) |

> **No board runs a WiFi access point or web server** — that whole stack is compiled
> out. You manage the device one of two ways: with its **buttons + on-screen menu**
> (T-Embed / T-Display), or entirely over **Bluetooth from your phone** (all boards; the
> *only* way on the single-button Waveshare). See `docs/DESIGN-ble-bridge.md`.

> ⚠️ Deauth frames are transmitted to force handshakes. Use only on networks you own or
> are explicitly authorized to test.

## Supported hardware

Three boards, one firmware. Buy links below are the official vendor stores; the wiki/docs
links are the manufacturer's own hardware documentation.

<table>
<tr>
<td width="33%" valign="top" align="center">

<img src="https://lilygo.cc/cdn/shop/files/T-EMBED-CC1101-PLUS_6_4bda35c0-79ad-41ba-9aa7-52792d723ab1.jpg?v=1755075224" width="240" alt="LilyGo T-Embed CC1101"><br>

**LilyGo T-Embed CC1101 / PLUS**<br>
ESP32-S3 · 16 MB · encoder + button<br>
_buttons + on-device menu (or BLE)_

[Buy](https://lilygo.cc/products/t-embed-cc1101-plus) ·
[Wiki](https://wiki.lilygo.cc/products/t-embed-series/t-embed-cc1101/) ·
[GitHub](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101)

</td>
<td width="33%" valign="top" align="center">

<img src="https://lilygo.cc/cdn/shop/files/LILYGO-T-DISPLAY-C5_10.jpg?v=1783057404" width="240" alt="LilyGo T-Display C5"><br>

**LilyGo T-Display C5**<br>
ESP32-C5 (dual-band) · 2 buttons<br>
_buttons + on-device menu (or BLE)_

[Buy](https://lilygo.cc/en-us/products/t-display-c5) ·
[Wiki](https://wiki.lilygo.cc/products/t-display-series/t-display-c5/quick-start.html)

</td>
<td width="33%" valign="top" align="center">

<img src="https://raw.githubusercontent.com/waveshareteam/esp32-c5-lcd-1.47/main/assets/Product-1.webp" width="240" alt="Waveshare ESP32-C5-LCD-1.47"><br>

**Waveshare ESP32-C5-LCD-1.47**<br>
ESP32-C5 (dual-band) · **1 button**<br>
_BLE portal only (no menu, no AP)_

[Buy](https://www.waveshare.com/esp32-c5-lcd-1.47.htm) ·
[Docs](https://docs.waveshare.com/ESP32-C5-LCD-1.47) ·
[GitHub](https://github.com/waveshareteam/esp32-c5-lcd-1.47)

</td>
</tr>
</table>

## Driving each board

**Button boards (T-Embed / T-Display).** From the capture screen, **back** opens the
**MENU**:
`CAPTURE` · `CAPTURE TARGETED` · `BLE BRIDGE` · `WPASEC / OHC / PWNCRACK SYNC` (direct
upload over the *board's own* WiFi) · `CAPTURES` · `STATS` · `OPTIONS` (edit all config
with the buttons) · `REBOOT` / `POWER OFF`. Choose **BLE BRIDGE** to hand off to the
phone portal instead of using WiFi.

**BLE-only board (Waveshare).** One button: **tap** = CAPTURE ⇄ **BLE BRIDGE** (it
reboots into bridge, ~15 s), **hold ~3 s** = power off. All config + sync are done in the
phone portal.

**The BLE portal** — the web page (nothing to install) that is the *entire* UI on the Waveshare and an
option on the others. Needs a **Web Bluetooth** browser — Chrome/Chromium/Edge on **Android or desktop** (Linux confirmed; Windows/macOS should work). **Not** iOS/Safari. Open
**https://whitewhidow.github.io/bboink/bridge/**, tap **Connect to board**, pick
`BBoink-XXXX`. Three tabs:
- **Config** — all device settings (below).
- **Captures** — the registry: view captures, see **cracked passwords**, **delete** a
  capture, and **exclude** a network by SSID (never-attack).
- **Sync** — **Check / wake relay**, then **Sync captures ↔ relay** (per-network results). Or
  **Download hashes (.hc22000)** to crack them yourself offline (`hashcat -m 22000`) — no relay,
  no account.

## Configuration — what you must set

Set these **once**. They live on the **device** (the single source of truth) and are read
back by whichever UI you use — buttons **Options** *or* the BLE portal **Config** tab.

| Setting | Value | Needed for |
|---|---|---|
| **Relay URL** | your Render URL, e.g. `https://bboink.onrender.com` | relay sync |
| **Relay token** | the `RELAY_TOKEN` you chose on Render | relay sync |
| **wpa-sec key** | wpa-sec.stanev.org user key (32 hex) | cracking |
| **OHC key** | OnlineHashCrack API key (`sk_…`) | cracking |
| **PwnCrack key** | PwnCrack.org key (UUID) | cracking |
| **WiFi SSID / pass** | a network the *board itself* can join | **only** for the direct menu sync |
| capture tuning | Ch Hop, Deauth, PMKID, Atk RSSI, Max Tries, … | capture behaviour |

The three **service keys are stored only on the device**; the BLE portal passes them to
the relay per-sync (an `X-Keys` header), so the relay's own env-var keys are just an
optional fallback. **WiFi creds are needed only for a button board's *direct* sync** — the
relay path uses your *phone's* connection, so the board needs no WiFi for it.

## Cracking sync — the relay

**Supported cloud crackers** — bring a free key from **any one** (a network cracked by several
shows all of them):
- [**wpa-sec**](https://wpa-sec.stanev.org) — Stanev's distributed WPA cracker; uploads raw `.pcap`, accepts duplicates.
- [**OnlineHashCrack**](https://onlinehashcrack.com) — GPU service with a free tier + API; submits WPA `22000` hashes.
- [**PwnCrack**](https://pwncrack.org) — uploads `hc22000` and reads back the cracked potfile.

Captures reach these services through a tiny **relay** you host once, so the board (or
phone) only ever talks to **one host**.

```
 phone/board ──►  relay (Render)  ──►  wpa-sec / OnlineHashCrack / PwnCrack
      ▲                            │
      └────  cracked passwords  ◄──┘
```

### 1 · Deploy the relay (once)
[`relay/`](relay/) is a ~60-line Node/Express service. Deploy to **Render** (free tier):
New → **Blueprint** → pick this repo → it reads `relay/render.yaml`. Set env var
**`RELAY_TOKEN`** (a long random string you invent). The service keys can live on the
device instead of Render, so `WPASEC_KEY` / `OHC_KEY` / `PWNCRACK_KEY` are **optional**
env fallbacks. You get a URL like `https://bboink.onrender.com`. Endpoint reference:
[`relay/README.md`](relay/README.md).

### 2 · Configure the device
Set **Relay URL**, **Relay token**, and the **three service keys** (table above).

### 3 · Sync
- **BLE portal (any board):** enter **BLE BRIDGE** (Waveshare: tap; T-Embed/T-Display:
  menu → *BLE BRIDGE*) → on the phone open the portal URL → **Connect** → **Sync** tab →
  **Check / wake relay** → **Sync captures ↔ relay**. Results print **per network**
  (OHC / PwnCrack / wpa-sec). Cracked passwords are written back to the board and shown
  on **Captures**. Already-synced captures are skipped (`nothing new to upload`); tick
  **re-sync everything** to force.
- **Direct (button boards only):** menu → **WPASEC / OHC / PWNCRACK SYNC** uploads over
  the board's own WiFi (needs WiFi creds set). No phone/relay involved.

### Notes
- Render free tier **sleeps after ~15 min idle** → the first request wakes it (~30–50 s);
  the portal's **Check / wake relay** button pre-warms it.
- wpa-sec **accepts duplicate** uploads, so the board tracks what it has synced and sends
  only new captures. OHC / PwnCrack dedup server-side.
- A network cracked by multiple services shows all of them (`wpa-sec+pwncrack`).

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
  (see CAPTURES) are neither attacked **nor** passively saved. Side button (back)
  stops capture and opens the **MENU**.
- **CAPTURE TARGETED** — scan, pick one AP, and capture **only that BSSID** (the
  engine locks to it and ignores everything else, even already-captured/excluded
  ones, and ignores the Max-Tries give-up); the Capture header shows `TGT <ssid>`.
  Back to exit; the normal CAPTURE item clears the lock.
- **BLE BRIDGE** — hand off to the phone: the board becomes a BLE peripheral (WiFi off)
  and the **BLE portal** does all config + relay sync. Back / tap returns to capture.
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
- **PWNCRACK SYNC** — lists `.22000` captures for [PwnCrack.org](https://pwncrack.org/).
  Per-capture upload (detail → **UPLOAD TO PWNCRACK / DELETE**) plus a **SYNC
  POTFILE** row that downloads PwnCrack's cracked potfile so its status tags and
  passwords populate. Folded into the unified crack status.
- **CAPTURES** — the persistent network **registry**, and the single source of
  capture exclusion. It holds every **captured** network (`C`, registered on save —
  so it stays listed and excluded **even after you delete the `.pcap`/`.22000`**)
  plus **manual** never-attack entries (`M`, add via *ADD IGNORE → scan → pick*).
  Exclusion matches **by SSID name**, not just BSSID — ignoring one AP ignores
  every radio broadcasting that name (mesh nodes, extenders, dual-radio routers).
  Click a network for detail: SSID, BSSID, type, **seen-time** (NTP), the
  **recovered password** if cracked, and a **SHOW WIFI QR** action for cracked
  networks (renders a `WIFI:` join QR you scan with a phone). **DELETE = forget**
  (re-capturable again). Rows show `C`/`M` (captured/manual), `W`/`O`/`P` (uploaded
  to wpa-sec / OHC / PwnCrack), `K` (cracked, any service). Backed by an extended boar-bros file
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
- **WPA Key** (wpa-sec, 32 hex) · **OHC Key** (`sk_…`) · **PWN Key** (PwnCrack UUID).
- Oink tuning: **Ch Hop ms**, **Lock ms**, **Atk RSSI**, **Deauth** on/off,
  **Rnd MAC** on/off, **Burst**, **Jitter ms**, **Max Tries** (attempts per
  network before giving up, 1–15), **Idle Retry** (re-arm a given-up network
  after N minutes without waiting for it to age out and reappear; 0 = off).
- **Brightness** · **Sound** on/off.
- **Purge Crk** — after a WPA-SEC sync, delete local `.pcap`/`.22000` files for
  networks that are now cracked (the password stays in the potfile cache + the
  CAPTURES registry).
- **Crk Uplink** — if the configured WiFi isn't reachable when syncing, fall back
  to an in-range **cracked** network (using its recovered password) for the upload.
  Opt-in (default off); only use on networks you're authorized on.
- **OTA Path** — SD path the launcher loads / the SD self-update writes to.
- **Update FW** / **Update→SD** — self-update over WiFi (see below).

Settings are saved on exit to a versioned, size-tolerant config blob.

## Firmware updates

> ⚡ **[Web flasher](https://whitewhidow.github.io/bboink/flasher/)** — flash any board over
> USB straight from the browser (Chrome/Edge desktop), no esptool needed. Pick the board +
> version and click Install. This is the easiest way to install or update the **Waveshare**.

How a board updates depends on whether it has the menu **and** a two-slot (A/B) partition:

- **Button boards (T-Embed / T-Display)** — update **over WiFi, no cable**, from the GitHub
  releases, under **Options → Update FW**. Two paths, because they can run two ways:
  - **Update FW** (*flash OTA*, for a **standalone** install flashed at 0x0) — downloads the
    app image straight into the spare OTA partition (`ota_0`/`ota_1`), validates it
    (magic + checksum), marks it bootable, and reboots into it. No SD, no launcher, no PC.
  - **Update→SD** (for a **launcher** install) — downloads the app image to the configured
    **OTA Path** on the SD card and reboots so the launcher loads it.

  Both pull the **app-only** asset for that board (`bboink-app-<env>.bin`). A launcher must
  be given the `-app-` image (not the merged 0x0 image), and its app slot must be ≥ ~1.3 MB.

- **Waveshare ESP32-C5-LCD (BLE-only)** — **no on-device OTA.** Its 4 MB flash holds a
  *single* app slot, and on-device OTA fundamentally needs a **second** slot to write into
  (you can't overwrite the running app) — two ~1.8 MB slots don't fit in 4 MB. It also has
  no menu to trigger one. So you update it by **reflashing the merged image over USB** —
  easiest via the **[web flasher](https://whitewhidow.github.io/bboink/flasher/)** (pick
  *Waveshare* → Install), or by hand:

  ```
  esptool --chip esp32c5 write_flash 0x0 bboink-waveshare-c5-lcd.bin
  ```

## Releases / CI
GitHub Actions (`.github/workflows/build.yml`) builds **all three boards** on every push to
`main` and, on a `vX.Y.Z` tag, publishes a Release with, **per board env**, two raw assets:

| Asset | Use |
|---|---|
| `bboink-<env>.bin` | merged image, full flash at `0x0` (esptool) — **all** boards |
| `bboink-app-<env>.bin` | app-only image, for button-board OTA / a launcher |

`<env>` is `t-embed-cc1101`, `tdisplay-c5`, or `waveshare-c5-lcd`.

Release flow: bump `BBOINK_VERSION` in `src/version.h`, `git tag vX.Y.Z`, push the tag — CI
builds and attaches all assets. Button boards then see it via *Update FW*; the Waveshare is
reflashed over USB.

## Controls

**T-Embed CC1101 — rotary encoder + side button:**
- **Turn** the encoder = move / adjust
- **Click** (push the encoder) = select / confirm
- **Side button** = back / stop; **hold ~3 s** = power off

**T-Display C5 — two buttons** (tap vs hold is decided on release):
- **Up button:** **tap** = move up · **hold (~0.5 s)** = select / confirm
- **Down button:** **tap** = move down · **hold (~0.5–3 s)** = back · **hold (~3 s)** = power off
- If up/down feel reversed on your unit, the two buttons are symmetric — just swap them in your head.

Text fields (SSID / passwords / keys) use the same on-screen **char-picker**: up/down change the
character, select confirms it, back deletes. (On the button-less Waveshare there's no on-device
input — do all config from the [BLE portal](https://whitewhidow.github.io/bboink/bridge/).)

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
# build envs: t-embed-cc1101 (S3) · tdisplay-c5 · waveshare-c5-lcd
~/.local/bin/pio run -e t-embed-cc1101                          # build
~/.local/bin/pio run -e t-embed-cc1101 -t upload                # flash (S3 / T-Display)
```
The **Waveshare C5** is flashed with esptool (bootloader @ `0x2000`); the **T-Display
C5** needs `--no-stub`/115200 (its env sets this) and a **clean power-cycle** to run the
app after flashing. All three envs build from the same source; the AP/web + menu screens
are excluded per-board via `build_src_filter`.
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
