# Getting started

A step-by-step setup for BBoink, from a blank board to cracked passwords. Read the
**common setup** first, then jump to the track for your board:

- **Track A — Button boards** (LilyGo T-Embed CC1101 / T-Display C5): manage on-device
  with the buttons + menu, *or* over Bluetooth.
- **Track B — BLE-only board** (Waveshare ESP32-C5-LCD-1.47): one button; everything is
  done from the phone console over Bluetooth.

> New to the project? Skim the [README](../README.md) for what BBoink does and which
> board is which. ⚠️ Deauth frames are transmitted — only use on networks you own or are
> authorized to test.

---

## What you'll need

**Hardware**
- One [supported board](../README.md#supported-hardware) + a USB-C cable.
- (Optional) a microSD card — captures also persist to internal flash without one.

**Accounts** (all free)
- A **[Render](https://render.com)** account — hosts the relay (one small web service).
- Cracking services — sign up for whichever you want; a key from **any one** is enough:
  - [wpa-sec.stanev.org](https://wpa-sec.stanev.org) — a user *key* (32 hex chars).
  - [OnlineHashCrack](https://onlinehashcrack.com) — an API key (`sk_…`).
  - [PwnCrack.org](https://pwncrack.org) — a key (UUID).

**A phone or computer with a Web Bluetooth browser** (required for the BLE console — the
*only* UI on the Waveshare, optional on the others):
- ✅ Chrome / Chromium / Edge on **Android** or **desktop** (Linux confirmed; Windows/macOS
  should work).
- ❌ **Not** iOS or Safari (no Web Bluetooth).

---

## Common setup (do this once, any board)

### 1 · Flash the firmware

Grab the latest merged image for your board from
[**Releases**](https://github.com/whitewhidow/bboink/releases) and flash it at `0x0`:

| Board | Merged image | Flash command (esptool) |
|---|---|---|
| T-Embed CC1101 | `bboink-t-embed-cc1101.bin` | `esptool --chip esp32s3 write_flash 0x0 bboink-t-embed-cc1101.bin` |
| T-Display C5 | `bboink-tdisplay-c5.bin` | `esptool --chip esp32c5 write_flash 0x0 bboink-tdisplay-c5.bin` |
| Waveshare C5-LCD | `bboink-waveshare-c5-lcd.bin` | `esptool --chip esp32c5 write_flash 0x0 bboink-waveshare-c5-lcd.bin` |

> The C5 chips need **esptool 5.x** (`pip install --upgrade "esptool>=5.1.0"`).
> First flash of a C5 board? Run `esptool erase_flash` once beforehand.

Prefer to build it yourself? See [Build / flash](../README.md#build--flash).

### 2 · Deploy the relay (once)

Captures reach the cracking services through a small **relay** you host, so the board (or
phone) only ever talks to **one** host.

1. In [Render](https://render.com): **New → Blueprint → pick this repo** — it reads
   [`relay/render.yaml`](../relay/render.yaml).
2. Set the one required env var **`RELAY_TOKEN`** to a long random string you invent.
   (`WPASEC_KEY` / `OHC_KEY` / `PWNCRACK_KEY` are **optional** here — you'll normally keep
   the keys on the device instead.)
3. You get a URL like `https://bboink.onrender.com`. Keep it + the token handy.

Details + endpoint reference: [`relay/README.md`](../relay/README.md).

### 3 · Know your config values

Whichever track you follow, you'll enter the same handful of settings. They live **on the
device** (single source of truth) and are read back by whatever UI you use:

| Setting | Value | Needed for |
|---|---|---|
| **Relay URL** | your Render URL, e.g. `https://bboink.onrender.com` | relay sync |
| **Relay token** | the `RELAY_TOKEN` you chose | relay sync |
| **wpa-sec key** | wpa-sec user key | cracking |
| **OHC key** | OnlineHashCrack `sk_…` key | cracking |
| **PwnCrack key** | PwnCrack UUID key | cracking |
| **WiFi SSID / pass** | a network the *board* can join | **only** Track-A direct sync |
| capture tuning | Ch Hop, Deauth, PMKID, Atk RSSI, Max Tries | capture behaviour |

You only need **one** cracking key to start. WiFi creds are optional — the relay path uses
your *phone's* connection, so the board needs no WiFi of its own for it.

---

## Track A — Button boards (T-Embed CC1101 / T-Display C5)

These boards have a screen + buttons and an on-device menu. You can do **everything** with
the buttons, and *also* hand off to the phone console when you prefer.

### A1 · Configure with the buttons

1. Power on. From the capture screen press **back** to open the **MENU**.
2. Go to **OPTIONS** and enter the config values from
   [step 3](#3--know-your-config-values): Relay URL, Relay token, and at least one service
   key. (Add **WiFi SSID / pass** only if you'll use direct sync in A3.)
3. Changes save on the device immediately.

> Prefer typing on a phone keyboard? Skip to
> [Track B's console steps](#b2--connect-the-console) — the console's **Config** tab edits
> the exact same settings and works on these boards too.

### A2 · Capture

- **MENU → CAPTURE** — channel-hop + deauth + capture everything in range.
- **MENU → CAPTURE TARGETED** — lock onto one AP.
- Captures land in **MENU → CAPTURES** (and persist across reboots).

### A3 · Sync — pick one path

**Via the phone console (recommended — no board WiFi needed):**
1. **MENU → BLE BRIDGE**. The screen shows `BLE BRIDGE` + the board name `BBoink-XXXX`.
2. Follow [B2–B4](#b2--connect-the-console) below to connect and sync.

**Direct over the board's own WiFi** (needs WiFi creds set in A1):
- **MENU → WPASEC / OHC / PWNCRACK SYNC** — the board uploads over its own WiFi. No phone,
  no relay involved.

---

## Track B — BLE-only board (Waveshare ESP32-C5-LCD-1.47)

One button, no menu, no WiFi AP. All config **and** sync happen in the phone console over
Bluetooth. The button does two things:

- **Tap** = toggle **CAPTURE ⇄ BLE BRIDGE** (entering the bridge reboots the board, ~15 s).
- **Hold ~3 s** = power off.

### B1 · Enter BLE bridge

From the capture screen, **tap** the button. The board reboots into **BLE BRIDGE** and the
screen shows the board name `BBoink-XXXX`, the console URL, and the relay URL (red
`(not set)` until you configure it in B3).

### B2 · Connect the console

1. On your phone/computer, open **https://whitewhidow.github.io/bboink/bridge/** in a
   [Web Bluetooth browser](#what-youll-need).
2. Tap **Connect to board** and pick **`BBoink-XXXX`** from the chooser.
3. You get three tabs: **Config**, **Captures**, **Sync**.

### B3 · Configure (Config tab)

Enter the values from [step 3](#3--know-your-config-values): **Relay URL**, **Relay
token**, and at least one **service key**. Tap **Save** — they're written to the device.

### B4 · Sync (Sync tab)

1. **Check / wake relay** — pre-warms Render (free tier sleeps after ~15 min idle; first
   wake takes ~30–50 s).
2. **Sync captures ↔ relay** — uploads new captures and pulls back any cracked passwords.
   Results print **per network** (OHC / PwnCrack / wpa-sec).
3. Cracked passwords appear on the **Captures** tab (with a join QR on-device).

> Already-synced captures are skipped (`nothing new to upload`) — tick **re-sync
> everything** to force a full re-upload.

The **Captures** tab also lets you view captures, delete one, and **exclude** a network by
SSID so it's never attacked.

---

## The capture → crack loop (both tracks)

1. **Capture** handshakes/PMKIDs (Track A menu, or Track B tap into capture).
2. **Sync** to the relay (console **Sync**, or Track-A direct sync).
3. The relay fans out to wpa-sec / OHC / PwnCrack and **merges** results.
4. **Cracked passwords** are written back to the board and shown on **Captures** with a
   join QR. A network cracked by several services lists all of them (`wpa-sec+pwncrack`).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Console won't connect / no chooser | You need a Web Bluetooth browser (Chrome/Edge, **not** iOS/Safari). Make sure the board says `BLE BRIDGE`. |
| Board name not in the chooser | Board must be **in BLE BRIDGE** (Track A: menu → BLE BRIDGE; Track B: tap). Only one central can connect at a time. |
| `relay (not set)` in red | Set **Relay URL** in Config/Options. |
| Sync says relay unreachable | Hit **Check / wake relay** and wait ~30–50 s for Render's free tier to wake, then retry. |
| Nothing uploads | You've already synced those captures — tick **re-sync everything**, or capture new ones. |
| C5 board won't flash / bad image | Use **esptool 5.x**, and `esptool erase_flash` once before the first flash. |

More background: [`docs/DESIGN-ble-bridge.md`](DESIGN-ble-bridge.md) ·
[relay reference](../relay/README.md) · [README](../README.md).
