// Node mock of the Keyra device API (SPEC §5), for UI development, screenshots and e2e.
// Behaviour mirrors firmware/components/keyra_api (routes, auth, error codes, the one-slot
// pending-action machine) closely enough that the web app cannot tell the difference.
//
//   npm run mock                       seeded, initialized vault (passphrase below)
//   MOCK_FRESH=1 npm run mock          uninitialized device (onboarding)
//   MOCK_AUTO_BUTTON=1 npm run mock    approves every pending item 3 s after it is armed
//   MOCK_USB=0                         start "not plugged in"
//   MOCK_BLE=0                         no paired Bluetooth device in the seed
//   MOCK_VIA=home                      every request arrives "through the home network" (SPEC §8.2);
//                                      without it, requests to http://127.0.0.1:PORT do, localhost is the AP
//   MOCK_BACKUP_DAYS=3                 days since the seeded vault's last backup (> 30 shows the reminder)
//   PORT=8787                          listen port
//
// Simulated hardware: POST /__mock/button {press:"short"|"long"} · POST /__mock/usb {usb:bool} (unplugging
//   a computer that was plugged in while unlocked auto-locks, SPEC §12.4)
//   POST /__mock/ble {pair:"<device name>"} (a device pairs while the window is open) · {connected:bool}
//     · {autoConnect:bool} (default true: the wanted device connects ~1.5 s after an action is armed)
// Home Wi‑Fi: any network joins ~2 s after the press, except with the password "wrong-password".
import { createServer } from 'node:http';
import { createCipheriv, createDecipheriv, createHash, createHmac, pbkdf2Sync, randomBytes, timingSafeEqual } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const PORT = Number(process.env.PORT || 8787);
const FRESH = process.env.MOCK_FRESH === '1';
const AUTO_BUTTON = process.env.MOCK_AUTO_BUTTON === '1';
const VIA_HOME = process.env.MOCK_VIA === 'home';
const DEMO_PASSPHRASE = 'keyra demo vault';

const EXPIRY_MS = 60000;
const KDF_MS = 450; // the device spends ≈1.2 s; keep the mock snappy but visibly async
const MAX_BODY = 64 * 1024;
const MAX_RESTORE_BODY = 2 * 1024 * 1024;
const MAX_IMAGE = 3 * 1024 * 1024; // one app partition (SPEC §14)
const MAX_ENTRIES = 1000;
const MAX_SESSIONS = 4;
const MAX_HISTORY = 10;
const LIMITS = { title: 128, url: 512, username: 256, password: 256, totp: 512, notes: 2048 };
const STR_FIELDS = Object.keys(LIMITS);
const DIST = fileURLToPath(new URL('../dist/', import.meta.url));

// ---------- device state ----------

const device = { name: 'Keyra', version: '1.0.0', model: 'ESP32-S3', mac: '7C:DF:A1:0B:7F:3A' };
const defaultSsid = () => `Keyra-${device.mac.replace(/:/g, '').slice(-4)}`;
const defaultSettings = () => ({
  deviceName: 'Keyra',
  wifiSsid: '', // empty = default SSID
  wifiPassword: 'keyra1234',
  autoLockMin: 15,
  keyDelayMs: 12,
  bothSeparator: 'tab',
  submitAfterBoth: false,
  ledBrightness: 60,
  bleEnabled: true,
  output: 'auto',
  bleConnect: 'on_demand',
  osUsb: '', // SPEC §10.5
  homeWifi: { enabled: false, ssid: '', password: '' }, // password is write-only, never sent
  apMode: 'always',
  protectReveal: true,
  lockOnUsb: true,
  lockOnBle: false,
  lastBackupAt: 0,
});

const host = { usb: process.env.MOCK_USB !== '0', capsLock: false };
// Bluetooth (SPEC §8.1): pairing window, bonded devices (max 4), the connected one.
const PAIR_WINDOW_MS = 120000;
const MAX_BONDS = 4;
const LINGER_MS = 20000; // on demand: keep the link this long after typing
const CONNECT_MS = 1500; // how long the simulated host takes to connect
const OSES = ['', 'mac', 'ios', 'windows', 'android', 'linux'];
const ble = { pairingUntil: 0, bonds: [], connected: null, wanted: null, lingerTimer: null, autoConnect: true };
const pairing = () => settings.bleEnabled && Date.now() < ble.pairingUntil;
const bleReady = () => settings.bleEnabled && ble.connected !== null;
/** keyra_api pickTarget(): usb → USB; ble → most recent bond; auto → USB if plugged in, else as ble. */
function pickTarget() {
  if (settings.output === 'usb' || (settings.output === 'auto' && host.usb)) return { kind: 'usb' };
  if (!settings.bleEnabled || !ble.bonds.length) return { kind: 'none' };
  const best = ble.bonds.find((b) => b.addr === ble.connected) ?? [...ble.bonds].sort((a, b) => b.lastSeen - a.lastSeen)[0];
  return { kind: 'ble', addr: best.addr };
}
const targetText = (t) => (t.kind === 'usb' ? 'usb' : t.kind === 'ble' ? t.addr : null);
function connectBle(addr) {
  if (!settings.bleEnabled || !ble.bonds.some((b) => b.addr === addr)) return;
  clearTimeout(ble.lingerTimer);
  ble.connected = addr;
  ble.bonds.find((b) => b.addr === addr).lastSeen = nowSec();
}
/** On demand, Keyra lets go of the link after a linger (or at once). Always keeps it. */
function releaseBle(lingerMs) {
  clearTimeout(ble.lingerTimer);
  if (settings.bleConnect === 'always') return;
  if (lingerMs) ble.lingerTimer = setTimeout(() => (ble.connected = null), lingerMs);
  else ble.connected = null;
}
/** keyra_api syncBleDemand(): follow the armed action's Bluetooth host. */
function syncDemand() {
  const s = machine.slot;
  const want = s?.kind === 'type' && s.req.target.kind === 'ble' ? s.req.target.addr : null;
  if (want) {
    if (ble.wanted !== want) {
      ble.wanted = want;
      clearTimeout(ble.lingerTimer);
      if (ble.connected && ble.connected !== want) ble.connected = null; // make room for the wanted host
      if (ble.connected !== want && ble.autoConnect) setTimeout(() => ble.wanted === want && connectBle(want), CONNECT_MS);
    }
  } else if (ble.wanted && !machine.typing) {
    ble.wanted = null;
    releaseBle(0);
  }
}
function randomAddr() {
  return [...randomBytes(6)].map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(':');
}
function blePair(name) {
  if (!pairing() || ble.bonds.length >= MAX_BONDS) return false;
  const addr = randomAddr();
  ble.bonds.push({ addr, name, lastSeen: nowSec(), os: '' });
  ble.connected = addr;
  ble.pairingUntil = 0; // one approval, one pairing
  releaseBle(LINGER_MS);
  return true;
}
let settings = defaultSettings();
let vault = null; // { passphrase, entries: Map<id, Entry> } once initialized
let unlocked = false;
let failures = 0;
let lockedUntil = 0;
let timeValid = false;
let lastActivity = Date.now();
const sessions = new Map(); // token → { csrf, lastUsed, trustId, graceUntil }
const GRACE_MS = 60000; // SPEC §12.3: after a press, this session may see secrets this long
let usbSeen = false; // a computer was plugged in since the unlock (charger-only never locks)
let usbSession = 1; // bumps on every plug-in; a USB action is bound to the one it was armed on
const trusted = new Map(); // sha256(kt) → { id, name, created, lastSeen }
const MAX_TRUSTED = 8;
const homeLink = { connected: false, ip: null, rssi: null, error: '', timer: null };
const NETWORKS = [
  { ssid: 'Al-Rashid Home', rssi: -48, secure: true, channel: 11 },
  { ssid: 'Al-Rashid Home 5G', rssi: -61, secure: true, channel: 1 },
  { ssid: 'TP-Link_3F2A', rssi: -72, secure: true, channel: 6 },
  { ssid: 'Cafe Baghdad Free', rssi: -80, secure: false, channel: 6 },
  { ssid: 'زين فايبر', rssi: -84, secure: true, channel: 3 },
];

// One slot (SPEC §5 button semantics): a type action or a presence op, 60 s expiry.
const machine = {
  slot: null, // { kind:'type', req, deadline } | { kind:'presence', op, commit, deadline }
  typing: false,
  running: null, // op name while a presence commit runs
  last: null, // { ok, code, at, title, what }
  opResult: null, // { op, code, at }
};

function expire(now = Date.now()) {
  const s = machine.slot;
  if (!s || now < s.deadline) return;
  if (s.kind === 'type') {
    // A Bluetooth host that never connected is the better explanation.
    const code = s.req.target.kind === 'ble' && ble.connected !== s.req.target.addr ? 'no_host' : 'expired';
    machine.last = { ok: false, code, at: s.deadline, title: s.req.title, what: s.req.what };
  } else machine.opResult = { op: s.op, code: 'expired', at: s.deadline };
  machine.slot = null;
}

function arm(req) {
  req.usbSession = req.target.kind === 'usb' && host.usb ? usbSession : 0;
  machine.slot = { kind: 'type', req, deadline: Date.now() + EXPIRY_MS };
  syncDemand();
  autoPress(machine.slot);
  return { ...req, target: targetText(req.target), expiresIn: EXPIRY_MS };
}

/** awaitPresence replaces the slot; tryAwaitPresence (no session) never displaces anything. */
function awaitPresence(op, commit, { tryOnly = false } = {}) {
  expire();
  if (tryOnly && (machine.slot || machine.running)) return null;
  // Like Machine::cancelPresence: only the requester gets the token that withdraws it.
  machine.slot = { kind: 'presence', op, commit, deadline: Date.now() + EXPIRY_MS, cancel: randomBytes(16).toString('hex') };
  autoPress(machine.slot);
  return EXPIRY_MS;
}

function autoPress(slot) {
  if (!AUTO_BUTTON) return;
  setTimeout(() => machine.slot === slot && press('short'), 3000);
}

function dropSessionItems() {
  const s = machine.slot;
  if (!s) return;
  if (s.kind === 'type') {
    machine.last = { ok: false, code: 'cancelled', at: Date.now(), title: s.req.title, what: s.req.what };
    machine.slot = null;
  } else if (!['setup', 'factory_reset', 'trust_browser'].includes(s.op)) {
    machine.opResult = { op: s.op, code: 'cancelled', at: Date.now() };
    machine.slot = null;
  }
}

