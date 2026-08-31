# BBoink — Two-Mode + Hosted Web UI architecture

**Status:** design draft · **Author:** whitewhidow · **Date:** 2026-08-30
**Applies to:** all board profiles (T-Embed CC1101, T-Display C5, Waveshare
ESP32-C5-LCD-1.47, future boards)
**Supersedes:** per-board bespoke menu/nav for management functions

---

## 1. Motivation

New target boards have too few usable inputs for the current menu-driven UI:

- **Waveshare ESP32-C5-LCD-1.47** has **one** usable button. It carries two
  physical buttons but only BOOT (**GPIO28**) is a readable GPIO; the second is
  wired to the chip reset line (CHIP_PU) and only hard-resets the board.
- The **T-Display C5** has two buttons (GPIO0 + GPIO28) — workable but cramped.
- The **T-Embed CC1101** has a rotary encoder + 2 buttons — comfortable, but its
  rich on-device menus are a per-board maintenance burden that does not port.

Rather than degrade the UI board-by-board, unify on a **two-mode model with a
single canonical action**, and move all management/configuration to a
**device-hosted web UI** driven from the operator's phone. This works on every
board from zero-to-many buttons and lets us retire most per-board nav code.

## 2. Key insight — the radio has exactly two states

Capture and web-serving are **mutually exclusive on the ESP32 radio**:
promiscuous mode drops the STA/AP netif (this is already why ntfy fires on
capture *exit*, not during — see `modes/oink.*`). So the device is always in one
of two radio states, and a single toggle exposes precisely that one axis:

| Mode | Radio state | Net stack | What it does |
|------|-------------|-----------|--------------|
| **CAPTURE** | promiscuous, channel-hopping, injection (deauth) | none | sniff EAPOL/PMKID, deauth, log to storage |
| **MANAGEMENT** | normal WiFi **AP+STA** | full | host web UI (SoftAP) **and** reach internet (STA) for cracking sync |

One button = one degree of freedom = correct fit, not a workaround.

### 2.1 Management mode is AP**STA** (both at once)

- **SoftAP** (`192.168.4.1`) hosts the web UI → phone drives it in the field
  with zero infrastructure.
- **STA** simultaneously joins the configured WiFi → wpa-sec / OnlineHashCrack /
  PwnCrack sync runs here, where internet is available.

Product flow: *capture in the field → tap to MANAGEMENT → device joins WiFi +
serves web UI → operator reviews captures, triggers syncs, edits config from the
browser → tap back to CAPTURE.*

## 3. Mode state machine

```
        ┌──────────────────────────── tap ────────────────────────────┐
        v                                                              │
   ┌─────────┐   tap    ┌────────────┐                                 │
   │ CAPTURE │ ───────► │ MANAGEMENT │ ───────────────────────────────┘
   └─────────┘          └────────────┘
        ▲                     │
        │ long-press = POWER OFF (from either mode)
        └── boot → CAPTURE if capture_ready · MANAGEMENT if not (§7.1)
```

- **Single tap** toggles CAPTURE ⇄ MANAGEMENT. This is the *only* required input.
- **Long-press (~2s)** = power off (deep sleep) from either mode.
- **Boot:** provisioning-aware (§7.1). If **`capture_ready`** it boots straight
  into **CAPTURE** and runs standalone — no phone, no WiFi station needed. If not
  (fresh flash / config cleared) it boots into **MANAGEMENT** so the operator can
  set it up — but one tap starts capturing anyway (§7.3). Overridable via
  `boot_mode` (`auto` | `capture` | `management`, default `auto`).
- Mode transitions are **hard radio teardown/bringup**, never overlapped:
  `CAPTURE→MGMT` = `NetworkRecon::stop()` (disable promiscuous via the raw
  `esp_wifi` path) → `NetLink` AP+STA up → start web server. `MGMT→CAPTURE` =
  stop web server → STA/AP down → `NetworkRecon::start()`.

## 4. Button semantics — uniform across all boards

The toggle is canonical everywhere; extra inputs are optional accelerators only.

| Board | Toggle (tap) | Power (long-press) | Extra inputs |
|-------|--------------|--------------------|--------------|
| Waveshare C5-LCD-1.47 | BOOT / GPIO28 | BOOT held | — (none) |
| T-Display C5 | GPIO28 top-tap | GPIO0 3s | GPIO0 = force-sync gesture (optional) |
| T-Embed CC1101 | encoder click | side btn hold | encoder = minor capture-side accelerator only; menus removed (Option A) |

