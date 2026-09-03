# 🐷 BBoink

**A pocket WiFi handshake hunter.** BBoink turns a LilyGo, Waveshare or M5Stack board into a
capture rig: it channel-hops, deauths eligible APs, grabs WPA **4-way handshakes + PMKIDs**,
and gets them cracked — in the cloud or on your own machine — then shows the recovered
**password** right on the screen. One firmware, five boards.

**[🌐 Site + live screenshots](https://whitewhidow.github.io/bboink/)**  ·
**[⚡ Web flasher](https://whitewhidow.github.io/bboink/flasher/)**  ·
**[📖 Getting started](docs/GETTING-STARTED.md)**  ·
**[🧭 Reference](docs/REFERENCE.md)**

> ⚠️ BBoink transmits **deauth frames** to force handshakes. Use it **only** on networks you
> own or are explicitly authorized to test.

## Supported hardware

Five boards, one firmware. Buy links are the official vendor stores; the wiki/docs links are
the manufacturer's own hardware documentation.

<table>
<tr>
<td width="50%" valign="top" align="center">

<img src="https://lilygo.cc/cdn/shop/files/T-EMBED-CC1101-PLUS_6_4bda35c0-79ad-41ba-9aa7-52792d723ab1.jpg?v=1755075224" width="240" alt="LilyGo T-Embed CC1101"><br>

**LilyGo T-Embed CC1101 / PLUS**<br>
ESP32-S3 · 16 MB · encoder + button<br>
_buttons + on-device menu (or BLE)_

[Buy](https://lilygo.cc/products/t-embed-cc1101-plus) ·
[Wiki](https://wiki.lilygo.cc/products/t-embed-series/t-embed-cc1101/) ·
[GitHub](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101)

</td>
<td width="50%" valign="top" align="center">

<img src="https://lilygo.cc/cdn/shop/files/LILYGO-T-DISPLAY-C5_10.jpg?v=1783057404" width="240" alt="LilyGo T-Display C5"><br>

**LilyGo T-Display C5**<br>
ESP32-C5 (dual-band) · 2 buttons<br>
_buttons + on-device menu (or BLE)_

[Buy](https://lilygo.cc/en-us/products/t-display-c5) ·
[Wiki](https://wiki.lilygo.cc/products/t-display-series/t-display-c5/quick-start.html)

</td>
</tr>
<tr>
<td width="50%" valign="top" align="center">

<img src="https://raw.githubusercontent.com/waveshareteam/esp32-c5-lcd-1.47/main/assets/Product-1.webp" width="240" alt="Waveshare ESP32-C5-LCD-1.47"><br>

**Waveshare ESP32-C5-LCD-1.47**<br>
ESP32-C5 (dual-band) · **1 button**<br>
_BLE portal only (no menu, no AP)_

[Buy](https://www.waveshare.com/esp32-c5-lcd-1.47.htm) ·
[Docs](https://docs.waveshare.com/ESP32-C5-LCD-1.47) ·
[GitHub](https://github.com/waveshareteam/esp32-c5-lcd-1.47)

</td>
<td width="50%" valign="top" align="center">

**M5Stack Cardputer ADV**<br>
ESP32-S3 · 8 MB · **1 button** + LCD + microSD<br>
_BLE portal only (no menu, no AP)_

[Buy](https://shop.m5stack.com/products/m5stack-cardputer-adv)

</td>
</tr>
<tr>
<td width="50%" valign="top" align="center">

**LilyGo T-Dongle S3**<br>
ESP32-S3 · 16 MB · **1 button** + 160×80 LCD<br>
_BLE portal only (no menu, no AP)_

[Buy](https://lilygo.cc/products/t-dongle-s3)

</td>
</tr>
</table>

## Flash → Capture → Crack

### 1 · Flash

Open the **[web flasher](https://whitewhidow.github.io/bboink/flasher/)** in Chrome/Edge, plug
the board in over USB, pick your board + version, click **Install** — no esptool, no drivers.
(Or grab the `.bin` from [Releases](https://github.com/whitewhidow/bboink/releases) and flash
it by hand.)

> **Heads-up — T-Display C5:** the browser flasher can fail with *"Failed to initialize"* (its ROM
> rejects esptool's stub loader). Flash *that* board on the command line instead —
> `esptool --chip esp32c5 --no-stub write_flash 0x0 bboink-tdisplay-c5.bin` (see
> [getting started](docs/GETTING-STARTED.md)). The T-Embed, Waveshare, Cardputer ADV and T-Dongle S3 flash fine in the browser.

### 2 · Capture

Drive the board with its **buttons + on-screen menu** (T-Embed / T-Display), or entirely from
your **phone over Bluetooth** — the **[BLE portal](https://whitewhidow.github.io/bboink/bridge/)**,
a web page that needs nothing installed and is the *only* UI on the single-button boards
(Waveshare / Cardputer ADV / T-Dongle S3). No access point, no web server on the device.

Hit **CAPTURE** and it channel-hops (2.4 **and** 5 GHz on the C5 boards), deauths eligible
APs, and saves handshakes + PMKIDs — with live per-event stats. Networks are remembered in a
registry, and you can mark any SSID **never-attack**.

### 3 · Crack

Get the captures cracked — pick whatever suits you:

- **Cloud** — send them to a free cracking service ([wpa-sec](https://wpa-sec.stanev.org) ·
  [OnlineHashCrack](https://onlinehashcrack.com) · [PwnCrack](https://pwncrack.org)). A key
  from **any one** is enough. Button boards can upload **directly over WiFi**; or your phone
  forwards them through a tiny **relay you host** (the only path on the single-button
  Waveshare / Cardputer ADV / T-Dongle S3).
- **Local** — no account, fully offline: **Download hashes (.hc22000)** from the BLE portal
  and run `hashcat -m 22000 bboink.hc22000 <wordlist>` on your own machine.

Cracked passwords are written **back to the device** and shown on the capture (with a WiFi
join QR).

## Get started

**→ [Getting-started guide](docs/GETTING-STARTED.md)** — step-by-step per board (buttons vs
BLE-only): flashing, deploying the optional relay, configuring keys, and the full
capture → crack loop. Have a button board and just want it working? It's a short path with no
relay and no phone.

## Cracking services

All free — bring a key from **any one**, or skip the cloud and crack locally.

| Service | What BBoink does | |
|---|---|---|
| **wpa-sec** | uploads the `.pcap`, reads back cracked passwords | [wpa-sec.stanev.org](https://wpa-sec.stanev.org) |
| **OnlineHashCrack** | submits WPA `22000` hashes (GPU service) | [onlinehashcrack.com](https://onlinehashcrack.com) |
| **PwnCrack** | uploads `hc22000`, reads back cracked passwords | [pwncrack.org](https://pwncrack.org) |
| **Local (hashcat)** | download `.hc22000` from the BLE portal, crack offline | `hashcat -m 22000` |

## Under the hood

**→ [Reference](docs/REFERENCE.md)** covers the details this overview skips: the on-device
**menu** + **options**, exact **controls**, the **relay** deploy, **firmware updates / OTA**,
**capture file** formats, **storage**, **releases / CI**, **build from source**, and
**provenance**. Design notes: [`docs/DESIGN-ble-bridge.md`](docs/DESIGN-ble-bridge.md).

## Firmware updates & switching

WiFi OTA works on **every** board — including the single-button ones that have no
on-device menu. From the BLE portal's **Update** tab, **Update to latest** flags a
fetch and reboots; at a clean heap (WiFi alone, no BLE contention) the board writes
the latest release into the spare OTA slot and boots it — progress on the LCD.

The two 16 MB S3 boards (T-Embed CC1101, T-Dongle S3) can also **switch firmware**:
a hidden action (tap the *BBoink* brand 3×) flashes the sibling
[hid-ble-poc](https://github.com/whitewhidow/hid-ble-poc) app — a USB/BLE HID
tool — into the spare slot and boots into it; switch back from its own portal. Same
OTA machinery, byte-compatible slots. Each firmware advertises a distinct BLE
address so the host's GATT cache doesn't collide across a switch.

## Build from source

Five PlatformIO envs from one codebase — `t-embed-cc1101` (ESP32-S3), `tdisplay-c5`,
`waveshare-c5-lcd` (ESP32-C5), `cardputer-adv` (ESP32-S3), `tdongle-s3` (ESP32-S3).
`pio run -e <env>`; per-board bits are gated with `build_src_filter`. Full build/flash notes
are in the [reference](docs/REFERENCE.md#build--flash).
