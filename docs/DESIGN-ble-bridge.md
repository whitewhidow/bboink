# BLE Bridge — sync with no board WiFi uplink (Android)

## Goal
Let the board sync **without its own internet uplink**. The **phone** does the
internet part: an Android/Chrome web app (hosted HTTPS) pulls captures off the board
over **BLE** (Web Bluetooth), forwards them to the **Render relay** over the phone's
**cellular**, and writes cracked results back to the board over BLE. The phone never
joins the board's AP — it stays on cellular, and BLE is a separate radio that doesn't
touch IP routing.

> **Android + Chrome only** (Web Bluetooth doesn't exist on iOS Safari). iPhone users
> keep using the board's WiFi-uplink → relay path.

## Why this is possible (and BT-tethering isn't)
- BT internet-sharing (PAN/BNEP) needs **Classic Bluetooth** — the ESP32-C5 is
  **BLE-only**, so the board can't use the phone as an IP uplink. ❌
- But BLE **GATT** data transfer is fine: board = BLE peripheral, phone = central via
  Web Bluetooth. The phone's own cellular handles the internet leg. ✅

## Board mode
Add a third mode to `ModeManager`: `BLE_BRIDGE`. In it **WiFi is fully off** (frees
~40 KB — plenty for NimBLE's ~30 KB) and the board runs a NimBLE **peripheral**
advertising the bridge service. No AP, no web server, no STA.

- **Enter:** from the MANAGEMENT web UI, a **"BLE Bridge"** button (`POST
  /api/blebridge`) → board tears down AP/web/DNS, stops WiFi, starts NimBLE, screen
  shows `BLE BRIDGE` + advertised name + a connection indicator.
- **Exit:** single button tap → back to CAPTURE (stops NimBLE, WiFi returns on next
  management toggle). The phone app can also send `{"c":"done"}`.
- Advertised name: `BBoink-XXXX` (same MAC-derived suffix as the AP) so it's
  recognisable in the Web Bluetooth chooser.

## GATT service (NimBLE peripheral)
One service, two characteristics (Nordic-UART-style split):

| Role | UUID | Props |
|------|------|-------|
| Service | `b0070000-b0b0-4b0a-9c5e-000000000000` | — |
| **RX** (phone → board) | `b0070001-b0b0-4b0a-9c5e-000000000000` | Write / WriteNR |
| **TX** (board → phone) | `b0070002-b0b0-4b0a-9c5e-000000000000` | Notify |

MTU is negotiated (~247 on Android → ~244-byte payloads). Captures are a few KB, so a
file is a handful of notifications.

### TX framing (board → phone), 1-byte type prefix
- `0x01` **TEXT** — payload is UTF-8 JSON (control/replies)
- `0x02` **BIN**  — payload is raw file bytes (a chunk)

### RX (phone → board): small JSON commands, one per write
- `{"c":"list"}` → TX `0x01 {"t":"list","files":[{"name","size","kind":"22000|pcap"}]}`
- `{"c":"get","name":"…"}` → board streams the file:
  1. TX `0x01 {"t":"begin","name":"…","size":N,"kind":"…"}`
  2. TX `0x02 <bytes>` × ⌈N / (MTU-4)⌉
  3. TX `0x01 {"t":"end"}`
- `{"c":"crk","b":"<bssid>","s":"<ssid>","p":"<password>"}` → append one cracked
  entry to the wpa-sec cracked cache; TX `0x01 {"t":"ok"}`
- `{"c":"crkdone"}` → flush/notify; TX `0x01 {"t":"ok"}`
- `{"c":"done"}` → leave bridge mode (→ CAPTURE)

Cracked results are sent **one entry per write** so nothing exceeds one MTU — simple
and robust, no reassembly on the board side.

## Phone web app (GitHub Pages, HTTPS)
Single static page (`bridge/` → GitHub Pages). Flow:
1. **Connect** → `navigator.bluetooth.requestDevice({ filters:[{ services:[SERVICE] }] })`,
   get RX (write) + TX (notify), `startNotifications()`.
2. Enter **Relay URL + token** once (saved in `localStorage`).
3. `list` → for each file, `get` and reassemble (TEXT `begin` → BIN chunks → `end`).
4. Forward over **cellular**: `.22000` text → `POST {relay}/v1/hashes`; each `.pcap`
   → `POST {relay}/v1/pcap?name=…`.
5. `GET {relay}/v1/cracked` → for each entry, RX `{"c":"crk",…}` back to the board,
   then `{"c":"crkdone"}`.
6. Show progress + the cracked list. `{"c":"done"}` on finish.

Web Bluetooth needs a **secure context** (HTTPS) — hence GitHub Pages, not the
board's `http://192.168.4.1`. The page is loaded over cellular, so it can reach both
BLE (local radio) and the relay (internet) at once.

## Relay change (CORS)
The web app calls the relay from a browser, so add CORS: reply to `OPTIONS` preflight
and send `Access-Control-Allow-Origin: *`, `Allow-Headers: authorization,
content-type`, `Allow-Methods: GET,POST,OPTIONS` on `/v1/*`. (`Origin *` is fine — the
`RELAY_TOKEN` is still required.)

## Heap / coexistence notes
- BLE_BRIDGE runs with **WiFi off** — the ~40 KB the WiFi stack held is free, so
  NimBLE's ~30 KB fits comfortably; no fragmentation fight, no reboot needed.
- Capture mode still deinits BLE for WiFi coexistence (unchanged); the bridge is only
  entered from management, never during capture.

## Build plan
1. Board: `ModeManager` `BLE_BRIDGE` + `src/ble/bridge.{h,cpp}` (NimBLE peripheral,
   the GATT protocol above, reads captures from `handshakesDir`, writes cracked into
   the wpa-sec cache). Screen status. `POST /api/blebridge` in the web UI.
2. Phone: `bridge/index.html` (Web Bluetooth app) → GitHub Pages.
3. Relay: add CORS to `/v1/*`.
4. On-device test: connect, list, transfer, forward, write-back.