A board declares its toggle/power source in `hal/board.h`; `app` consumes an
abstract `ModeInput{ toggle, power }` so no screen code is board-specific.

## 5. On-device display — minimal in both modes

The screen is now a **status surface**, not a control surface.

- **CAPTURE mode:** the existing live capture view (pool breakdown
  work/cool/pmf/cap/ign/weak/open/idle, `target:` SSID+dBm, beep + LED on hit).
  Unchanged from today's `screen_capture`.
- **MANAGEMENT mode:** just enough to connect a phone —
  - SoftAP **SSID** + **IP** (`192.168.4.1`)
  - **WiFi join QR** (reuse the LovyanGFX qrcode already used for cracked-net
    `WIFI:` QRs) → phone scans, joins AP, browser auto-opens
  - STA line: `wifi: <ssid> connected / connecting / down`
  - live counters: captures pending sync, last sync result
  All actual interaction happens in the browser.

## 6. Web UI architecture

A tiny **single-page app** served from flash, talking to a **JSON API** the
firmware exposes. The web app is the full replacement for every current screen.

```
 phone browser  ──HTTP──►  ESP32 SoftAP 192.168.4.1
   index.html (SPA)          AsyncWebServer / WebServer
   app.js  ── fetch ──►      /api/*  (JSON)
   app.css                   static assets from SPIFFS/LittleFS (gzipped)
```

### 6.1 JSON API surface (mirrors each existing function)

| Area (current screen) | Endpoint(s) | Notes |
|-----------------------|-------------|-------|
| Status / mode | `GET /api/status`, `POST /api/mode {capture\|management}` | device info, version, heap, mode, radio state |
| Captures registry (`MANAGE`) | `GET /api/captures`, `GET /api/captures/{id}`, `DELETE /api/captures/{id}` | C/M type, W/O/P upload flags, K cracked, SSID/BSSID/seen/pass |
| Capture QR / export | `GET /api/captures/{id}/qr`, `GET /api/captures/{id}/file?type=pcap\|22000` | download raw capture files |
| Targeted capture | `GET /api/targets`, `POST /api/capture/target {bssid}` | pick one BSSID to lock next capture run |
| wpa-sec sync | `POST /api/sync/wpasec`, `GET /api/sync/wpasec/status` | bulk upload + potfile pull |
| OnlineHashCrack | `POST /api/sync/ohc`, `GET /api/sync/ohc/status` | per-file upload |
| PwnCrack | `POST /api/sync/pwncrack`, `GET /api/sync/pwncrack/status` | per-file upload + potfile |
| Stats | `GET /api/stats` | totals, cracked count, storage usage |
| Options (all config) | `GET /api/config`, `PATCH /api/config {…}` | every OPTIONS field (WiFi, keys, ntfy, ch-hop, atk RSSI, deauth, max tries, etc.) |
| Secrets entry | `PATCH /api/config` (write-only keys) | WPA/OHC/PWN keys accepted, never returned in GET |
| Firmware update | `POST /api/ota`, `GET /api/ota/status` | trigger flash/SD OTA (§8) |
| Power / reboot | `POST /api/power {reboot\|off}` | |

Design rules:
- **Read models are cheap JSON; write actions are POST/PATCH** returning the new
  state so the SPA re-renders without a full reload.
- **Secrets are write-only.** `GET /api/config` returns key *presence*
  (`has_wpa_key: true`) never the value. Matches today's "keys entered on-device,
  never in git" rule — now "never returned over the wire" too.
- Long operations (sync, OTA) are **async with a status poll** endpoint, so the
  SPA shows progress and the HTTP request never blocks the capture engine's
  storage.

### 6.2 Front-end

- One `index.html` + one `app.js` + one `app.css`, **gzipped** into the flash FS.
  No framework, no CDN (device is offline on SoftAP) — vanilla JS, fetch, hash
  routing. Target < 40 KB gzipped total (see flash budget §8).
- Tabs mirror the API areas: **Capture control · Captures · Sync · Stats ·
  Config · System**.
- Renders the same tags the device does (C/M, W/O/P, K) so operators see a
  consistent model on-device and in-browser.

### 6.3 New/changed firmware modules

