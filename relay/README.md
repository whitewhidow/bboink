# BBoink relay

A tiny HTTPS front so the board talks to **one host** instead of three cracking
services. The ESP32-C5 reliably completes only **one TLS handshake per boot**, and
juggling three service APIs on-device is fragile — so the board does just two things:

- **send** a capture (hashes + pcap)
- **get** merged cracked results

…and this relay holds the API keys and does all the per-service uploading, format
juggling, dedup, and result merging.

## Endpoints

All `/v1/*` routes require `Authorization: Bearer <RELAY_TOKEN>`.

| Method | Path | Body | Forwards to |
|--------|------|------|-------------|
| POST | `/v1/hashes` | hashcat `22000` text (`WPA*…` lines) | OnlineHashCrack + PwnCrack |
| POST | `/v1/pcap?name=<n>` | raw `.pcap` bytes | wpa-sec |
| GET  | `/v1/cracked` | — | merges wpa-sec + PwnCrack potfiles → `{count, cracked:[{bssid,ssid,password,source}]}` |
| GET  | `/healthz` | — (no auth) | returns `ok` — used to wake the free tier |

## Environment variables (set in Render, **not** in git)

| Var | What |
|-----|------|
| `RELAY_TOKEN` | a long random string; the board sends it as `Bearer <token>` |
| `WPASEC_KEY` | your wpa-sec.stanev.org key |
| `OHC_KEY` | your OnlineHashCrack API key (`sk_…`) |
| `PWNCRACK_KEY` | your PwnCrack.org key (UUID) |

## Deploy to Render

1. Push this repo; in Render → **New + → Blueprint** → pick the repo → it reads
   `relay/render.yaml`. (Or **New Web Service**, Root Directory `relay`, build
   `npm install`, start `npm start`.)
2. Fill the 4 env vars above.
3. Deploy → note the URL (e.g. `https://bboink.onrender.com`).
4. Check: `curl https://<url>/healthz` → `ok`.

> The free tier **sleeps after ~15 min idle**; the first request wakes it (~30–50s).
> The board's **Wake / Check** button and its sync both use generous timeouts, and
> the sync's `GET /healthz`-style wake covers the cold start.

## Board setup

In the board's web **Config** tab set **Relay URL** (`https://…onrender.com`) and
**Relay token**. Then **Sync → Sync via Relay**: the board opens one kept-alive TLS
connection and POSTs all hashes, POSTs each pcap, and GETs merged cracked — a single
handshake, no chained reboots. **Wake / Check** pings `/healthz` to pre-warm the
service and report up/down + latency.

## Local run

```bash
cd relay
npm install
RELAY_TOKEN=dev WPASEC_KEY=… OHC_KEY=… PWNCRACK_KEY=… npm start
curl -s localhost:3000/healthz          # -> ok
```

Node 18+ (uses global `fetch` / `FormData` / `Blob`); only dependency is `express`.
