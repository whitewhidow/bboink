# BBoink on the LilyGO T-Display C5 (ESP32-C5) — Port Notes

Port of BBoink from the T-Embed CC1101 (ESP32-S3) to the **LilyGO T-Display C5**
(ESP32-C5). Branch: `tdisplay-c5-port`. Env: `[env:tdisplay-c5]`.

**Status: WORKING on hardware** — display, 2-button nav, Config AP web UI, dual-band
(2.4 + 5 GHz) capture, deauth injection, and handshake capture all verified on-device.
The one gotcha was **reception: this board needs an antenna** (see §6).

---

## 1. Why the C5 — and what we proved first (spike)

Before porting, a throwaway spike (`~/esp/c5-wifi-spike`) answered the make-or-break
questions on real hardware. Results:
- **5 GHz promiscuous RX: WORKS** (the S3 physically can't do 5 GHz). 2.4 GHz too.
- **Mgmt-frame injection / deauth: WORKS** — but only with BBoink's existing
  `wsl_bypasser` (`ieee80211_raw_frame_sanity_check` override + `-Wl,-zmuldefs`).
  The naive `esp_wifi_80211_tx` returns `ESP_ERR_INVALID_ARG` ("unsupport frame type").
- **SoftAP + web config page: WORKS** (spike `~/esp/c5-apweb-spike`) — a plain SoftAP
  you browse to at `192.168.4.1`, NOT a captive portal (see §7).

Verdict: full dual-band upgrade, worth doing. Nothing about the capture/attack engine
needed rethinking — only the board-adaptation layer + storage.

## 2. Board hardware (T-Display C5)

| | |
|---|---|
| MCU | ESP32-C5 (RISC-V, dual-band Wi-Fi 6), 16MB flash, 8MB PSRAM |
| Display | ST7789 170×320, SPI2: SCK=7, MOSI=9 (no MISO), CS=26, DC=8, RST=23; backlight/PWR = GPIO25 |
| Display params | offset/gap x=35 y=0, color **invert = true**, landscape (rotation 1) → 320×170 |
| Touch | CST816S cap-touch (I2C), INT=27, RST=24, addr 0x15 — **optional; may be absent** |
| PMU | AXP2602 — a battery **gauge only** (getSOC/voltage); does NOT gate LCD power |
| I2C | SDA=2, SCL=3 (AXP + touch) |
| Buttons | GPIO0 and GPIO28 |
| **Missing vs T-Embed** | **no microSD, no WS2812 LED, no I2S speaker** |

## 3. What changed for the port

- **`platformio.ini`** — new `[env:tdisplay-c5]` (pioarduino, C5 board, deauth override).
- **`src/hal/board.h`** — `PORK_BOARD_TDISPLAY_C5` branch (pins, geometry, SD/LED = -1/0).
- **`src/hal/tdisplay_c5_lgfx.h`** (new) — LovyanGFX ST7789 device for the C5.
- **`src/hal/m5compat.{h,cpp}`** — C5 branches: display facade, backlight, and the
  **2-button + CST816S input** backend (replacing the rotary encoder).
- **`src/main.cpp`, `src/core/config.cpp`** — gated out T-Embed-only bits (GPIO15 rail,
  WS2812, shared-bus SD reset-hold); C5 uses **LittleFS** for captures, SPIFFS for config.
- **`src/core/network_recon.cpp`** — dual-band channel hopping + the C5 capture fixes (§5).
- **`src/app/screen_configap.cpp`** (new) + menu wiring — the Config AP mode (§7).

## 4. Input scheme (2 buttons; touch is a bonus if present)

| Button | Tap | Hold ~1s | Hold 3s |
|---|---|---|---|
| **Top** (GPIO28) | Up | Select (enter) | — |
| **Bottom** (GPIO0) | Down | Back | Power off |

Decided on release so tap vs hold are distinct. If a CST816S touch panel is present,
swipes/tap also drive nav (auto-detected via an I2C probe at boot — logs
`[C5] CST816S(0x15) touch: PRESENT/absent`). This unit tested **button-only**.

## 5. Capture on the C5 — the fixes that mattered

Getting capture working took four distinct fixes, in `network_recon.cpp`:

1. **Band selection** — the dual-band C5 needs `esp_wifi_set_band()` before
   `esp_wifi_set_channel()`, or the radio never lands on 2.4 GHz. Wrapped in
   `reconTune(ch)` (ch≥36 ⇒ 5 GHz). **Only switches band when it actually changes** —
   calling `set_band` on every hop thrashes the radio (symptom: "finds only 1 network").
2. **Promiscuous filter** — a `nullptr` filter does NOT deliver **management frames**
   (beacons) on the C5's stack, so nothing gets discovered though data packets flow.
   Fixed by setting `WIFI_PROMIS_FILTER_MASK_ALL` explicitly (`reconApplyPromiscFilter()`).
3. **Dual-band hop list** — 2.4 GHz channels then 5 GHz UNII (36-161), grouped so the
   band boundary is crossed only twice per sweep. Targeting a 5 GHz AP works via the
   same band-aware `reconTune`.
4. **Dwell floor** — C5 min channel dwell raised to 250 ms (S3 uses 50). Tunable via the
   "Ch Hop ms" option.

Diagnostic added: the capture screen shows `ch: nets: pkts: mgmt:` — `mgmt:` (beacon)
count is the key signal (near-0 = reception problem; climbing = beacons arriving).

## 6. RECEPTION — needs an antenna ⚠️

The single biggest gotcha. With no antenna the C5 barely receives (≈34 packets / 19
mgmt frames in 15 s → "found only 2 networks" despite many APs around). **Every
software lever (band/filter/dwell) was already correct** — it was purely RF. Attaching
a **dual-band 2.4/5 GHz antenna** (u.FL/IPEX if the board has the connector) fixed it and
networks + handshakes flow normally. If discovery is weak, suspect the antenna first.

## 7. Config AP mode (the reason for the port on a 2-button board)

Data entry with 2 buttons is painful, so the **CONFIG AP** menu item starts a plain
**SoftAP** (`BBoink-<chipid>`) + an Arduino `WebServer` at `http://192.168.4.1`:
set WiFi STA creds + service keys (wpa-sec / OHC / PwnCrack / ntfy), list/download
captures, and trigger a sync — all from a phone browser.

**It is deliberately NOT a captive portal.** The earlier `~/esp/captive_portal` attempt
never got the auto-popup working on the C5 — the phone's connectivity-check *probe flood*
+ DNS hijack exhausted the C5's app/socket layer (the "matches Marauder" symptom on the
dev-IDF v6.1 stack). The phone side is fine (a real airport captive portal triggers
sign-in normally) — the failure was C5-side. A single manually-loaded page never hits
that flood, so plain SoftAP + web sidesteps the whole problem. (Now that we have a
working SoftAP + web server on the C5, revisiting the captive portal is possible, but
unnecessary for config.)