| Module | Role |
|--------|------|
| `core/mode_manager.{h,cpp}` | the CAPTURE⇄MANAGEMENT state machine + radio teardown/bringup |
| `web/webui.{h,cpp}` | HTTP server + `/api/*` handlers (JSON) |
| `web/webui_assets.h` | gzipped SPA bytes (generated from `web/ui/` at build) |
| `app/screen_capture` | unchanged (capture status) |
| `app/screen_manage` → **status-only** | becomes the MANAGEMENT connect screen (SSID/IP/QR); control moves to web |
| `net_link` | gains AP+STA (APSTA) bringup for management mode |

Existing `web/{wpasec,ohc,pwncrack,cracks,ntfy,updater}.*` are reused verbatim as
the *implementation* behind the `/api/sync/*` and `/api/ota` handlers.

## 7. Persistence, provisioning & standalone operation

- `mode` (last active), `boot_mode` policy, and all config live in the existing
  size-tolerant config blob (`core/config.*`) — SD on boards that have it,
  SPIFFS/LittleFS on the C5 boards without SD.
- The Waveshare C5-LCD **regains a microSD slot** (SCLK7/MOSI6/MISO5/CS4, shared
  with the LCD SPI bus), so captures + config can live on SD like the T-Embed,
  unlike the SD-less T-Display C5.

### 7.1 Configure once, run standalone

The web UI is a **provisioning surface, not a runtime dependency**. You set the
device up once from a browser; afterward it operates on its own with no phone.

- Two independent readiness flags are stored in config (they are decoupled — see
  §7.3):
  - **`capture_ready`** — capture params accepted. These have sane defaults, so
    this is trivially achievable (even with one tap, §7.3). *This alone* is enough
    for standalone operation.
  - **`sync_configured`** — at least one STA network **and** one service key
    present. Optional; only needed to upload.
  A device is "provisioned" for the purpose of the boot decision once
  `capture_ready` is set.
- **Not `capture_ready`** (fresh flash, or config wiped) → boots into **MANAGEMENT**,
  raises the SoftAP, and the on-device screen shows "SETUP — join AP" + QR. The
  SPA opens on a first-run wizard (WiFi creds → service keys → capture params →
  Save). Saving the wizard sets `capture_ready` (and `sync_configured` if a
  station + key were entered).
- **`capture_ready`** → every subsequent boot goes **straight into CAPTURE** and runs
  autonomously off the persisted config. The device "already knows what to do."
- MANAGEMENT is now an **on-demand** state: enter it (single tap) only to review
  captures, trigger sync, tweak config, or update firmware. Leave it and the
  device is standalone again.
- Config is **forward/back compatible** (size-tolerant blob) so a firmware update
  never forces re-provisioning; new fields take defaults.

### 7.3 No station configured / pulled into the field unprepared

Capture and sync are **fully decoupled** — capture is promiscuous and never
associates with any network, so a station is irrelevant to capturing. A device
with **no STA / no keys** pulled into the field behaves correctly:

- **Captures still happen** and are written to local storage (SD / LittleFS) with
  the W/O/P upload flags unset — they simply **queue** for later.
- **Store-and-forward:** queued captures auto-sync the next time *any* configured
  network is in range (ties into §7.2 and the existing `Crk Uplink` fallback).
  Nothing is lost by capturing before sync is set up.

**Configuring in the field (no infrastructure needed).** MANAGEMENT raises the
device's **own SoftAP**, which depends on no external WiFi. So anywhere, with only
a phone, you can enter MANAGEMENT → join the device AP → web UI → add WiFi/keys →
Save. You are never locked out for lack of a station.

**Field sync via phone hotspot.** A phone usually can't host a hotspot *and* stay
joined to the ESP AP simultaneously, so the flow is: (1) join ESP AP, enter your
**phone-hotspot** SSID/pass as the station + keys, Save; (2) leave the ESP AP,
enable the phone hotspot; (3) the device (APSTA) joins the hotspot as STA, syncs,
and shows **sync progress on its own screen** (which is why sync status must
render on-device, not only in the browser).

**Fresh-device escape hatch.** The first-run MANAGEMENT screen offers a one-tap
**"Start capturing now (configure sync later)"** action: it sets `capture_ready`
with defaults and drops straight into CAPTURE. A device pulled out fresh is never
stuck in a setup wizard when all you want is to capture.

