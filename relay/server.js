// bboink relay — a tiny HTTPS front for the WPA capture-cracking services.
//
// The ESP32-C5 can only complete ONE TLS handshake per boot reliably, and juggling
// three different service APIs (wpa-sec / OnlineHashCrack / PwnCrack) on-device is
// fragile. So the board talks to THIS one host over one reused connection and does
// exactly two things: SEND a capture, and GET cracked results. This relay holds the
// API keys and does all the per-service formatting, uploading, and result merging.
//
// Endpoints (all require  Authorization: Bearer <RELAY_TOKEN>):
//   POST /v1/hashes            body = hashcat 22000 text  -> OHC + PwnCrack
//   POST /v1/pcap?name=<n>     body = raw .pcap bytes      -> wpa-sec
//   GET  /v1/cracked           -> merged {bssid,ssid,password,source}[] from potfiles
//   GET  /healthz              -> ok (no auth)
//
// Keys come from env vars (set them in the Render dashboard, NOT in git):
//   RELAY_TOKEN, WPASEC_KEY, OHC_KEY, PWNCRACK_KEY
// Node 18+ (global fetch / FormData / Blob). Only dependency is express.

const express = require('express');

const {
  RELAY_TOKEN = '',
  WPASEC_KEY = '',
  OHC_KEY = '',
  PWNCRACK_KEY = '',
  PORT = 3000,
} = process.env;

const app = express();

// CORS — the BLE-bridge web app (GitHub Pages) calls these routes from a browser.
// The RELAY_TOKEN is still required, so Allow-Origin * is fine.
app.use((req, res, next) => {
  res.set('Access-Control-Allow-Origin', '*');
  res.set('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.set('Access-Control-Allow-Headers', 'authorization,content-type');
  res.set('Access-Control-Max-Age', '86400');
  if (req.method === 'OPTIONS') return res.sendStatus(204);
  next();
});

// Raw bodies: the board POSTs plain bytes (no multipart on the device side).
app.use('/v1/hashes', express.text({ type: '*/*', limit: '5mb' }));
app.use('/v1/pcap', express.raw({ type: '*/*', limit: '10mb' }));

function authed(req, res) {
  const h = req.get('authorization') || '';
  const tok = h.startsWith('Bearer ') ? h.slice(7) : '';
  if (!RELAY_TOKEN || tok !== RELAY_TOKEN) {
    res.status(401).json({ error: 'unauthorized' });
    return false;
  }
  return true;
}

app.get('/healthz', (_req, res) => res.type('text').send('ok'));

// ---- upload: hashcat 22000 hashes -> OHC + PwnCrack -------------------------
app.post('/v1/hashes', async (req, res) => {
  if (!authed(req, res)) return;
  const text = (req.body || '').toString();
  const hashes = text.split(/\r?\n/).map(s => s.trim()).filter(s => s.startsWith('WPA*'));
  const uniq = [...new Set(hashes)];
  if (uniq.length === 0) return res.status(400).json({ error: 'no WPA* hash lines' });

  const out = { hashes: uniq.length, ohc: null, pwncrack: null };

  // OnlineHashCrack — JSON add_tasks
  try {
    const r = await fetch('https://api.onlinehashcrack.com/v2', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Connection': 'close' },
      body: JSON.stringify({
        api_key: OHC_KEY, agree_terms: 'yes', action: 'add_tasks',
        algo_mode: 22000, hashes: uniq,
      }),
    });
    const j = await r.json().catch(() => ({}));
    out.ohc = {
      http: r.status,
      accepted: j?.accepted?.count ?? 0,
      skipped: j?.skipped?.count ?? 0,
      rejected: j?.rejected?.count ?? 0,
    };
  } catch (e) { out.ohc = { error: String(e.message || e) }; }

  // PwnCrack — multipart .hc22000
  try {
    const fd = new FormData();
    fd.append('key', PWNCRACK_KEY);
    fd.append('handshake', new Blob([uniq.join('\n') + '\n']), 'bboink.hc22000');
    const r = await fetch('https://pwncrack.org/upload_handshake', { method: 'POST', body: fd });
    out.pwncrack = { http: r.status, ok: r.status === 200 || r.status === 201 };
  } catch (e) { out.pwncrack = { error: String(e.message || e) }; }

  res.json(out);
});