function lockAll() {
  unlocked = false;
  usbSeen = false;
  sessions.clear();
  dropSessionItems();
  ble.pairingUntil = 0;
}

function press(kind) {
  expire();
  syncDemand();
  const s = machine.slot;
  lastActivity = Date.now(); // button use counts as activity, like the firmware
  if (kind === 'short') {
    if (s?.kind === 'presence') {
      machine.slot = null;
      machine.running = s.op;
      setTimeout(() => {
        let ok = false;
        try {
          ok = s.commit() !== false;
        } catch (e) {
          console.error(`[mock] ${s.op} commit failed:`, e.message);
        }
        machine.opResult = { op: s.op, code: ok ? 'done' : 'failed', at: Date.now() };
        machine.running = null;
      }, 300);
      return `approved ${s.op}`;
    }
    const t = s?.kind === 'type' ? s.req.target : null;
    if (t?.kind === 'ble' && ble.connected !== t.addr) return 'connecting (blink)'; // stays armed
    if (s?.kind === 'type' && !machine.typing) {
      machine.slot = null;
      machine.typing = true;
      ble.wanted = null; // the job owns the link now
      const text = typedText(s.req);
      setTimeout(() => {
        machine.typing = false;
        let code = 'typed';
        if (t.kind === 'none') code = 'no_host';
        else if (t.kind === 'usb' && !host.usb) code = 'no_usb';
        else if (t.kind === 'ble' && ble.connected !== t.addr) code = 'no_host';
        else if (text === null) code = 'failed';
        else if (/[^\x20-\x7e\t\n]/.test(text)) code = 'unsupported_char';
        machine.last = { ok: code === 'typed', code, at: Date.now(), title: s.req.title, what: s.req.what };
        if (t.kind === 'ble') releaseBle(LINGER_MS);
        const e = vault?.entries.get(s.req.id);
        if (code === 'typed' && e) e.lastUsed = nowSec();
      }, 250 + Math.min(1500, (text?.length ?? 0) * settings.keyDelayMs));
      // Free text: only its length, so e2e can check "twice" without the mock echoing secrets.
      return s.req.what === 'text' ? `typing text (${text.length} chars)` : `typing ${s.req.what} · ${s.req.title}`;
    }
    return 'nothing to do (blink)';
  }
  if (s) {
    if (s.kind === 'type') machine.last = { ok: false, code: 'cancelled', at: Date.now(), title: s.req.title, what: s.req.what };
    else machine.opResult = { op: s.op, code: 'cancelled', at: Date.now() };
    machine.slot = null;
    return 'cancelled';
  }
  if (machine.typing || machine.running) return 'busy (cannot interrupt)';
  if (unlocked) {
    lockAll();
    return 'locked';
  }
  return 'nothing to do (blink)';
}

/** What the HID engine would type for a request (null = entry vanished). */
function typedText(req) {
  if (req.what === 'test') return 'Keyra test 123';
  if (req.what === 'text') return req.text + (req.twice ? (req.enterBetween ? '\n' : '\t') + req.text : '');
  const e = vault?.entries.get(req.id);
  if (!e) return null;
  const sep = settings.bothSeparator === 'enter' ? '\n' : '\t';
  if (req.what === 'username') return e.username;
  if (req.what === 'password') return e.password;
  if (req.what === 'totp') return totpCode(e.totp)?.code ?? null;
  return e.username + sep + e.password + (req.submit ? '\n' : '');
}

// ---------- TOTP (RFC 6238) ----------

function base32(s) {
  const alpha = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
  const clean = s.toUpperCase().replace(/[\s=-]/g, '');
  let bits = 0;
  let acc = 0;
  const out = [];
  for (const ch of clean) {
    const v = alpha.indexOf(ch);
    if (v < 0) return null;
    acc = (acc << 5) | v;
    bits += 5;
    if (bits >= 8) {
      out.push((acc >>> (bits - 8)) & 0xff);
      bits -= 8;
    }
  }
  return out.length ? Buffer.from(out) : null;
}

function totpCode(secretOrUri, nowMs = Date.now()) {
  let secret = secretOrUri;
  let digits = 6;
  let period = 30;
  let algo = 'sha1';
  if (/^otpauth:\/\//i.test(secretOrUri)) {
    try {
      const u = new URL(secretOrUri);
      secret = u.searchParams.get('secret') ?? '';
      digits = Number(u.searchParams.get('digits') ?? 6);
      period = Number(u.searchParams.get('period') ?? 30);
      algo = (u.searchParams.get('algorithm') ?? 'SHA1').toLowerCase();
    } catch {
      return null;
    }
  }
  const key = base32(secret);
  if (!key || ![6, 7, 8].includes(digits) || !(period > 0) || !['sha1', 'sha256', 'sha512'].includes(algo)) return null;
  const t = Math.floor(nowMs / 1000);
  const counter = Buffer.alloc(8);
  counter.writeBigUInt64BE(BigInt(Math.floor(t / period)));
  const h = createHmac(algo, key).update(counter).digest();
  const o = h[h.length - 1] & 0x0f;
  const bin = (h.readUInt32BE(o) & 0x7fffffff) % 10 ** digits;
  return { code: String(bin).padStart(digits, '0'), period, remaining: period - (t % period) };
}

// ---------- backup file (same envelope as firmware keyra_vault/src/core/backup_format.hpp) ----------

const BACKUP_ITER = 20000;

function exportBackup(pass) {
  const salt = randomBytes(16);
  const iv = randomBytes(12);
  const key = pbkdf2Sync(pass, salt, BACKUP_ITER, 32, 'sha256');
  const c = createCipheriv('aes-256-gcm', key, iv);
  const plain = Buffer.from(JSON.stringify([...vault.entries.values()]));
  const data = Buffer.concat([c.update(plain), c.final(), c.getAuthTag()]);
  return JSON.stringify({
    format: 'keyra-backup',
    v: 2,
    kdf: { alg: 'pbkdf2-sha256', iter: BACKUP_ITER, salt: salt.toString('base64') },
    iv: iv.toString('base64'),
    data: data.toString('base64'),
  });
}

/** → entries array, or 'invalid' / 'wrong'. */
function openBackup(pass, b) {
  if (b?.format !== 'keyra-backup' || ![1, 2].includes(b.v) || b.kdf?.alg !== 'pbkdf2-sha256' || !Number.isInteger(b.kdf.iter)) return 'invalid';
  const salt = Buffer.from(String(b.kdf.salt ?? ''), 'base64');
  const iv = Buffer.from(String(b.iv ?? ''), 'base64');
  const data = Buffer.from(String(b.data ?? ''), 'base64');
  if (salt.length !== 16 || iv.length !== 12 || data.length < 16 || b.kdf.iter < 1 || b.kdf.iter > 10_000_000) return 'invalid';
  const key = pbkdf2Sync(pass, salt, b.kdf.iter, 32, 'sha256');
  let plain;
  try {
    const d = createDecipheriv('aes-256-gcm', key, iv);
    d.setAuthTag(data.subarray(data.length - 16));
    plain = Buffer.concat([d.update(data.subarray(0, data.length - 16)), d.final()]);
  } catch {
    return 'wrong';
  }
  let list;
  try {
    list = JSON.parse(plain.toString('utf8'));
  } catch {
    return 'invalid';
  }
  if (!Array.isArray(list)) return 'invalid';
  const out = [];
  for (const item of list) {
    const e = readEntry(item, true);
    if (typeof e === 'string' || !validEntry(e)) return 'invalid';
    // v2: up to 10 previous passwords (SPEC §9.3); absent in v1.
    const h = item.history ?? [];
    if (!Array.isArray(h) || h.length > MAX_HISTORY) return 'invalid';
    for (const x of h) {
      if (!x || typeof x !== 'object' || typeof (x.password ?? '') !== 'string' || !Number.isInteger(x.changedAt ?? 0)) return 'invalid';
      if (Buffer.byteLength(x.password ?? '') > LIMITS.password) return 'invalid';
    }
    e.history = h.map((x) => ({ password: x.password ?? '', changedAt: x.changedAt ?? 0 }));
    e.id = Number.isInteger(item.id) && item.id > 0 && item.id <= 0xffffffff ? item.id : 0;
    out.push(e);
  }
  return out;
}

/** Merge (or replace) like Vault::importBackup: match by id, then by title+username+url. */
function importBackup(incoming, replace) {
  const next = replace ? new Map() : new Map(vault.entries);
  let added = 0;
  let updated = 0;
  for (const e of incoming) {
    let match = !replace && e.id ? next.get(e.id) : undefined;
    if (!match && !replace) match = [...next.values()].find((x) => dedupeKey(x) === dedupeKey(e));
    if (match) {
      next.set(match.id, { ...e, id: match.id });
      updated++;
    } else {
      const id = e.id && !next.has(e.id) ? e.id : freshId(next);
      next.set(id, { ...e, id });
      added++;
    }
  }
  if (next.size > MAX_ENTRIES) return 'full';
  vault.entries = next;
  return { added, updated };
}

// ---------- generator (same rules as firmware keyra_api/src/generator.cpp) ----------

const GEN_SYMBOLS = '!@#$%^&*-_=+?';
const GEN_AMBIGUOUS = '0Oo1lI|`\'"';

/** P(length uniform draws meet every class minimum), via exponential generating functions. */
function genAcceptance(classes, L) {
  const n = classes.reduce((a, c) => a + c.chars.length, 0);
  let acc = new Array(L + 1).fill(0);
  acc[0] = 1;
  for (const c of classes) {
    const q = c.chars.length / n;
    const term = [];
    for (let k = 0, t = 1; k <= L; k++) {
      if (k > 0) t *= q / k;
      term.push(k >= c.min ? t : 0);
    }
    const next = new Array(L + 1).fill(0);
    for (let a = 0; a <= L; a++) if (acc[a]) for (let k = 0; a + k <= L; k++) next[a + k] += acc[a] * term[k];
    acc = next;
  }
  let f = 1;
  for (let i = 2; i <= L; i++) f *= i;
  return Math.min(1, acc[L] * f);
}