### 7.4 Sync topologies — DECIDED: start with #1 (board uploads via STA)

How captures reach the crack services. Three topologies were evaluated; **v1
ships only #1** — the others are recorded for later.

1. **Board uploads via its own STA (v1).** In MANAGEMENT the board is AP+STA; its
   STA joins the configured network (home WiFi, or the phone's hotspot in the
   field) and the existing `web/{wpasec,ohc,pwncrack}` clients upload directly.
   CORS-free (an ESP HTTP client, not a browser). Needs the board to have
   internet. This is what the firmware already does; M2 (APSTA) is what lets it
   happen alongside the web UI. **No new upload code required.**

2. **Phone browser uploads over cellular (later).** Phone joins the board AP
   (WiFi, no internet) and the SPA pulls capture files from the board, then POSTs
   them to the service over the phone's cellular — board needs zero internet.
   Feasible only where the service API is `no-cors`-friendly (key in the form
   body, no custom header, response unreadable): **PwnCrack fits** (multipart
   `key` field + `handshake` file); **wpa-sec does not** (key is a Cookie, a
   forbidden fetch header); OHC TBD. Fire-and-forget only — cracked-result
   readback is a cross-origin GET the browser blocks.

3. **Export to phone, upload from the phone's own app (later).** SPA offers
   "download pending captures"; the board sends the `.22000`/`.pcap` over WiFi and
   the operator uploads them from the service's own site/app over cellular
   (same-origin there → no CORS, full results, every service). Robust universal
   fallback; one manual step; no board internet.

Rationale for starting with #1: it reuses the shipped upload clients unchanged,
works for all three services with full result readback, and the field/no-internet
gap is covered by the phone-hotspot variant already in §7.3. #2/#3 become the
"board fully offline" enhancements once the web UI exists (M4+).

### 7.2 Optional autonomous sync (later)

Because capture and STA are radio-exclusive, a fully standalone device can't sync
while capturing. Optional, opt-in behavior for unattended runs: on a schedule (or
when idle N minutes), **briefly drop capture → MANAGEMENT/STA-only → sync → resume
capture**. This generalizes the existing `Crk Uplink` fallback. Off by default;
kept out of v1 scope but the mode machine is built to allow it.

## 8. Flash budget & OTA (per board)

**RESOLVED:** dual-slot A/B OTA on **every** board, including the 4 MB Waveshare.
Safe rollback everywhere; no fragile single-slot in-place scheme.

| Board | Flash | App (~1.25 MB) | Web assets | OTA scheme |
|-------|-------|----------------|------------|-----------|
| T-Embed CC1101 | 16 MB | fits easily | ample | dual A/B flash-OTA + SD-OTA (today) |
| T-Display C5 | (per port) | fits | fits | dual A/B, as C5 port |
| **Waveshare C5-LCD** | **4 MB** | fits (1.25 MB in 1.7 MB slot) | embedded in app | **dual A/B flash-OTA, SD-OTA fallback** |

### 8.1 Why dual A/B fits in 4 MB

The earlier worry ("no room for two 1.5 MB slots + a big FS") does not apply here,
because two design choices keep the **internal** flash lean:

1. **Web UI assets are embedded in the app binary** (`web/webui_assets.h`, gzipped
   C array — <40 KB), not a separate FS partition. They update **atomically with
   the app** over OTA, and cost no dedicated partition.
2. **Captures + config live on SD** (this board has a slot), so the internal
   filesystem is only a small fallback (see §8.3), not the primary store.

With those, two ~1.7 MB app slots leave the ~1.25 MB app **~475 KB of headroom**
each and still fit a small FS inside 4 MB.

### 8.2 Waveshare 4 MB partition table (`partitions-waveshare-c5.csv`)

```
# Name,     Type, SubType, Offset,    Size      # Notes
nvs,        data, nvs,     0x9000,    0x6000    # 24K  keys/small state
otadata,    data, ota,     0xF000,    0x2000    # 8K   A/B boot selector
phy_init,   data, phy,     0x11000,   0x1000    # 4K
ota_0,      app,  ota_0,   0x20000,   0x1B0000  # 1728K app slot A
ota_1,      app,  ota_1,   0x1D0000,  0x1B0000  # 1728K app slot B
littlefs,   data, spiffs,  0x380000,  0x80000   # 512K  fallback FS (§8.3)
# ends at 0x400000 = 4 MB exactly. Bootloader offset 0x2000 (C5), table at 0x8000.
```