## 8. Build & flash (hard-won)

```
pio run -e tdisplay-c5            # build
pio run -e tdisplay-c5 -t upload --upload-port /dev/ttyACMx   # flash
```
- Needs **pioarduino** platform (`55.03.36`), not the official espressif32 (no C5 Arduino).
- Serial→USB needs BOTH `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` (else the
  `Serial USBSerial` macro is undeclared — C5 is USB-Serial-JTAG, `Serial`=HWCDCSerial).
- Flashing: `upload_speed=115200` + `upload_flags=--no-stub` (the 460800 baud switch
  Guru-Meditations the stub; native-USB write-timeouts need `--no-stub`).
- **To FLASH**: enter download mode = **hold BOOT, tap RST, release BOOT**.
- **To RUN the app after flashing**: **clean power-cycle (unplug/replug, no buttons)** —
  a plain RST or any DTR/RTS toggling keeps landing in `boot:0xe DOWNLOAD`.
- The port drifts between `/dev/ttyACM0` and `/dev/ttyACM1` on replug — detect it.
- `pio device monitor` in a real terminal works for serial (headless cat/pyserial nudge
  it into download mode). Baud is irrelevant over USB-CDC.

## 9. Still open / unverified
- Battery % (AXP2602 gauge not read yet — stubs 100%/4.0V).
- Touch mapping only matters if a CST816S is bonded (this unit had none).
- Config AP WPA-SEC sync path compiles + wired but light on runtime testing.
- Final dwell value (250 ms) may want tuning per antenna.