// ---- upload: raw pcap -> wpa-sec --------------------------------------------
app.post('/v1/pcap', async (req, res) => {
  if (!authed(req, res)) return;
  const buf = req.body;
  if (!buf || !buf.length) return res.status(400).json({ error: 'empty pcap body' });
  const name = (req.query.name || 'capture').toString().replace(/[^\w.-]/g, '_');

  try {
    const fd = new FormData();
    fd.append('file', new Blob([buf]), name.endsWith('.pcap') ? name : name + '.pcap');
    const r = await fetch('https://wpa-sec.stanev.org/', {
      method: 'POST', headers: { Cookie: `key=${WPASEC_KEY}` }, body: fd,
    });
    // 409 = already uploaded -> treat as success
    res.json({ wpasec: { http: r.status, ok: [200, 201, 409].includes(r.status) } });
  } catch (e) { res.status(502).json({ wpasec: { error: String(e.message || e) } }); }
});

// ---- fetch cracked: merge wpa-sec + pwncrack potfiles -----------------------
// Potfile line formats:  BSSID:STATIONMAC:SSID:PASSWORD  (wpa-sec)
//                        hash:SSID:PASSWORD or BSSID:SSID:PASSWORD (pwncrack varies)
function pushCracked(map, bssid, ssid, password, source) {
  if (!password) return;
  const key = (bssid || ssid || '').toLowerCase();
  if (!key) return;
  const e = map.get(key);
  if (e) {
    // Same network cracked by another service — record the extra source.
    if (!e.sources.includes(source)) e.sources.push(source);
    if (!e.password && password) e.password = password;
  } else {
    map.set(key, { bssid: bssid || '', ssid: ssid || '', password, sources: [source] });
  }
}

app.get('/v1/cracked', async (req, res) => {
  if (!authed(req, res)) return;
  const map = new Map();
  const errors = {};

  // wpa-sec potfile
  try {
    const r = await fetch('https://wpa-sec.stanev.org/?api&dl=1', { headers: { Cookie: `key=${WPASEC_KEY}` } });
    if (r.ok) {
      const t = await r.text();
      for (const line of t.split(/\r?\n/)) {
        const p = line.split(':');
        if (p.length >= 4) pushCracked(map, p[0], p[2], p.slice(3).join(':'), 'wpa-sec');
      }
    } else errors.wpasec = r.status;
  } catch (e) { errors.wpasec = String(e.message || e); }

  // pwncrack potfile
  try {
    const r = await fetch(`https://pwncrack.org/download_potfile_script?key=${encodeURIComponent(PWNCRACK_KEY)}`);
    if (r.ok) {
      const t = await r.text();
      for (const line of t.split(/\r?\n/)) {
        const p = line.split(':');
        // pwncrack potfile: hash : APMAC : CLIENTMAC : ESSID : PASSWORD (5+ fields)
        // -> bssid = APMAC (field 1), ssid = 2nd-to-last, password = last.
        if (p.length >= 5)      pushCracked(map, p[1], p[p.length - 2], p[p.length - 1], 'pwncrack');
        else if (p.length >= 3) pushCracked(map, p[0], p[p.length - 2], p[p.length - 1], 'pwncrack');
        else if (p.length === 2) pushCracked(map, p[0], '', p[1], 'pwncrack');
      }
    } else if (r.status !== 404) errors.pwncrack = r.status;
  } catch (e) { errors.pwncrack = String(e.message || e); }

  const cracked = [...map.values()].map(e => ({
    bssid: e.bssid, ssid: e.ssid, password: e.password,
    sources: e.sources, source: e.sources.join('+'),   // e.g. "wpa-sec+pwncrack"
  }));
  res.json({ count: cracked.length, cracked, errors });
});

app.listen(PORT, () => console.log(`bboink relay listening on :${PORT}`));