(16 MB boards keep their existing generous table; this CSV is Waveshare-only,
selected by `[env:waveshare-c5-lcd]`.)

### 8.3 Update paths

- **Primary — WiFi flash-OTA (A/B):** in MANAGEMENT (STA up), fetch the app-only
  release asset (`bboink-app-t-*-c5*.bin`) from GitHub `releases/latest`, write it
  to the **inactive** slot, set it pending, reboot. Same mechanism as the T-Embed
  `Update FW`, just A/B-swapped. **Rollback-safe:** the new image must confirm
  healthy on first boot (`esp_ota_mark_app_valid_cancel_rollback` after the UI +
  radio come up); if it panics/hangs before confirming, the bootloader reverts to
  the previous slot. A bad update **cannot brick** the device.
- **Fallback — SD-OTA:** put the app-only `.bin` on SD; MANAGEMENT offers "Update
  from SD" → writes the inactive slot from the file → same A/B swap + rollback.
  Covers field/no-internet updates (§7.3). Requires a **genuine** SD card — the
  known counterfeit-card failure (`FR_DISK_ERR`) blocks this path, so it is a
  fallback, not the primary.
- **First flash:** CI still ships a merged full-flash image (bootloader +
  partitions + boot_app0 + app at 0x0) built against the 4 MB CSV, plus the
  app-only image for OTA — mirroring the current two-asset release.

### 8.4 Internal FS as config fallback

Config/captures target SD when a genuine card is present. If SD is **absent or
unreliable** (the counterfeit-card case), the size-tolerant config blob and a
short capture spool fall back to the 512 KB internal `littlefs`, so the device is
never dependent on SD to boot, provision, or capture. Full capture archives still
need SD (flash is too small to hoard them).

## 9. Security considerations

- SoftAP **must be WPA2 with a per-device password** (not open) — the web UI can
  trigger deauth and exposes capture data. Password shown on the MANAGEMENT
  screen QR only.
- Secrets (WPA/OHC/PWN keys) are **write-only over the API** (§6.1).
- No auth beyond AP password in v1; the AP is short-lived (only up in management
  mode) and the operator physically controls the device. Revisit if we ever host
  the UI over STA/LAN instead of SoftAP.

## 10. Per-board rollout matrix

| Board | Toggle input | On-device MGMT screen | SD | OTA | Web UI |
|-------|--------------|----------------------|----|----|--------|
| Waveshare C5-LCD-1.47 | BOOT/GPIO28 | SSID/IP/QR (172×320) | ✅ | dual A/B (WiFi + SD fallback) | required (only real UI) |
| T-Display C5 | GPIO28 | SSID/IP/QR | ❌ (LittleFS) | C5 OTA | required |
| T-Embed CC1101 | encoder click | SSID/IP/QR | ✅ | dual + SD OTA | additive; legacy menus optional |

## 11. Implementation milestones

1. **M1 — ModeManager.** Introduce `core/mode_manager`, wire the single-button
   toggle + long-press power through `app`, restore/persist `mode`. Radio
   teardown/bringup verified (no wedge on repeated toggles). No web yet;
   MANAGEMENT screen shows a placeholder. *Gate: toggle 100× without a promiscuous
   wedge.*
2. **M2 — APSTA + connect screen.** `net_link` AP+STA bringup; MANAGEMENT screen
   renders SSID/IP/WiFi-QR + STA status. *Gate: phone joins AP + device syncs
   over STA in the same mode.*
3. **M3 — Web server + read API.** `web/webui` serving a static SPA shell +
   `GET /api/status|captures|stats|config`. Read-only mirror of the device.
4. **M4 — Write API + full SPA.** mode switch, config PATCH (write-only secrets),
   capture delete, targeted capture, async sync + OTA with status polling. Full
   tabbed SPA. *Gate: every current on-device function reachable from browser.*
5. **M5 — Waveshare board profile.** New `[env:waveshare-c5-lcd]`, board.h branch,
   LGFX header (ST7789 172×320, X-gap 34, SCLK7/MOSI6/CS23/DC24/RST26/BL10),
   WS2812 on GPIO8, SD on SCLK7/MOSI6/MISO5/CS4, single-button ModeInput, 4 MB
   partition table with **dual A/B OTA** (§8.2). *Gate: capture + management +
   an A/B OTA round-trip with rollback verified on hardware.*
