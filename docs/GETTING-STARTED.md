# Getting started

Pick the path that matches your board:

- **[Path A — button board, the simple way](#path-a--button-board-the-simple-way)**
  (T-Embed CC1101 / T-Display C5): flash, set one key, capture, sync from the menu.
  **No relay and no phone** — you just need a free key from one cracking service.
- **[Path B — relay + phone console](#path-b--relay--phone-console)**: needed **only** if
  you have the button-less **Waveshare**, or you *want* to drive a button board from your
  phone (per-network results + cracked write-back). This is the part with the Render relay.

> If you have a button board and just want it working, do **Path A** and stop. Path B is an
> optional upgrade — come back to it later if you want it.

⚠️ Deauth frames are transmitted to force handshakes — only use on networks you own or are
authorized to test. New to the project? Skim the [README](../README.md) first.

---

## Flash the firmware (everyone)

**Easiest — the web flasher (no tools to install):** open
**[whitewhidow.github.io/bboink/flasher/](https://whitewhidow.github.io/bboink/flasher/)** in
**Chrome or Edge on desktop**, connect the board with a **USB-C data cable**, pick your board +
version, and click **Install**. First install on a C5 board? Tick **Erase device**; *updating* a
board that already runs BBoink? Leave it unticked to keep your saved config. After a C5 flash,
**unplug/replug** to run the new firmware.

> If a C5 board doesn't appear in the port chooser: use a real **data** cable, and if needed force
> download mode — **hold BOOT, tap RESET, release BOOT** — then Connect.

**By hand (esptool):** grab the latest merged image from
[**Releases**](https://github.com/whitewhidow/bboink/releases) and flash it at `0x0`:

| Board | Merged image | Flash command (esptool) |
|---|---|---|
| T-Embed CC1101 | `bboink-t-embed-cc1101.bin` | `esptool --chip esp32s3 write_flash 0x0 bboink-t-embed-cc1101.bin` |
| T-Display C5 | `bboink-tdisplay-c5.bin` | `esptool --chip esp32c5 write_flash 0x0 bboink-tdisplay-c5.bin` |
| Waveshare C5-LCD | `bboink-waveshare-c5-lcd.bin` | `esptool --chip esp32c5 write_flash 0x0 bboink-waveshare-c5-lcd.bin` |

> The C5 chips need **esptool 5.x** (`pip install --upgrade "esptool>=5.1.0"`), and a one-time
> `esptool erase_flash` before their first flash. Building it yourself?
> See [Build / flash](../README.md#build--flash).

> **Updating later:** button boards update themselves over WiFi (**Options → Update FW**).
> The **Waveshare has no on-device OTA** (single-slot 4 MB flash, no menu) — re-flash it with
> the [web flasher](https://whitewhidow.github.io/bboink/flasher/) (leave *Erase device* off to
> keep your config) or the `write_flash` command above. See
> [Firmware updates](../README.md#firmware-updates).

You'll also want a key from **at least one** cracking service (any one is enough):
[wpa-sec](https://wpa-sec.stanev.org) (32-hex key) ·
[OnlineHashCrack](https://onlinehashcrack.com) (`sk_…`) ·
[PwnCrack](https://pwncrack.org) (UUID).

---

## Path A — button board, the simple way

*T-Embed CC1101 / T-Display C5. No relay, no phone.*

1. **Configure.** Power on; press **back** to open the **MENU → OPTIONS** and set:
   - **WiFi SSID / pass** — a network the board can join (this is how it uploads).
   - at least one **service key** (wpa-sec / OHC / PwnCrack).
   - *(Relay URL/token: leave blank — not used on this path.)*
2. **Capture.** **MENU → CAPTURE** (channel-hop everything in range) or **CAPTURE
   TARGETED** (one AP). Captures show in **MENU → CAPTURES** and survive reboots.
3. **Sync.** **MENU → WPASEC / OHC / PWNCRACK SYNC** — the board uploads directly over its
   own WiFi.
4. **Read results.** Cracked passwords appear on the capture (with a join QR).

That's the whole loop. If you later want phone-driven sync with per-network results and
automatic cracked write-back, add **Path B** on top — it doesn't replace anything here.

---

## Path B — relay + phone console

*Required for the **Waveshare** (it has no menu). Optional for button boards.*

Here the board talks to **one** small web service you host (the **relay**), which fans your
captures out to all three cracking services and merges the results. You drive it from a **web
page you just open in your phone's browser** (the **BLE console** — nothing to install), which
talks to the board over Bluetooth — so the board needs **no WiFi of its own**.

### B1 · Deploy the relay (once)

1. In [Render](https://render.com): **New → Blueprint → pick this repo** — it reads
   [`relay/render.yaml`](../relay/render.yaml).
2. Set the one required env var **`RELAY_TOKEN`** to a long random string you invent.
   (Service keys normally live on the device, so `WPASEC_KEY` / `OHC_KEY` / `PWNCRACK_KEY`
   here are optional fallbacks.)
3. You get a URL like `https://bboink.onrender.com`. Keep it + the token handy.
   Reference: [`relay/README.md`](../relay/README.md).

### B2 · Open the console + connect

You need a **Web Bluetooth browser**: Chrome / Chromium / Edge on **Android or desktop**
(Linux confirmed). ❌ Not iOS / Safari.

1. Put the board in **BLE BRIDGE**:
   - **Waveshare:** **tap** the button (it reboots into the bridge, ~15 s).
   - **Button board:** **MENU → BLE BRIDGE**.
   - The screen shows the board name `BBoink-XXXX`, the console URL to open, and the relay
     URL — on every board (not just the Waveshare).
2. Open **https://whitewhidow.github.io/bboink/bridge/**, tap **Connect to board**, pick
   `BBoink-XXXX`. You get three tabs: **Config**, **Captures**, **Sync**.

### B3 · Configure (Config tab)

Enter **Relay URL**, **Relay token**, and at least one **service key**, then **Save** —
they're written to the device. (These are the same settings a button board can also set in
**Options**.)

### B4 · Sync (Sync tab)

1. **Check / wake relay** — pre-warms Render (free tier sleeps after ~15 min; first wake
   ~30–50 s).
2. **Sync captures ↔ relay** — uploads new captures, pulls back cracked passwords. Results
   print **per network** (wpa-sec / OHC / PwnCrack).
3. Cracked passwords land on the **Captures** tab and on-device (with a join QR). Already-
   synced captures are skipped — tick **re-sync everything** to force.

The **Captures** tab also lets you view, delete, and **exclude** a network by SSID
(never-attack).

---

## Config values (reference)

Whichever path you use, these are the settings and when each is needed:

| Setting | Needed for |
|---|---|
| **wpa-sec / OHC / PwnCrack key** (≥1) | cracking — both paths |
| **WiFi SSID / pass** | **Path A only** (the board's own upload) |
| **Relay URL + token** | **Path B only** (relay/console sync) |
| capture tuning (Ch Hop, Deauth, PMKID, Atk RSSI, Max Tries) | capture behaviour |

Settings live **on the device** and are read back by whatever UI you use (button
**Options** or the console **Config** tab).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| C5 board won't flash / bad image | Use **esptool 5.x**; run `esptool erase_flash` once before the first flash. |
| Console won't connect / no chooser | Needs a Web Bluetooth browser (Chrome/Edge, **not** iOS/Safari), and the board must be **in BLE BRIDGE**. |
| Board name not in the chooser | Board must be in BLE BRIDGE (Waveshare: tap; button board: menu → BLE BRIDGE). Only one phone can connect at a time. |
| `relay (not set)` in red | Set **Relay URL** (Path B only). Path A ignores it. |
| Relay sync says unreachable | Hit **Check / wake relay**, wait ~30–50 s for Render to wake, retry. |
| Nothing uploads | Those captures already synced — tick **re-sync everything**, or capture new ones. |

More: [`docs/DESIGN-ble-bridge.md`](DESIGN-ble-bridge.md) ·
[relay reference](../relay/README.md) · [README](../README.md).