/** → {password, entropyBits} or an error message (400). */
function generate(b) {
  const L = b.length;
  if (!Number.isInteger(L)) return '"length" (8-128) is required';
  for (const k of ['lower', 'upper', 'digits', 'symbols']) if (typeof b[k] !== 'boolean') return '"lower", "upper", "digits" and "symbols" (booleans) are required';
  for (const k of ['minDigits', 'minSymbols']) if (b[k] !== undefined && !(Number.isInteger(b[k]) && b[k] >= 0 && b[k] <= 128)) return `${k} must be 0-128`;
  if (b.avoidAmbiguous !== undefined && typeof b.avoidAmbiguous !== 'boolean') return 'avoidAmbiguous must be a boolean';
  const symbolSet = b.symbolSet ?? GEN_SYMBOLS;
  const symErr = 'symbolSet must be 1-32 distinct ASCII punctuation characters';
  if (typeof symbolSet !== 'string' || !/^[!-/:-@[-`{-~]{1,32}$/.test(symbolSet) || new Set(symbolSet).size !== symbolSet.length) return symErr;
  if (L < 8 || L > 128) return 'length must be 8-128';
  if (!b.lower && !b.upper && !b.digits && !b.symbols) return 'enable at least one of lower, upper, digits, symbols';
  const minD = b.minDigits ?? 0;
  const minS = b.minSymbols ?? 0;
  const minErr = 'minDigits/minSymbols need their class enabled and must fit in the length';
  if (minD > L || minS > L || (!b.digits && minD > 0) || (!b.symbols && minS > 0)) return minErr;
  const strip = (s) => (b.avoidAmbiguous ? [...s].filter((c) => !GEN_AMBIGUOUS.includes(c)).join('') : s);
  const classes = [
    [b.lower, 'abcdefghijklmnopqrstuvwxyz', 1],
    [b.upper, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 1],
    [b.digits, '0123456789', Math.max(1, minD)],
    [b.symbols, symbolSet, Math.max(1, minS)],
  ]
    .filter(([on]) => on)
    .map(([, chars, min]) => ({ chars: strip(chars), min }));
  if (classes.some((c) => !c.chars)) return 'avoidAmbiguous leaves an enabled class without characters';
  if (classes.reduce((a, c) => a + c.min, 0) > L) return minErr;
  const p = genAcceptance(classes, L);
  if (p < 1e-3) return 'minimums are too high for this length';
  const alphabet = classes.map((c) => c.chars).join('');
  const limit = 256 - (256 % alphabet.length);
  for (;;) {
    let pw = '';
    while (pw.length < L) {
      for (const byte of randomBytes(64)) if (byte < limit && pw.length < L) pw += alphabet[byte % alphabet.length];
    }
    if (classes.every((c) => [...pw].filter((ch) => c.chars.includes(ch)).length >= c.min))
      return { password: pw, entropyBits: Math.floor(L * Math.log2(alphabet.length) + Math.log2(p)) };
  }
}

// ---------- entries ----------

const nowSec = () => Math.floor(Date.now() / 1000);
const dedupeKey = (e) => `${e.title}\u001f${e.username}\u001f${e.url}`;

function freshId(map = vault.entries) {
  for (;;) {
    const id = randomBytes(4).readUInt32BE();
    if (id !== 0 && !map.has(id)) return id;
  }
}

const summary = (e) => ({
  id: e.id,
  title: e.title,
  url: e.url,
  username: e.username,
  favorite: e.favorite,
  hasPassword: e.password !== '',
  hasTotp: e.totp !== '',
  updated: e.updated,
  lastUsed: e.lastUsed,
});

/** Copies present fields onto `base`; returns an error message on a wrong type. */
function readEntry(src, withTimestamps, base = { title: '', url: '', username: '', password: '', totp: '', notes: '', favorite: false, created: 0, updated: 0, lastUsed: 0, history: [] }) {
  const e = { ...base };
  for (const k of STR_FIELDS) {
    if (src[k] === undefined) continue;
    if (typeof src[k] !== 'string') return `"${k}" must be a string`;
    e[k] = src[k];
  }
  if (src.favorite !== undefined) {
    if (typeof src.favorite !== 'boolean') return '"favorite" must be a boolean';
    e.favorite = src.favorite;
  }
  if (withTimestamps) {
    for (const k of ['created', 'updated', 'lastUsed']) {
      if (src[k] === undefined) continue;
      if (!Number.isInteger(src[k]) || src[k] < 0 || src[k] > 2 ** 40) return `"${k}" must be unix seconds`;
      e[k] = src[k];
    }
  }
  return e;
}

const validEntry = (e) => STR_FIELDS.every((k) => Buffer.byteLength(e[k]) <= LIMITS[k]);

// ---------- firmware update (SPEC §14; firmware keyra_api/src/handlers_update.cpp) ----------

// What "the latest GitHub release" holds in the mock. MOCK_RELEASE_VERSION overrides it.
const MOCK_RELEASE = {
  version: process.env.MOCK_RELEASE_VERSION || '1.1.0',
  size: 1_700_000,
  notes: '- Password health\n- Updates over Wi-Fi\n- Bluetooth: pair a new device while another is connected',
};
const fw = { phase: 'idle', source: 'upload', done: 0, total: 0, version: '', error: '' };
const fwState = () => ({ ...fw });
const homeOnline = () => homeLink.connected || VIA_HOME;

function fwBegin(source, total) {
  Object.assign(fw, { phase: 'receiving', source, done: 0, total, version: '', error: '' });
}

const FW_MESSAGES = {
  bad_signature: "That firmware is not signed with this Keyra's key (or this Keyra was not installed signed)",
  bad_image: 'That file is not Keyra firmware',
  downgrade: 'That firmware is older than the one installed',
  offline: 'Keyra is not on the internet; join your home Wi-Fi first',
};
const FW_STATUS = { downgrade: 409, offline: 409 };

/** Records the failure (when `record`) and, unless `quiet`, answers like the firmware. */
function fwFail(code, record = true, quiet = false) {
  if (record) Object.assign(fw, { phase: 'failed', error: code });
  if (!quiet) fail(FW_STATUS[code] ?? 400, code, FW_MESSAGES[code]);
}

function cmpVersion(a, b) {
  const p = (v) => /^(\d{1,4})\.(\d{1,4})\.(\d{1,4})(?:-.*)?$/.exec(v)?.slice(1).map(Number);
  const x = p(a), y = p(b);
  if (!x || !y) return -1;
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
}

/**
 * Checks an ESP-IDF app image the way esp_ota_end() + handlers_update.cpp do, as far as a mock can:
 * image magic, app description (project "keyra", version), and a Secure Boot V2 signature block
 * (magic 0xE7 in the last 4 KiB sector). The mock cannot check the signature itself.
 */
function fwStage(buf) {
  if (buf.length < 0x1000 || buf[0] !== 0xe9 || buf.readUInt32LE(0x20) !== 0xabcd5432) return 'bad_image';
  const str = (off) => buf.subarray(off, off + 32).toString('latin1').replace(/\0.*$/s, '');
  if (str(0x50) !== 'keyra') return 'bad_image';
  if (buf.length % 4096 !== 0 || buf[buf.length - 4096] !== 0xe7) return 'bad_signature';
  const version = str(0x30);
  if (cmpVersion(version, device.version) < 0) return 'downgrade';
  Object.assign(fw, { phase: 'staged', version });
  console.log(`[mock] update ${version} uploaded and verified`);
  return null;
}

function readRaw(req, cap) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > cap) {
        reject(new HttpError(413, 'too_large', 'Request body too large'));
        req.destroy();
      } else chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

// ---------- password health (SPEC §13; firmware keyra_api/src/health.cpp) ----------

// Same estimate and common list as web/src/lib/strength.ts.
const COMMON = new Set([
  'password', '123456', '12345678', '123456789', '1234567890', 'qwerty', 'qwertyuiop', 'keyra1234',
  'iloveyou', 'admin', 'welcome', 'letmein', 'monkey', 'dragon', 'football', 'baseball', 'abc123',
  '111111', '000000', '123123', '654321', 'sunshine', 'princess', 'master', 'shadow', 'superman',
  'trustno1', 'passw0rd', 'password1', 'password123', 'qwerty123', '1q2w3e4r', 'zaq12wsx', 'starwars',
  'whatever', 'freedom', 'hello123', 'login', 'access', 'secret', 'michael', 'charlie', 'jordan',
  'mustang', 'batman', 'computer', 'internet', 'samsung', 'google', 'asdfghjkl',
]);

function strengthLevel(pw) {
  if (!pw) return 0;
  if (COMMON.has(pw.toLowerCase())) return 1;
  const chars = Array.from(pw);
  let pool = 0;
  if (/[a-z]/.test(pw)) pool += 26;
  if (/[A-Z]/.test(pw)) pool += 26;
  if (/[0-9]/.test(pw)) pool += 10;
  if (/[!-/:-@[-`{-~ ]/.test(pw)) pool += 33;
  if (/[\u0600-\u06FF]/.test(pw)) pool += 36;
  if (pool === 0) pool = 33;
  let b = chars.length * Math.log2(pool);
  const cp = chars.map((ch) => ch.toLowerCase().codePointAt(0));
  let run = 1, seq = 1, prev = 0;
  for (let i = 1; i < cp.length; i++) {
    const d = cp[i] - cp[i - 1];
    run = d === 0 ? run + 1 : 1;
    if (run === 3) b -= 8;
    if (d === 1 || d === -1) seq = seq > 1 && d === prev ? seq + 1 : 2;
    else seq = 1;
    if (seq === 3) b -= 8;
    prev = d;
  }
  b = Math.max(0, b);
  return b < 36 ? 1 : b < 60 ? 2 : b < 80 ? 3 : 4;
}

function health(entries) {
  const now = timeValid ? nowSec() : 0;
  const withPw = entries.filter((e) => e.password);
  const weak = [], old = [], groups = new Map();
  for (const e of withPw) {
    const level = strengthLevel(e.password);
    if (level < 3) weak.push({ id: e.id, level });
    const since = e.history.length ? e.history[0].changedAt : e.created;
    if (now && since && now - since > 365 * 86400) old.push({ id: e.id, since });
    groups.set(e.password, [...(groups.get(e.password) ?? []), e.id]);
  }
  const reused = [...groups.values()].filter((g) => g.length > 1).map((g) => g.sort((a, b) => a - b));
  const byId = (a, b) => a.id - b.id;
  return { checked: withPw.length, clock: !!now, weak: weak.sort(byId), reused, old: old.sort(byId) };
}

// ---------- seed ----------

function seed() {
  const day = 86400;
  const now = nowSec();
  const rows = [
    ['Google', 'accounts.google.com', 'hasan.ali@gmail.com', 'Tigris-River-42!', 'otpauth://totp/Google:hasan.ali%40gmail.com?secret=JBSWY3DPEHPK3PXP&issuer=Google', '', true, 1],
    ['Microsoft', 'login.microsoftonline.com', 'hasan.ali@outlook.com', 'b9#Lw2-qPz7&Rk', '', '', false, 0],
    ['Apple ID', 'appleid.apple.com', 'hasan.ali@icloud.com', 'Orchard-Silver-77', 'otpauth://totp/Apple:hasan?secret=KRSXG5CTMVRXEZLU&issuer=Apple', '', true, 0],
    ['GitHub', 'github.com', 'hasanalaaa', 'gh!R3d-Lantern-Fox', 'otpauth://totp/GitHub:hasanalaaa?secret=GEZDGNBVGY3TQOJQ&issuer=GitHub', 'Recovery codes are in the safe.', true, 0.2],
    ['Instagram', 'instagram.com', 'hasan.baghdad', 'Palm-Date-Sunset-9', '', '', false, 2],
    ['بنك الرافدين', 'rafidain-bank.gov.iq', '0771 234 5678', 'Rf-2026-Secure#', '', 'رقم الحساب في دفتر الشيكات.', true, 0.1],
    ['زين العراق', 'iq.zain.com', '07801234567', 'Zain*Mobile88', '', '', false, 4],
    ['Netflix', 'netflix.com', 'family@hasan.iq', 'Movie-Night-Popcorn', '', '', false, 3],
    ['Steam', 'store.steampowered.com', 'hasan_gamer', 'St3am!Valve-Quest', 'otpauth://totp/Steam:hasan_gamer?secret=MFRGGZDFMZTWQ2LK&issuer=Steam', '', false, 1.5],
    ['Amazon', 'amazon.com', 'hasan.ali@gmail.com', 'Prime-Box-Delivery-5', '', '', false, 9],
    ['Facebook', 'facebook.com', 'hasan.ali.iq', 'Fb-Blue-Thumb-61', '', '', false, 0],
    ['WhatsApp Web', 'web.whatsapp.com', '+964 770 123 4567', '', '', 'Linked devices only — no password.', false, 0],
    ['X', 'x.com', 'hasanalaaa', 'Xx-Birdless-Sky-3', '', '', false, 0],
    ['LinkedIn', 'linkedin.com', 'hasan.ali@outlook.com', 'Career-Ladder-2026', '', '', false, 0],
    ['Dropbox', 'dropbox.com', 'hasan.ali@gmail.com', 'Prime-Box-Delivery-5', '', '', false, 0],
    ['PayPal', 'paypal.com', 'hasan.ali@gmail.com', 'Pp$Wallet-Green-12', 'otpauth://totp/PayPal:hasan?secret=NBSWY3DPO5XXE3DE&issuer=PayPal', '', false, 0],
    ['Spotify', 'spotify.com', 'hasan.music', 'Maqam-Rast-Oud-7', '', '', false, 0],
    ['Discord', 'discord.com', 'hasan#4821', 'Discord-Night-Owl', '', '', false, 6],
    ['آسيا سيل', 'asiacell.com', '07701234567', 'Asia-Cell-2026', '', '', false, 0],
    ['البريد الجامعي', 'mail.uobaghdad.edu.iq', 'h.ali@uobaghdad.edu.iq', 'Uni-Baghdad-Library', '', '', false, 0],
    ['كي كارد', 'qi.iq', '6014 •••• 1234', 'QiCard-PIN-Safe', '', '', false, 0],
    ['توترز', 'tooters.iq', 'hasan.ali', 'Food-Tooters-99', '', '', false, 0],
    ['Notion', 'notion.so', 'hasan.ali@gmail.com', 'Notes-Blocks-Pages', '', '', false, 0],
    ['Slack', 'keyra.slack.com', 'hasan@keyra.dev', 'Slack-Channel-Hash', '', '', false, 0],
    ['Router', '192.168.1.1', 'admin', 'admin1234', '', 'TP-Link in the living room.', false, 0],
  ];
  const entries = new Map();
  for (const [title, url, username, password, totp, notes, favorite, usedDaysAgo] of rows) {
    const id = freshId(entries);
    // Zain has kept its password for over a year (Password health: "old").
    const created = title === 'زين العراق' ? now - 500 * day : now - 200 * day + entries.size * day;
    entries.set(id, {
      id,
      title,
      url,
      username,
      password,
      totp,
      notes,
      favorite,
      created,
      updated: created,
      lastUsed: usedDaysAgo ? Math.round(now - usedDaysAgo * day) : 0,
      history: [],
    });
  }
  return entries;
}

// Passkeys (docs/FIDO.md): created by websites over USB on a real Keyra; the
// mock only lists and deletes them.
function seedPasskeys() {
  const now = nowSec();
  const day = 86400;
  const list = [
    { id: 3141592653, rpId: 'github.com', userName: 'hasan-dev', displayName: 'Hasan', created: now - 40 * day },
    { id: 2718281828, rpId: 'accounts.google.com', userName: 'hasan@gmail.com', displayName: 'Hasan Ali', created: now - 12 * day },
    { id: 1618033988, rpId: 'www.amazon.com', userName: 'hasan@example.com', displayName: '', created: now - 2 * day },
  ];
  return new Map(list.map((p) => [p.id, p]));
}

if (!FRESH) {
  vault = { passphrase: DEMO_PASSPHRASE, entries: seed(), passkeys: seedPasskeys() };
  settings.wifiPassword = 'Tigris-42-Kx9p';
  settings.lastBackupAt = nowSec() - Number(process.env.MOCK_BACKUP_DAYS ?? 3) * 86400;
  if (process.env.MOCK_BLE !== '0') ble.bonds.push({ addr: 'F0:2B:7C:41:9A:D3', name: 'MacBook Air', lastSeen: nowSec() - 86400 * 2, os: 'mac' });
}

// ---------- HTTP plumbing ----------

const SECURITY = {
  'Content-Security-Policy': "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:",
  'X-Frame-Options': 'DENY',
  'Referrer-Policy': 'no-referrer',
  'X-Content-Type-Options': 'nosniff',
};

class HttpError extends Error {
  constructor(status, code, message, extra = {}, headers = {}) {
    super(message);
    Object.assign(this, { status, code, extra, headers });
  }
}

const fail = (status, code, message, extra, headers) => {
  throw new HttpError(status, code, message, extra, headers);
};
const bad = (message) => fail(400, 'invalid', message);

function send(res, status, body, headers = {}) {
  const json = body === undefined ? '' : JSON.stringify(body);
  res.writeHead(status, {
    ...SECURITY,
    'Cache-Control': 'no-store',
    ...(json ? { 'Content-Type': 'application/json; charset=utf-8' } : {}),
    ...headers,
  });
  res.end(json);
}

function readBody(req, cap) {
  return new Promise((resolve, reject) => {
    const declared = Number(req.headers['content-length'] || 0);
    if (declared > cap) return reject(new HttpError(413, 'too_large', 'Request body too large'));
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > cap) {
        reject(new HttpError(413, 'too_large', 'Request body too large'));
        req.destroy();
      } else chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

const cookie = (req, name) =>
  (req.headers.cookie ?? '')
    .split(';')
    .map((p) => p.trim().split('='))
    .find(([k]) => k === name)?.[1];

function safeEqual(a, b) {
  const x = Buffer.from(String(a ?? ''));
  const y = Buffer.from(String(b ?? ''));
  return x.length === y.length && timingSafeEqual(x, y);
}

/** Same-origin rule of routes.cpp, generalised to "the Host this request came to". */
function originAllowed(req) {
  const origin = req.headers.origin;
  if (origin === undefined) return true;
  return origin === `http://${req.headers.host}`;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** SPEC §8.2 `via`: the firmware reads the socket's local address; the mock uses the Host. */
const viaOf = (req) => (VIA_HOME || /^127\.0\.0\.1(:|$)/.test(req.headers.host ?? '') ? 'home' : 'ap');
const sha = (s) => createHash('sha256').update(String(s)).digest('hex');
const KT_RE = /^[0-9a-f]{64}$/;

/** Short "Safari on iPhone"-style label, like trust.cpp browserName(). */
function browserName(ua = '') {
  const has = (x) => ua.includes(x);
  const b = has('Edg') ? 'Edge' : has('OPR/') ? 'Opera' : has('Firefox/') || has('FxiOS/') ? 'Firefox' : has('SamsungBrowser/') ? 'Samsung Internet' : has('CriOS/') || has('Chrome/') ? 'Chrome' : has('Safari/') ? 'Safari' : null;
  const p = has('iPhone') ? 'iPhone' : has('iPad') ? 'iPad' : has('Android') ? 'Android' : has('CrOS') ? 'ChromeOS' : has('Macintosh') ? 'Mac' : has('Windows') ? 'Windows' : has('Linux') ? 'Linux' : null;
  if (b && p) return `${b} on ${p}`;
  return b ?? (p ? `Browser on ${p}` : ua.replace(/[^\x20-\x7e]/g, '').slice(0, 32) || 'Browser');
}

function knownBrowser(req) {
  const kt = cookie(req, 'kt');
  return kt && KT_RE.test(kt) ? trusted.get(sha(kt)) : undefined;
}

/** Simulates the station joining (or failing to join) after a home_wifi commit. */
function applyHome() {
  clearTimeout(homeLink.timer);
  Object.assign(homeLink, { connected: false, ip: null, rssi: null, error: '' });
  const h = settings.homeWifi;
  if (!h.enabled) return;
  homeLink.timer = setTimeout(() => {
    if (h.password === 'wrong-password') {
      homeLink.error = 'wrong_password'; // like net::homeErrorFor(15)
      return console.log(`[mock] joining ${h.ssid} keeps failing (backoff)`);
    }
    const net = NETWORKS.find((n) => n.ssid === h.ssid);
    Object.assign(homeLink, { connected: true, ip: '192.168.1.42', rssi: net?.rssi ?? -60 });
    timeValid = true; // SNTP
    console.log(`[mock] joined ${h.ssid} as 192.168.1.42`);
  }, 2000);
}

// ---------- routes (routes.cpp matchApi) ----------

// Like percentDecode() in routes.cpp: %XX decoded; malformed, NUL or '/' → no match.
function decodePath(path) {
  if (!path.includes('%')) return path;
  if (/%(?![0-9A-Fa-f]{2})/.test(path) || /%(00|2[Ff])/.test(path)) return null;
  return path.replace(/%([0-9A-Fa-f]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16)));
}

function match(method, rawPath) {
  const path = decodePath(rawPath);
  if (path === null) return null;
  const p = path.slice('/api/'.length);
  const one = (want, route) => (method === want ? { route } : { notAllowed: true });
  switch (p) {
    case 'state': return one('GET', 'state');
    case 'setup': return one('POST', 'setup');
    case 'unlock': return one('POST', 'unlock');
    case 'unlock/recovery': return one('POST', 'unlockRecovery');
    case 'recovery':
      return method === 'GET' ? { route: 'getRecovery' } : method === 'POST' ? { route: 'createRecovery' } : method === 'DELETE' ? { route: 'deleteRecovery' } : { notAllowed: true };
    case 'lock': return one('POST', 'lock');
    case 'type': return one('POST', 'type');
    case 'generate': return one('POST', 'generate');
    case 'type/cancel': return one('POST', 'typeCancel');
    case 'presence/cancel': return one('POST', 'presenceCancel');
    case 'passphrase': return one('POST', 'passphrase');
    case 'backup': return one('POST', 'backup');
    case 'restore': return one('POST', 'restore');
    case 'factory-reset': return one('POST', 'factoryReset');
    case 'settings':
      return method === 'GET' ? { route: 'getSettings' } : method === 'PUT' ? { route: 'putSettings' } : { notAllowed: true };
    case 'entries':
      return method === 'GET' ? { route: 'list' } : method === 'POST' ? { route: 'create' } : { notAllowed: true };
    case 'entries/import': return one('POST', 'import');
    case 'wifi/scan': return one('GET', 'wifiScan');
    case 'wifi/home': return one('PUT', 'wifiHome');
    case 'trusted': return one('GET', 'trusted');
    case 'fido': return one('GET', 'passkeys');
    case 'update': return one('POST', 'fwUpload');
    case 'update/check': return one('POST', 'fwCheck');
    case 'update/download': return one('POST', 'fwDownload');
    case 'update/apply': return one('POST', 'fwApply');
    case 'health': return one('GET', 'health');
    case 'ble': return one('GET', 'ble');
    case 'ble/pair': return one('POST', 'blePair');
  }
  const bond = /^ble\/bonds\/(.*)$/.exec(p);
  if (bond) {
    if (!/^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/.test(bond[1])) return null;
    if (method === 'PUT') return { route: 'bleSetOs', addr: bond[1].toUpperCase() };
    return method === 'DELETE' ? { route: 'bleForget', addr: bond[1].toUpperCase() } : { notAllowed: true };
  }
  const fk = /^fido\/([0-9]{1,10})$/.exec(p);
  if (fk) return Number(fk[1]) > 0 && Number(fk[1]) <= 0xffffffff ? (method === 'DELETE' ? { route: 'deletePasskey', id: Number(fk[1]) } : { notAllowed: true }) : null;
  const tr = /^trusted\/([0-9]{1,10})$/.exec(p);
  if (tr) return Number(tr[1]) > 0 && Number(tr[1]) <= 0xffffffff ? (method === 'DELETE' ? { route: 'untrust', id: Number(tr[1]) } : { notAllowed: true }) : null;
  const m = /^entries\/([0-9]{1,10})(\/totp|\/reveal)?$/.exec(p);
  const id = m ? Number(m[1]) : 0;
  if (!m || id === 0 || id > 0xffffffff) return null;
  if (m[2] === '/totp') return method === 'GET' ? { route: 'totp', id } : { notAllowed: true };
  if (m[2] === '/reveal') return method === 'POST' ? { route: 'reveal', id } : { notAllowed: true };
  if (method === 'GET') return { route: 'get', id };
  if (method === 'PUT') return { route: 'update', id };
  if (method === 'DELETE') return { route: 'delete', id };
  return { notAllowed: true };
}

const OPEN = new Set(['state', 'setup', 'unlock', 'unlockRecovery', 'factoryReset', 'presenceCancel']);
const BODY = new Set(['setup', 'unlock', 'unlockRecovery', 'create', 'update', 'import', 'type', 'generate', 'putSettings', 'passphrase', 'backup', 'restore', 'wifiHome', 'bleSetOs', 'presenceCancel']);

const validPassphrase = (s) => typeof s === 'string' && [...s].length >= 10 && [...s].length <= 128;
const validWifi = (s) => typeof s === 'string' && s.length >= 8 && s.length <= 63 && /^[\x20-\x7e]+$/.test(s) && s !== 'keyra1234';
const validName = (s) => typeof s === 'string' && s.length > 0 && Buffer.byteLength(s) <= 32 && !/[\x00-\x1f\x7f]/.test(s);
const str = (b, k) => (typeof b[k] === 'string' ? b[k] : bad(`"${k}" (string) is required`));
const awaiting = (res, expiresIn) => send(res, 202, { awaiting: 'button', expiresIn, cancel: machine.slot?.cancel ?? '' });
const busy409 = () => fail(409, 'busy', "Keyra is waiting for another request; long-press its button to cancel it");

function getEntry(id) {
  return vault.entries.get(id) ?? fail(404, 'not_found', 'No such entry');
}

// SPEC §12.3: the entry with secrets, or without them (hasPassword/hasTotp, history dates only).
function entryView(e, revealed) {
  const { password, totp, history, ...rest } = e;
  if (revealed) return { ...rest, revealed, hasPassword: !!password, hasTotp: !!totp, password, totp, history };
  return { ...rest, revealed, hasPassword: !!password, hasTotp: !!totp, history: history.map((h) => ({ changedAt: h.changedAt })) };
}

const graceLeft = (sess) => (sess ? Math.max(0, (sess.graceUntil ?? 0) - Date.now()) : 0);
const mayReveal = (sess) => !settings.protectReveal || graceLeft(sess) > 0;
/** Arms `op`; the press opens this session's grace. */
function requestPress(res, op, token) {
  const exp = awaitPresence(op, () => {
    const s = sessions.get(token);
    if (!s) return false;
    s.graceUntil = Date.now() + GRACE_MS;
  });
  return send(res, 202, { awaiting: 'button', op, expiresIn: exp, cancel: machine.slot?.cancel ?? '' });
}

/** A new session for this browser (+ the renewed trust cookie). */
function issueSession(res, req, known) {
  unlocked = true;
  usbSeen = host.usb;
  lastActivity = Date.now();
  if (sessions.size >= MAX_SESSIONS) {
    const oldest = [...sessions.entries()].sort((x, y) => x[1].lastUsed - y[1].lastUsed)[0][0];
    sessions.delete(oldest);
  }
  const tok = randomBytes(32).toString('hex');
  const csrf = randomBytes(32).toString('hex');
  sessions.set(tok, { csrf, lastUsed: Date.now(), trustId: known?.id ?? 0, graceUntil: 0 });
  const cookies = [`ks=${tok}; HttpOnly; SameSite=Strict; Path=/`];
  if (known) cookies.push(`kt=${cookie(req, 'kt')}; HttpOnly; SameSite=Strict; Path=/; Max-Age=31536000`);
  return send(res, 200, { csrf }, { 'Set-Cookie': cookies });
}

function trustRequest(res, req) {
  const kt = randomBytes(32).toString('hex');
  const name = browserName(req.headers['user-agent']);
  const exp = awaitPresence(
    'trust_browser',
    () => {
      if (trusted.size >= MAX_TRUSTED) {
        const [h, old] = [...trusted.entries()].sort((x, y) => Math.max(x[1].lastSeen, x[1].created) - Math.max(y[1].lastSeen, y[1].created))[0];
        trusted.delete(h);
        for (const [tok, s] of sessions) if (s.trustId === old.id) sessions.delete(tok);
      }
      trusted.set(sha(kt), { id: randomBytes(4).readUInt32BE() || 1, name, created: nowSec(), lastSeen: nowSec() });
      console.log(`[mock] trusted ${name}`);
    },
    { tryOnly: true },
  );
  if (exp === null) busy409();
  return send(res, 202, { awaiting: 'button', op: 'trust_browser', expiresIn: exp, cancel: machine.slot?.cancel ?? '' }, { 'Set-Cookie': `kt=${kt}; HttpOnly; SameSite=Strict; Path=/` });
}

function throttle() {
  if (Date.now() < lockedUntil) {
    const retryAfterMs = lockedUntil - Date.now();
    fail(429, 'rate_limited', 'Too many attempts', { retryAfterMs }, { 'Retry-After': String(Math.ceil(retryAfterMs / 1000)) });
  }
  failures++;
}
function wrongAttempt(message) {
  const retryAfterMs = failures <= 4 ? 0 : Math.min(900, 2 ** (failures - 4)) * 1000;
  lockedUntil = Date.now() + retryAfterMs;
  fail(401, 'wrong', message, { retryAfterMs });
}

async function api(req, res, path) {
  const method = req.method;
  const via = viaOf(req);
  const cap = path === '/api/update' ? MAX_IMAGE : path === '/api/restore' ? MAX_RESTORE_BODY : MAX_BODY;
  if (Number(req.headers['content-length'] || 0) > cap) fail(413, 'too_large', 'Request body too large');
  const m = match(method, path);
  if (!m) fail(404, 'not_found', 'No such endpoint');
  if (m.notAllowed) fail(405, 'method_not_allowed', 'Method not allowed');
  if (method !== 'GET' && !originAllowed(req)) fail(403, 'csrf', 'Cross-origin request refused');

  expire();
  syncDemand();
  if (unlocked && Date.now() - lastActivity > settings.autoLockMin * 60000) {
    console.log('[mock] idle auto-lock');
    lockAll();
  }

  const token = cookie(req, 'ks');
  const sess = token ? sessions.get(token) : undefined;
  if (sess) sess.lastUsed = Date.now();
  const session = !!sess && unlocked;
  if (!OPEN.has(m.route)) {
    if (!session) fail(401, 'locked', 'Vault is locked');
    if (method !== 'GET' && !safeEqual(req.headers['x-keyra-csrf'], sess.csrf)) fail(403, 'csrf', 'Missing or invalid CSRF token');
    if (m.route !== 'totp' && m.route !== 'ble') lastActivity = Date.now(); // polls are not the user (handleApi)
  }

  let b = {};
  if (BODY.has(m.route)) {
    const raw = await readBody(req, cap);
    try {
      b = JSON.parse(raw);
    } catch {
      b = null;
    }
    if (!b || typeof b !== 'object' || Array.isArray(b)) bad('Body must be a JSON object');
  }

  switch (m.route) {
    case 'state': {
      const s = machine.slot;
      return send(res, 200, {
        device: { ...device, name: settings.deviceName },
        initialized: vault !== null,
        unlocked,
        session,
        autoLockMin: settings.autoLockMin,
        host: (() => {
          const next = pickTarget();
          const armed = session && s?.kind === 'type' && s.req.target.kind === 'ble' ? s.req.target.addr : null;
          const bond = ble.bonds.find((b) => b.addr === armed);
          return {
            usb: host.usb,
            ble: bleReady(),
            capsLock: host.capsLock,
            output: next.kind === 'none' ? null : next.kind,
            bleTarget: bond ? { addr: bond.addr, name: bond.name } : null,
            connecting: !!armed && ble.connected !== armed,
            usbOs: settings.osUsb,
          };
        })(),
        pending:
          session && s?.kind === 'type'
            ? { kind: 'type', id: s.req.id, title: s.req.title, what: s.req.what, submit: s.req.submit, expiresIn: s.deadline - Date.now(), target: targetText(s.req.target) }
            : null,
        last: session && machine.last ? { ...machine.last, at: Date.now() - machine.last.at } : null,
        presence: {
          awaiting: s?.kind === 'presence',
          op: s?.kind === 'presence' ? s.op : machine.running,
          expiresIn: s?.kind === 'presence' ? s.deadline - Date.now() : 0,
          result: machine.opResult
            ? { op: machine.opResult.op, ok: machine.opResult.code === 'done', code: machine.opResult.code, at: Date.now() - machine.opResult.at }
            : null,
        },
        net: {
          ap: { on: !(settings.apMode === 'fallback' && homeLink.connected), ssid: settings.wifiSsid || defaultSsid(), clients: via === 'ap' ? 1 : 0 },
          home: settings.homeWifi.enabled
            ? { enabled: true, connected: homeLink.connected, ssid: settings.homeWifi.ssid, ip: homeLink.ip, rssi: homeLink.rssi, error: homeLink.connected ? '' : homeLink.error }
            : null,
          via,
        },
        timeValid,
        graceMs: session ? graceLeft(sess) : 0,
        ...(session && fw.phase !== 'idle' ? { update: fwState() } : {}),
      });
    }

    case 'setup': {
      if (vault) fail(409, 'already_initialized', 'Keyra is already set up');
      const passphrase = str(b, 'passphrase');
      const wifiPassword = str(b, 'wifiPassword');
      if (b.deviceName !== undefined && !validName(b.deviceName)) bad('deviceName must be 1-32 bytes without control characters');
      if (!validPassphrase(passphrase)) bad('passphrase must be 10-128 characters');
      if (!validWifi(wifiPassword)) bad('wifiPassword must be 8-63 printable ASCII characters and not the default');
      const exp = awaitPresence(
        'setup',
        () => {
          vault = { passphrase, entries: new Map(), passkeys: new Map() };
          unlocked = true; // left unlocked for the client's unlock call
          lastActivity = Date.now();
          settings.wifiPassword = wifiPassword;
          if (b.deviceName) settings.deviceName = b.deviceName;
          console.log(`[mock] set up · Wi-Fi ${defaultSsid()} / ${wifiPassword}`);
        },
        { tryOnly: true },
      );
      return exp === null ? busy409() : awaiting(res, exp);
    }

    case 'unlock': {
      const pass = str(b, 'passphrase');
      if (Buffer.byteLength(pass) > 1024) bad('passphrase too long');
      if (!vault) fail(409, 'not_initialized', 'Keyra is not set up yet');
      throttle();
      await sleep(KDF_MS);
      if (pass !== vault.passphrase) wrongAttempt('Wrong passphrase');
      failures = 0;
      lockedUntil = 0;
      const known = knownBrowser(req);
      // SPEC §8.2: right passphrase, unknown browser on the home network → the button first.
      if (via === 'home' && !known) return trustRequest(res, req);
      if (known) known.lastSeen = nowSec();
      return issueSession(res, req, known);
    }

    case 'unlockRecovery': {
      // SPEC §12.2: the recovery key sets a new passphrase and unlocks.
      const key = str(b, 'recoveryKey');
      const next = str(b, 'next');
      if (!/^[0-9a-f]{40}$/.test(key)) bad('recoveryKey must be 40 hex characters');
      if (!validPassphrase(next)) bad('next must be 10-128 characters');
      if (!vault) fail(409, 'not_initialized', 'Keyra is not set up yet');
      throttle();
      await sleep(80);
      if (!vault.recovery || !safeEqual(key, vault.recovery.key)) wrongAttempt('Wrong recovery key');
      failures = 0;
      lockedUntil = 0;
      const known = knownBrowser(req);
      if (via === 'home' && !known) return trustRequest(res, req);
      vault.passphrase = next;
      console.log('[mock] unlocked with the recovery key; passphrase replaced');
      return issueSession(res, req, known);
    }

    case 'getRecovery':
      return send(res, 200, { enabled: !!vault.recovery, created: vault.recovery?.created ?? 0 });

    case 'createRecovery': {
      if (graceLeft(sess) <= 0) return requestPress(res, 'recovery', token);
      const key = randomBytes(20).toString('hex');
      vault.recovery = { key, created: nowSec() };
      return send(res, 200, { recoveryKey: key, created: vault.recovery.created });
    }

    case 'deleteRecovery':
      if (graceLeft(sess) <= 0) return requestPress(res, 'recovery', token);
      if (!vault.recovery) fail(404, 'not_found', 'No recovery key');
      vault.recovery = null;
      return send(res, 204);

    case 'lock':
      lockAll();
      return send(res, 204, undefined, { 'Set-Cookie': 'ks=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0' });

    case 'list':
      return send(res, 200, { entries: [...vault.entries.values()].map(summary) });

    case 'get':
      return send(res, 200, entryView(getEntry(m.id), mayReveal(sess)));

    case 'reveal': {
      const e = getEntry(m.id);
      if (!mayReveal(sess)) return requestPress(res, 'reveal', token);
      return send(res, 200, entryView(e, true));
    }

    case 'create': {
      const e = readEntry(b, true);
      if (typeof e === 'string') bad(e);
      if (!validEntry(e)) bad('Invalid data');
      if (vault.entries.size >= MAX_ENTRIES) fail(507, 'full', 'Vault is full');
      e.id = freshId();
      e.created ||= nowSec();
      e.updated ||= nowSec();
      e.history = []; // owned by the device, never taken from a client
      vault.entries.set(e.id, e);
      return send(res, 201, { id: e.id });
    }

    case 'update': {
      const old = getEntry(m.id);
      const e = readEntry(b, false, old);
      if (typeof e === 'string') bad(e);
      if (!validEntry(e)) bad('Invalid data');
      e.updated = nowSec();
      // Like Vault::put: a changed password moves the old one to the front of the history.
      e.history = old.password && e.password !== old.password ? [{ password: old.password, changedAt: e.updated }, ...old.history].slice(0, MAX_HISTORY) : old.history;
      vault.entries.set(m.id, e);
      return send(res, 200, { id: m.id });
    }

    case 'delete':
      getEntry(m.id);
      vault.entries.delete(m.id);
      return send(res, 204);

    case 'import': {
      if (!Array.isArray(b.entries)) bad('"entries" (array) is required');
      if (b.entries.length > 50) bad('at most 50 entries per request');
      const seen = new Set([...vault.entries.values()].map(dedupeKey));
      let added = 0;
      let skipped = 0;
      for (const item of b.entries) {
        const e = item && typeof item === 'object' && !Array.isArray(item) ? readEntry(item, true) : 'bad';
        if (typeof e === 'string' || !validEntry(e) || seen.has(dedupeKey(e))) {
          skipped++;
          continue;
        }
        if (vault.entries.size >= MAX_ENTRIES) fail(507, 'full', 'Vault is full');
        seen.add(dedupeKey(e));
        e.id = freshId();
        e.created ||= nowSec();
        e.updated ||= nowSec();
        vault.entries.set(e.id, e);
        added++;
      }
      return send(res, 200, { added, skipped });
    }

    case 'totp': {
      const e = getEntry(m.id);
      if (!e.totp) fail(404, 'not_found', 'Entry has no 2FA secret');
      if (!timeValid) fail(409, 'no_time', 'Device clock is not set');
      const c = totpCode(e.totp);
      if (!c) bad('2FA secret is not valid');
      return send(res, 200, c);
    }

    case 'type': {
      if (b.test !== undefined && typeof b.test !== 'boolean') bad('"test" must be a boolean');
      if (b.switchLang !== undefined && typeof b.switchLang !== 'boolean') bad('"switchLang" must be a boolean');
      if (b.switchLang) console.log('[mock] Ctrl+Space before and after typing (input language switch)');
      let target = pickTarget();
      if (b.target !== undefined) {
        if (b.target === 'usb') target = { kind: 'usb' };
        else if (typeof b.target === 'string' && /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/.test(b.target)) {
          if (!settings.bleEnabled) fail(409, 'ble_disabled', 'Bluetooth is turned off');
          const addr = b.target.toUpperCase();
          if (!ble.bonds.some((x) => x.addr === addr)) fail(404, 'not_found', 'No such device');
          target = { kind: 'ble', addr };
        } else bad('"target" must be "usb" or a device address');
      }
      if (b.text !== undefined) {
        // Free text (SPEC §9.2), like handlers_gen.cpp textRequest.
        if (['id', 'what', 'test', 'submit'].some((k) => b[k] !== undefined)) bad('"text" cannot be combined with id, what, test or submit');
        if (typeof b.text !== 'string') bad('"text" (string) is required');
        if (!/^[\x20-\x7e]{1,256}$/.test(b.text)) bad('text must be 1-256 characters Keyra can type (printable ASCII, no control characters)');
        if (b.repeat !== undefined && b.repeat !== 1 && b.repeat !== 2) bad('repeat must be 1 or 2');
        if (b.separator !== undefined && b.separator !== 'tab' && b.separator !== 'enter') bad('separator must be "tab" or "enter"');
        const req = { id: 0, title: null, what: 'text', submit: false, target, text: b.text, twice: b.repeat === 2, enterBetween: b.separator === 'enter' };
        const { text: _t, twice: _w, enterBetween: _e, ...pending } = arm(req);
        return send(res, 202, { pending: { kind: 'type', ...pending } });
      }
      if (b.test) return send(res, 202, { pending: { kind: 'type', ...arm({ id: 0, title: 'Keyra test', what: 'test', submit: false, target }) } });
      if (!Number.isInteger(b.id) || b.id < 1 || b.id > 0xffffffff) bad('"id" (entry id) is required');
      if (!['username', 'password', 'both', 'totp'].includes(b.what)) bad('"what" must be username, password, both or totp');
      if (b.submit !== undefined && typeof b.submit !== 'boolean') bad('"submit" must be a boolean');
      const submit = b.submit ?? (b.what === 'both' && settings.submitAfterBoth);
      const e = getEntry(b.id);
      const missing =
        (b.what === 'username' && !e.username) ||
        (b.what === 'password' && !e.password) ||
        (b.what === 'both' && (!e.username || !e.password)) ||
        (b.what === 'totp' && !e.totp);
      if (missing) bad('Entry has no value for that field');
      if (b.what === 'totp' && !timeValid) fail(409, 'no_time', 'Device clock is not set');
      return send(res, 202, { pending: { kind: 'type', ...arm({ id: e.id, title: e.title, what: b.what, submit, target }) } });
    }

    case 'generate': {
      const r = generate(b);
      if (typeof r === 'string') bad(r);
      return send(res, 200, r);
    }

    case 'presenceCancel': {
      // Like Machine::cancelPresence: only the named op, and nothing runs later.
      if (typeof b.op !== 'string') bad('op must name a presence operation');
      if (typeof b.cancel !== 'string') bad('cancel must be the token from the 202 answer');
      expire();
      const s = machine.slot;
      if (!(s?.kind === 'presence' && s.op === b.op && s.cancel && safeEqual(b.cancel, s.cancel)))
        fail(409, 'not_cancelled', 'Nothing of yours is waiting for the button');
      machine.opResult = { op: s.op, code: 'cancelled', at: Date.now() };
      machine.slot = null;
      console.log(`[mock] ${s.op} cancelled from the app`);
      return send(res, 204);
    }

    case 'typeCancel': {
      const s = machine.slot;
      if (s?.kind === 'type') {
        machine.last = { ok: false, code: 'cancelled', at: Date.now(), title: s.req.title, what: s.req.what };
        machine.slot = null;
        syncDemand();
      }
      return send(res, 204);
    }

    case 'getSettings':
      return send(res, 200, publicSettings());

    case 'putSettings': {
      const next = { ...settings };
      if (b.deviceName !== undefined) {
        if (!validName(b.deviceName)) bad('deviceName must be 1-32 bytes without control characters');
        next.deviceName = b.deviceName;
      }
      const int = (k, lo, hi, msg) => {
        if (b[k] === undefined) return;
        if (!Number.isInteger(b[k]) || b[k] < lo || b[k] > hi) bad(msg);
        next[k] = b[k];
      };
      int('autoLockMin', 1, 120, 'autoLockMin must be 1-120');
      int('keyDelayMs', 1, 100, 'keyDelayMs must be 1-100');
      int('ledBrightness', 0, 100, 'ledBrightness must be 0-100');
      if (b.bleEnabled !== undefined) {
        if (typeof b.bleEnabled !== 'boolean') bad('bleEnabled must be a boolean');
        next.bleEnabled = b.bleEnabled;
      }
      if (b.output !== undefined) {
        if (!['auto', 'usb', 'ble'].includes(b.output)) bad('output must be "auto", "usb" or "ble"');
        next.output = b.output;
      }
      if (b.osUsb !== undefined) {
        if (!OSES.includes(b.osUsb)) bad('osUsb must be "", "mac", "ios", "windows", "android" or "linux"');
        next.osUsb = b.osUsb;
      }
      if (b.bleConnect !== undefined) {
        if (!['on_demand', 'always'].includes(b.bleConnect)) bad('bleConnect must be "on_demand" or "always"');
        next.bleConnect = b.bleConnect;
      }
      if (b.bothSeparator !== undefined) {
        if (b.bothSeparator !== 'tab' && b.bothSeparator !== 'enter') bad('bothSeparator must be "tab" or "enter"');
        next.bothSeparator = b.bothSeparator;
      }
      if (b.submitAfterBoth !== undefined) {
        if (typeof b.submitAfterBoth !== 'boolean') bad('submitAfterBoth must be a boolean');
        next.submitAfterBoth = b.submitAfterBoth;
      }
      if (b.apMode !== undefined) {
        if (b.apMode !== 'always' && b.apMode !== 'fallback') bad('apMode must be "always" or "fallback"');
        next.apMode = b.apMode;
      }
      if (b.homeWifi !== undefined) bad('home Wi-Fi changes go through PUT /api/wifi/home');
      for (const k of ['lockOnUsb', 'lockOnBle', 'protectReveal']) {
        if (b[k] === undefined) continue;
        if (typeof b[k] !== 'boolean') bad(`${k} must be a boolean`);
        next[k] = b[k];
      }
      // Turning protection off waits for the button (SPEC §12.3); turning it on applies at once.
      const unprotect = settings.protectReveal && next.protectReveal === false;
      if (unprotect) next.protectReveal = true;
      let ssid = '';
      let pw = '';
      if (b.wifiSsid !== undefined) {
        if (!validName(b.wifiSsid)) bad('wifiSsid must be 1-32 bytes without control characters');
        if (b.wifiSsid !== (settings.wifiSsid || defaultSsid())) ssid = b.wifiSsid;
      }
      if (b.wifiPassword !== undefined) {
        if (!validWifi(b.wifiPassword)) bad('wifiPassword must be 8-63 printable ASCII characters and not the default');
        if (b.wifiPassword !== settings.wifiPassword) pw = b.wifiPassword;
      }
      settings = next;
      if (unprotect && !(ssid || pw)) {
        return awaiting(
          res,
          awaitPresence('unprotect', () => {
            settings.protectReveal = false;
          }),
        );
      }
      if (ssid || pw) {
        return awaiting(
          res,
          awaitPresence('wifi', () => {
            if (ssid) settings.wifiSsid = ssid === defaultSsid() ? '' : ssid;
            if (pw) settings.wifiPassword = pw;
            console.log(`[mock] Wi-Fi now ${settings.wifiSsid || defaultSsid()} / ${settings.wifiPassword}`);
          }),
        );
      }
      return send(res, 200, publicSettings());
    }

    case 'passphrase': {
      const cur = str(b, 'current');
      const nxt = str(b, 'next');
      if (Buffer.byteLength(cur) > 1024) bad('passphrase too long');
      if (!validPassphrase(nxt)) bad('next must be 10-128 characters');
      await sleep(KDF_MS);
      if (cur !== vault.passphrase) fail(401, 'wrong', 'Wrong passphrase');
      vault.passphrase = nxt;
      return send(res, 204);
    }

    case 'backup': {
      const pass = str(b, 'passphrase');
      if ([...pass].length < 12 || Buffer.byteLength(pass) > 1024) bad('backup passphrase must be at least 12 characters');
      if (!mayReveal(sess)) return requestPress(res, 'backup', token);
      settings.lastBackupAt = nowSec();
      const d = new Date();
      const ymd = `${d.getUTCFullYear()}${String(d.getUTCMonth() + 1).padStart(2, '0')}${String(d.getUTCDate()).padStart(2, '0')}`;
      const out = exportBackup(pass);
      res.writeHead(200, {
        ...SECURITY,
        'Content-Type': 'application/json',
        'Content-Disposition': `attachment; filename="keyra-backup-${ymd}.json"`,
        'Cache-Control': 'no-store',
      });
      return res.end(out);
    }

    case 'restore': {
      const pass = str(b, 'passphrase');
      const mode = str(b, 'mode');
      if (mode !== 'merge' && mode !== 'replace') bad('mode must be "merge" or "replace"');
      if (!b.backup || typeof b.backup !== 'object' || Array.isArray(b.backup)) bad('"backup" (object) is required');
      if (mode === 'replace') {
        return awaiting(
          res,
          awaitPresence('restore', () => {
            const list = openBackup(pass, b.backup);
            if (typeof list === 'string') return false;
            return typeof importBackup(list, true) !== 'string';
          }),
        );
      }
      await sleep(KDF_MS);
      const list = openBackup(pass, b.backup);
      if (list === 'wrong') fail(401, 'wrong', 'Wrong passphrase');
      if (list === 'invalid') bad('Invalid data');
      const r = importBackup(list, false);
      if (r === 'full') fail(507, 'full', 'Vault is full');
      return send(res, 200, r);
    }

    case 'wifiScan':
      await sleep(1800); // the radio scan
      return send(res, 200, { networks: NETWORKS });

    case 'wifiHome': {
      if (typeof b.enabled !== 'boolean') bad('"enabled" (boolean) is required');
      if (b.ssid !== undefined && !validName(b.ssid)) bad('ssid must be 1-32 bytes without control characters');
      if (b.password !== undefined && !(typeof b.password === 'string' && b.password.length >= 8 && b.password.length <= 63 && /^[\x20-\x7e]+$/.test(b.password)))
        bad('password must be 8-63 printable ASCII characters');
      const cur = settings.homeWifi;
      const ssid = b.ssid ?? cur.ssid;
      if (b.enabled) {
        if (!ssid) bad('ssid is required');
        if (!b.password && (ssid !== cur.ssid || !cur.password)) bad('password is required for a new network');
      }
      const exp = awaitPresence('home_wifi', () => {
        settings.homeWifi = { enabled: b.enabled, ssid: ssid || cur.ssid, password: b.password || cur.password };
        applyHome();
      });
      return send(res, 202, { awaiting: 'button', op: 'home_wifi', expiresIn: exp, cancel: machine.slot?.cancel ?? '' });
    }

    case 'trusted': {
      const mine = knownBrowser(req);
      return send(res, 200, { browsers: [...trusted.values()].map((t) => ({ ...t, current: t === mine })) });
    }

    case 'fwUpload': {
      if (fw.phase === 'receiving') fail(409, 'busy', 'Another update is in progress');
      const buf = await readRaw(req, MAX_IMAGE);
      if (!buf.length) bad('Send the firmware file as the request body');
      fwBegin('upload', buf.length);
      fw.done = buf.length;
      const err = fwStage(buf);
      if (err) fwFail(err);
      return send(res, 200, { version: fw.version });
    }

    case 'fwCheck': {
      if (!homeOnline()) fwFail('offline', false);
      return send(res, 200, { current: device.version, latest: MOCK_RELEASE.version, newer: cmpVersion(MOCK_RELEASE.version, device.version) > 0, size: MOCK_RELEASE.size, notes: MOCK_RELEASE.notes });
    }

    case 'fwDownload': {
      if (!homeOnline()) fwFail('offline', false);
      if (fw.phase === 'receiving') fail(409, 'busy', 'Another update is in progress');
      fwBegin('github', MOCK_RELEASE.size);
      // ~3 s of "download", then staged like the firmware's background task.
      const tick = setInterval(() => {
        fw.done = Math.min(fw.total, fw.done + Math.ceil(fw.total / 12));
        if (fw.done < fw.total) return;
        clearInterval(tick);
        if (cmpVersion(MOCK_RELEASE.version, device.version) < 0) return fwFail('downgrade', false, true);
        fw.phase = 'staged';
        fw.version = MOCK_RELEASE.version;
        console.log(`[mock] update ${fw.version} downloaded and verified`);
      }, 250);
      return send(res, 202, { downloading: true });
    }

    case 'fwApply': {
      if (fw.phase !== 'staged') fail(409, 'not_staged', 'No verified update is waiting');
      const version = fw.version;
      const exp = awaitPresence('update', () => {
        console.log(`[mock] installing ${version}; restarting`);
        // Like a reboot into the new firmware: every session ends, the vault locks.
        setTimeout(() => {
          device.version = version;
          fw.phase = 'idle';
          lockAll();
        }, 2000);
      });
      return send(res, 202, { awaiting: 'button', op: 'update', expiresIn: exp, cancel: machine.slot?.cancel ?? '', version });
    }

    case 'health':
      return send(res, 200, health([...vault.entries.values()]));

    case 'passkeys': {
      const list = [...vault.passkeys.values()].sort((a, b) => b.created - a.created);
      return send(res, 200, { passkeys: list, max: 50 });
    }

    case 'deletePasskey': {
      if (!vault.passkeys.delete(m.id)) fail(404, 'not_found', 'No such passkey');
      console.log(`[mock] passkey ${m.id} deleted`);
      return send(res, 204);
    }

    case 'untrust': {
      const entry = [...trusted.entries()].find(([, t]) => t.id === m.id);
      if (!entry) fail(404, 'not_found', 'No such trusted browser');
      const wasMine = knownBrowser(req) === entry[1];
      trusted.delete(entry[0]);
      for (const [tok, s] of sessions) if (s.trustId === m.id) sessions.delete(tok);
      return send(res, 204, undefined, wasMine ? { 'Set-Cookie': 'kt=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0' } : {});
    }

    case 'factoryReset': {
      const exp = awaitPresence(
        'factory_reset',
        () => {
          lockAll();
          vault = null;
          settings = defaultSettings();
          Object.assign(ble, { pairingUntil: 0, bonds: [], connected: null, wanted: null });
          trusted.clear();
          applyHome();
          failures = 0;
          lockedUntil = 0;
          machine.last = null;
          console.log('[mock] factory reset');
        },
        { tryOnly: true },
      );
      return exp === null ? busy409() : awaiting(res, exp);
    }

    case 'ble': {
      const peer = (b) => ({ addr: b.addr, name: b.name });
      const live = ble.bonds.find((x) => x.addr === ble.connected);
      return send(res, 200, {
        enabled: settings.bleEnabled,
        pairing: { active: pairing(), expiresIn: pairing() ? ble.pairingUntil - Date.now() : 0 },
        connected: settings.bleEnabled && live ? peer(live) : null,
        bonds: ble.bonds.map((b) => ({ ...peer(b), lastSeen: b.lastSeen, os: b.os })),
      });
    }

    case 'blePair': {
      if (!settings.bleEnabled) fail(409, 'ble_disabled', 'Bluetooth is turned off');
      if (ble.bonds.length >= MAX_BONDS) fail(409, 'bonds_full', 'Keyra already knows 4 devices; forget one first');
      return awaiting(
        res,
        awaitPresence('ble_pair', () => {
          ble.pairingUntil = Date.now() + PAIR_WINDOW_MS;
          console.log('[mock] Bluetooth pairing window open for 120 s');
          if (AUTO_BUTTON) setTimeout(() => blePair("Hasan's iPad"), 2000);
        }),
      );
    }

    case 'bleSetOs': {
      const bnd = ble.bonds.find((x) => x.addr === m.addr);
      if (!bnd) fail(404, 'not_found', 'No such device');
      if (!OSES.includes(b.os)) bad('os must be "", "mac", "ios", "windows", "android" or "linux"');
      bnd.os = b.os;
      return send(res, 204);
    }

    case 'bleForget': {
      const i = ble.bonds.findIndex((x) => x.addr === m.addr);
      if (i < 0) fail(404, 'not_found', 'No such device');
      if (ble.connected === m.addr) ble.connected = null;
      ble.bonds.splice(i, 1);
      return send(res, 204);
    }
  }
  return fail(404, 'not_found', 'No such endpoint');
}

const publicSettings = () => ({
  deviceName: settings.deviceName,
  wifiSsid: settings.wifiSsid || defaultSsid(),
  autoLockMin: settings.autoLockMin,
  keyDelayMs: settings.keyDelayMs,
  bothSeparator: settings.bothSeparator,
  submitAfterBoth: settings.submitAfterBoth,
  ledBrightness: settings.ledBrightness,
  bleEnabled: settings.bleEnabled,
  output: settings.output,
  bleConnect: settings.bleConnect,
  osUsb: settings.osUsb,
  homeWifi: { enabled: settings.homeWifi.enabled, ssid: settings.homeWifi.ssid },
  apMode: settings.apMode,
  protectReveal: settings.protectReveal,
  lockOnUsb: settings.lockOnUsb,
  lockOnBle: settings.lockOnBle,
  lastBackupAt: settings.lastBackupAt,
});

// ---------- static (exactly what the firmware embeds) + captive probes ----------

const ASSETS = {
  '/': ['index.html', 'text/html; charset=utf-8'],
  '/index.html': ['index.html', 'text/html; charset=utf-8'],
  '/manifest.webmanifest': ['manifest.webmanifest', 'application/manifest+json'],
  '/icon-192.png': ['icon-192.png', 'image/png'],
  '/icon-512.png': ['icon-512.png', 'image/png'],
  '/apple-touch-icon.png': ['apple-touch-icon.png', 'image/png'],
  '/favicon.svg': ['favicon.svg', 'image/svg+xml'],
};

const PROBES = {
  '/hotspot-detect.html': [200, 'text/html', '<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>'],
  '/library/test/success.html': [200, 'text/html', '<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>'],
  '/generate_204': [204, 'text/plain', ''],
  '/gen_204': [204, 'text/plain', ''],
  '/connecttest.txt': [200, 'text/plain', 'Microsoft Connect Test'],
  '/ncsi.txt': [200, 'text/plain', 'Microsoft NCSI'],
  '/success.txt': [200, 'text/plain', 'success'],
};

function serveStatic(req, res, path) {
  const probe = PROBES[path];
  if (probe) {
    res.writeHead(probe[0], { 'Content-Type': probe[1], 'Cache-Control': 'no-store' });
    return res.end(probe[2]);
  }
  const asset = req.method === 'GET' ? ASSETS[path] : undefined;
  const file = asset && DIST + asset[0];
  if (!file || !existsSync(file)) {
    const hint = asset ? 'Run `npm run build` first (or use `npm run dev`).' : 'Not found';
    return send(res, 404, { error: 'not_found', message: hint });
  }
  res.writeHead(200, {
    ...SECURITY,
    'Content-Type': asset[1],
    'Cache-Control': asset[0] === 'index.html' ? 'no-cache' : 'public, max-age=31536000',
  });
  res.end(readFileSync(file));
}

// ---------- USB host (SPEC §12.4: host binding and auto-lock on unplug) ----------

function setUsb(on) {
  if (on === host.usb) return;
  host.usb = on;
  if (on) {
    usbSession++;
    if (unlocked) usbSeen = true;
    return;
  }
  const s = machine.slot;
  if (s?.kind === 'type' && s.req.usbSession) {
    machine.last = { ok: false, code: 'host_changed', at: Date.now(), title: s.req.title, what: s.req.what };
    machine.slot = null;
  }
  // The firmware waits 1 s to ride out a bus reset; the mock locks at once.
  if (unlocked && usbSeen && settings.lockOnUsb) {
    console.log('[mock] USB host gone: auto-lock');
    lockAll();
  }
}

// ---------- mock controls ----------

async function mockControl(req, res, path) {
  if (req.method !== 'POST') return send(res, 405, { error: 'method_not_allowed', message: 'Method not allowed' });
  let b;
  try {
    b = JSON.parse((await readBody(req, MAX_BODY)) || '{}');
  } catch {
    return send(res, 400, { error: 'invalid', message: 'Body must be a JSON object' });
  }
  if (path === '/__mock/button' && (b.press === 'short' || b.press === 'long')) {
    const what = press(b.press);
    console.log(`[mock] button ${b.press}: ${what}`);
    return send(res, 200, { result: what });
  }
  if (path === '/__mock/ble' && typeof b.pair === 'string') {
    const ok = blePair(b.pair);
    return send(res, ok ? 200 : 409, { paired: ok });
  }
  if (path === '/__mock/ble' && typeof b.connected === 'boolean') {
    if (b.connected) connectBle(ble.wanted ?? ble.bonds[0]?.addr);
    else ble.connected = null;
    return send(res, 200, { connected: ble.connected });
  }
  if (path === '/__mock/ble' && typeof b.autoConnect === 'boolean') {
    ble.autoConnect = b.autoConnect;
    return send(res, 200, { autoConnect: ble.autoConnect });
  }
  if (path === '/__mock/usb' && typeof b.usb === 'boolean') {
    setUsb(b.usb);
    return send(res, 200, { usb: host.usb, unlocked });
  }
  return send(res, 400, { error: 'invalid', message: 'POST /__mock/button {press}, /__mock/usb {usb}, /__mock/ble {pair|connected}' });
}

// ---------- server ----------

const server = createServer(async (req, res) => {
  const path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const t = Number(req.headers['x-keyra-time']);
  if (Number.isFinite(t) && t > 1.6e12) timeValid = true; // the device adopts the client clock
  try {
    if (path.startsWith('/api/')) await api(req, res, path);
    else if (path.startsWith('/__mock/')) await mockControl(req, res, path);
    else serveStatic(req, res, path);
  } catch (e) {
    if (e instanceof HttpError) send(res, e.status, { error: e.code, message: e.message, ...e.extra }, e.headers);
    else {
      console.error('[mock]', e);
      send(res, 500, { error: 'storage', message: 'Storage error' });
    }
  }
});

server.listen(PORT, () => {
  console.log(`Keyra mock on http://localhost:${PORT}`);
  console.log(FRESH ? '  uninitialized (onboarding)' : `  seeded vault · passphrase: "${DEMO_PASSPHRASE}"`);
  if (AUTO_BUTTON) console.log('  auto-approving the button after 3 s');
});