6. **M6 — Delete per-board management menus (Option A).** Remove the on-device
   menu / OPTIONS / registry / sync screens on all boards; the web UI is the sole
   management surface. Keep only the two shared on-device status screens (CAPTURE
   stats, MANAGEMENT connect/QR). Extra inputs (T-Embed encoder, C5 2nd button)
   are reduced to minor capture-side accelerators + the safety-valve gesture
   below — no full menus survive. *Gate: no board-specific management UI remains;
   every management function is web-only.*

## 12. Open questions

- ~~Boot mode default~~ **RESOLVED (§7.1):** provisioning-aware — provisioned →
  CAPTURE, unprovisioned → MANAGEMENT; `boot_mode` override defaults to `auto`.
- **Capture while management screen is up?** No — radio is exclusive. Confirm the
  operator understands sync/capture never overlap (surface it in the UI copy).
- **SoftAP channel** in management mode — fixed 2.4 GHz ch1/6/11? (STA may pull
  the AP to the STA's channel; document the constraint.)
- ~~4 MB OTA~~ **RESOLVED (§8):** dual-slot A/B flash-OTA fits 4 MB (web assets
  embedded in the app, captures/config on SD → lean internal flash). WiFi A/B is
  primary with rollback (bad update can't brick); SD-OTA is the field fallback
  (genuine card required — counterfeit-SD caveat).
- **Emergency on-device control (KEEP):** with Option A there is no on-device
  management, so retain one hardcoded safety-valve gesture —
  **long-press-in-CAPTURE = force one sync cycle** (drop→STA→sync→resume) — so the
  device isn't useless if the phone/web path fails. This is a fixed gesture, not a
  menu.

---

*Hardware refs: Waveshare ESP32-C5-LCD-1.47 schematic (waveshareteam/esp32-c5-lcd-1.47) —
LCD ST7789 172×320 SCLK7/MOSI6/CS23/DC24/RST26/BL10, SD SCLK7/MOSI6/MISO5/CS4,
WS2812 GPIO8, BOOT=GPIO28, RESET=CHIP_PU, ESP32-C5HF4 (4 MB), onboard 2.4G+5G
antenna paths. C5 capture/deauth carries over from `tdisplay-c5-port`.*

---

## Addendum — sync architecture (C5 TLS reality + Render relay)

**Finding (ESP32-C5):** mbedtls needs a large *contiguous* heap block (~35 KB) per
TLS handshake, and after one handshake the heap is fragmented enough that a **second
handshake in the same boot** fails (`esp-aes: Failed to allocate memory` → `abort()`,
or a corrupt ClientHello → `-29312` EOF). Management mode (SoftAP+STA+web+DNS) is too
fragmented for TLS at all. Consequences that shaped the design:

- **Reboot-to-sync**: TLS runs *early in boot* at clean heap (STA only), never in
  management mode. State survives the soft reset via `RTC_NOINIT` (`boot_sync.h`).
- **One handshake per boot**: each sync *op* is a single handshake. `HTTPClient`
  (not raw `WiFiClientSecure`) drives the TLS write/read; `Connection: close` avoids
  read-timeouts; a `getMaxAllocHeap()` guard bails gracefully (`LOW HEAP`) instead of
  aborting. Multi-file uploads are **combined into one request** so a service = one
  handshake regardless of capture count.
- **Chained-reboot queue** (`bootSyncQueue`, a bitmask of `SyncOp`): *Sync All* /
  *Check Cracked* queue several ops and reboot through them one at a time, one
  handshake each, accumulating the result string across reboots.

**Render relay (recommended):** the cleaner end state. The board talks to **one**
HTTPS host (`relay/`, on Render) and does only *send capture* / *get cracked*. One
kept-alive connection = **one handshake** for everything; the relay holds the three
API keys and does all per-service formatting, dedup, and potfile merging. Board side:
`relayUrl`/`relayToken` config, `SYNC_RELAY` (upload all + fetch cracked) and
`SYNC_RELAY_PING` (`/healthz` wake + status) ops, exposed as **Sync via Relay** and
**Wake / Check** in the web UI. This makes the buffer-shrink / runtime-teardown ideas
unnecessary for the common case.
