// Node mock of the Keyra device API (SPEC §5), for UI development, screenshots and e2e.
// Behaviour mirrors firmware/components/keyra_api (routes, auth, error codes, the one-slot
// pending-action machine) closely enough that the web app cannot tell the difference.
//
//   npm run mock                       seeded, initialized vault (passphrase below)
//   MOCK_FRESH=1 npm run mock          uninitialized device (onboarding)
//   MOCK_AUTO_BUTTON=1 npm run mock    approves every pending item 3 s after it is armed
//   MOCK_USB=0                         start "not plugged in"
//   PORT=8787                          listen port
//
// Simulated hardware: POST /__mock/button {press:"short"|"long"} · POST /__mock/usb {usb:bool}
import { createServer } from 'node:http';
import { createCipheriv, createDecipheriv, createHmac, pbkdf2Sync, randomBytes, timingSafeEqual } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const PORT = Number(process.env.PORT || 8787);
const FRESH = process.env.MOCK_FRESH === '1';
const AUTO_BUTTON = process.env.MOCK_AUTO_BUTTON === '1';
const DEMO_PASSPHRASE = 'keyra demo vault';

const EXPIRY_MS = 60000;
const KDF_MS = 450; // the device spends ≈1.2 s; keep the mock snappy but visibly async
const MAX_BODY = 64 * 1024;
const MAX_RESTORE_BODY = 2 * 1024 * 1024;
const MAX_ENTRIES = 1000;
const MAX_SESSIONS = 4;
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
});

const host = { usb: process.env.MOCK_USB !== '0', capsLock: false };
let settings = defaultSettings();
let vault = null; // { passphrase, entries: Map<id, Entry> } once initialized
let unlocked = false;
let failures = 0;
let lockedUntil = 0;
let timeValid = false;
let lastActivity = Date.now();
const sessions = new Map(); // token → { csrf, lastUsed }

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
  if (s.kind === 'type') machine.last = { ok: false, code: 'expired', at: s.deadline, title: s.req.title, what: s.req.what };
  else machine.opResult = { op: s.op, code: 'expired', at: s.deadline };
  machine.slot = null;
}

function arm(req) {
  machine.slot = { kind: 'type', req, deadline: Date.now() + EXPIRY_MS };
  autoPress(machine.slot);
  return { ...req, expiresIn: EXPIRY_MS };
}

/** awaitPresence replaces the slot; tryAwaitPresence (no session) never displaces anything. */
function awaitPresence(op, commit, { tryOnly = false } = {}) {
  expire();
  if (tryOnly && (machine.slot || machine.running)) return null;
  machine.slot = { kind: 'presence', op, commit, deadline: Date.now() + EXPIRY_MS };
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
  } else if (s.op === 'wifi' || s.op === 'restore') {
    machine.opResult = { op: s.op, code: 'cancelled', at: Date.now() };
    machine.slot = null;
  }
}

function lockAll() {
  unlocked = false;
  sessions.clear();
  dropSessionItems();
}

function press(kind) {
  expire();
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
    if (s?.kind === 'type' && !machine.typing) {
      machine.slot = null;
      machine.typing = true;
      const text = typedText(s.req);
      setTimeout(() => {
        machine.typing = false;
        let code = 'typed';
        if (!host.usb) code = 'no_usb';
        else if (text === null) code = 'failed';
        else if (/[^\x20-\x7e\t\n]/.test(text)) code = 'unsupported_char';
        machine.last = { ok: code === 'typed', code, at: Date.now(), title: s.req.title, what: s.req.what };
        const e = vault?.entries.get(s.req.id);
        if (code === 'typed' && e) e.lastUsed = nowSec();
      }, 250 + Math.min(1500, (text?.length ?? 0) * settings.keyDelayMs));
      return `typing ${s.req.what} · ${s.req.title}`;
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
    v: 1,
    kdf: { alg: 'pbkdf2-sha256', iter: BACKUP_ITER, salt: salt.toString('base64') },
    iv: iv.toString('base64'),
    data: data.toString('base64'),
  });
}

/** → entries array, or 'invalid' / 'wrong'. */
function openBackup(pass, b) {
  if (b?.format !== 'keyra-backup' || b.v !== 1 || b.kdf?.alg !== 'pbkdf2-sha256' || !Number.isInteger(b.kdf.iter)) return 'invalid';
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
function readEntry(src, withTimestamps, base = { title: '', url: '', username: '', password: '', totp: '', notes: '', favorite: false, created: 0, updated: 0, lastUsed: 0 }) {
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
    ['Dropbox', 'dropbox.com', 'hasan.ali@gmail.com', 'Box-Of-Files-88!', '', '', false, 0],
    ['PayPal', 'paypal.com', 'hasan.ali@gmail.com', 'Pp$Wallet-Green-12', 'otpauth://totp/PayPal:hasan?secret=NBSWY3DPO5XXE3DE&issuer=PayPal', '', false, 0],
    ['Spotify', 'spotify.com', 'hasan.music', 'Maqam-Rast-Oud-7', '', '', false, 0],
    ['Discord', 'discord.com', 'hasan#4821', 'Discord-Night-Owl', '', '', false, 6],
    ['آسيا سيل', 'asiacell.com', '07701234567', 'Asia-Cell-2026', '', '', false, 0],
    ['البريد الجامعي', 'mail.uobaghdad.edu.iq', 'h.ali@uobaghdad.edu.iq', 'Uni-Baghdad-Library', '', '', false, 0],
    ['كي كارد', 'qi.iq', '6014 •••• 1234', 'QiCard-PIN-Safe', '', '', false, 0],
    ['توترز', 'tooters.iq', 'hasan.ali', 'Food-Tooters-99', '', '', false, 0],
    ['Notion', 'notion.so', 'hasan.ali@gmail.com', 'Notes-Blocks-Pages', '', '', false, 0],
    ['Slack', 'keyra.slack.com', 'hasan@keyra.dev', 'Slack-Channel-Hash', '', '', false, 0],
    ['Router', '192.168.1.1', 'admin', 'Home-Router-Ü', '', 'TP-Link in the living room.', false, 0],
  ];
  const entries = new Map();
  for (const [title, url, username, password, totp, notes, favorite, usedDaysAgo] of rows) {
    const id = freshId(entries);
    const created = now - 200 * day + entries.size * day;
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
    });
  }
  return entries;
}

if (!FRESH) {
  vault = { passphrase: DEMO_PASSPHRASE, entries: seed() };
  settings.wifiPassword = 'Tigris-42-Kx9p';
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

// ---------- routes (routes.cpp matchApi) ----------

function match(method, path) {
  const p = path.slice('/api/'.length);
  const one = (want, route) => (method === want ? { route } : { notAllowed: true });
  switch (p) {
    case 'state': return one('GET', 'state');
    case 'setup': return one('POST', 'setup');
    case 'unlock': return one('POST', 'unlock');
    case 'lock': return one('POST', 'lock');
    case 'type': return one('POST', 'type');
    case 'type/cancel': return one('POST', 'typeCancel');
    case 'passphrase': return one('POST', 'passphrase');
    case 'backup': return one('POST', 'backup');
    case 'restore': return one('POST', 'restore');
    case 'factory-reset': return one('POST', 'factoryReset');
    case 'settings':
      return method === 'GET' ? { route: 'getSettings' } : method === 'PUT' ? { route: 'putSettings' } : { notAllowed: true };
    case 'entries':
      return method === 'GET' ? { route: 'list' } : method === 'POST' ? { route: 'create' } : { notAllowed: true };
    case 'entries/import': return one('POST', 'import');
  }
  const m = /^entries\/([0-9]{1,10})(\/totp)?$/.exec(p);
  const id = m ? Number(m[1]) : 0;
  if (!m || id === 0 || id > 0xffffffff) return null;
  if (m[2]) return method === 'GET' ? { route: 'totp', id } : { notAllowed: true };
  if (method === 'GET') return { route: 'get', id };
  if (method === 'PUT') return { route: 'update', id };
  if (method === 'DELETE') return { route: 'delete', id };
  return { notAllowed: true };
}

const OPEN = new Set(['state', 'setup', 'unlock', 'factoryReset']);
const BODY = new Set(['setup', 'unlock', 'create', 'update', 'import', 'type', 'putSettings', 'passphrase', 'backup', 'restore']);

const validPassphrase = (s) => typeof s === 'string' && [...s].length >= 10 && [...s].length <= 128;
const validWifi = (s) => typeof s === 'string' && s.length >= 8 && s.length <= 63 && /^[\x20-\x7e]+$/.test(s) && s !== 'keyra1234';
const validName = (s) => typeof s === 'string' && s.length > 0 && Buffer.byteLength(s) <= 32 && !/[\x00-\x1f\x7f]/.test(s);
const str = (b, k) => (typeof b[k] === 'string' ? b[k] : bad(`"${k}" (string) is required`));
const awaiting = (res, expiresIn) => send(res, 202, { awaiting: 'button', expiresIn });
const busy409 = () => fail(409, 'busy', "Keyra is waiting for another request; long-press its button to cancel it");

function getEntry(id) {
  return vault.entries.get(id) ?? fail(404, 'not_found', 'No such entry');
}

async function api(req, res, path) {
  const method = req.method;
  const cap = path === '/api/restore' ? MAX_RESTORE_BODY : MAX_BODY;
  if (Number(req.headers['content-length'] || 0) > cap) fail(413, 'too_large', 'Request body too large');
  const m = match(method, path);
  if (!m) fail(404, 'not_found', 'No such endpoint');
  if (m.notAllowed) fail(405, 'method_not_allowed', 'Method not allowed');
  if (method !== 'GET' && !originAllowed(req)) fail(403, 'csrf', 'Cross-origin request refused');

  expire();
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
    lastActivity = Date.now();
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
        host,
        pending:
          session && s?.kind === 'type'
            ? { kind: 'type', id: s.req.id, title: s.req.title, what: s.req.what, submit: s.req.submit, expiresIn: s.deadline - Date.now() }
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
        timeValid,
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
          vault = { passphrase, entries: new Map() };
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
      if (Date.now() < lockedUntil) {
        const retryAfterMs = lockedUntil - Date.now();
        fail(429, 'rate_limited', 'Too many attempts', { retryAfterMs }, { 'Retry-After': String(Math.ceil(retryAfterMs / 1000)) });
      }
      failures++;
      await sleep(KDF_MS);
      if (pass !== vault.passphrase) {
        const retryAfterMs = failures <= 4 ? 0 : Math.min(900, 2 ** (failures - 4)) * 1000;
        lockedUntil = Date.now() + retryAfterMs;
        fail(401, 'wrong', 'Wrong passphrase', { retryAfterMs });
      }
      failures = 0;
      lockedUntil = 0;
      unlocked = true;
      lastActivity = Date.now();
      if (sessions.size >= MAX_SESSIONS) {
        const oldest = [...sessions.entries()].sort((x, y) => x[1].lastUsed - y[1].lastUsed)[0][0];
        sessions.delete(oldest);
      }
      const tok = randomBytes(32).toString('hex');
      const csrf = randomBytes(32).toString('hex');
      sessions.set(tok, { csrf, lastUsed: Date.now() });
      return send(res, 200, { csrf }, { 'Set-Cookie': `ks=${tok}; HttpOnly; SameSite=Strict; Path=/` });
    }

    case 'lock':
      lockAll();
      return send(res, 204, undefined, { 'Set-Cookie': 'ks=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0' });

    case 'list':
      return send(res, 200, { entries: [...vault.entries.values()].map(summary) });

    case 'get':
      return send(res, 200, { ...getEntry(m.id) });

    case 'create': {
      const e = readEntry(b, true);
      if (typeof e === 'string') bad(e);
      if (!validEntry(e)) bad('Invalid data');
      if (vault.entries.size >= MAX_ENTRIES) fail(507, 'full', 'Vault is full');
      e.id = freshId();
      e.created ||= nowSec();
      e.updated ||= nowSec();
      vault.entries.set(e.id, e);
      return send(res, 201, { id: e.id });
    }

    case 'update': {
      const e = readEntry(b, false, getEntry(m.id));
      if (typeof e === 'string') bad(e);
      if (!validEntry(e)) bad('Invalid data');
      e.updated = nowSec();
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
      if (b.test) return send(res, 202, { pending: { kind: 'type', ...arm({ id: 0, title: 'Keyra test', what: 'test', submit: false }) } });
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
      return send(res, 202, { pending: { kind: 'type', ...arm({ id: e.id, title: e.title, what: b.what, submit }) } });
    }

    case 'typeCancel': {
      const s = machine.slot;
      if (s?.kind === 'type') {
        machine.last = { ok: false, code: 'cancelled', at: Date.now(), title: s.req.title, what: s.req.what };
        machine.slot = null;
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
      if (b.bothSeparator !== undefined) {
        if (b.bothSeparator !== 'tab' && b.bothSeparator !== 'enter') bad('bothSeparator must be "tab" or "enter"');
        next.bothSeparator = b.bothSeparator;
      }
      if (b.submitAfterBoth !== undefined) {
        if (typeof b.submitAfterBoth !== 'boolean') bad('submitAfterBoth must be a boolean');
        next.submitAfterBoth = b.submitAfterBoth;
      }
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

    case 'factoryReset': {
      const exp = awaitPresence(
        'factory_reset',
        () => {
          lockAll();
          vault = null;
          settings = defaultSettings();
          failures = 0;
          lockedUntil = 0;
          machine.last = null;
          console.log('[mock] factory reset');
        },
        { tryOnly: true },
      );
      return exp === null ? busy409() : awaiting(res, exp);
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
  if (path === '/__mock/usb' && typeof b.usb === 'boolean') {
    host.usb = b.usb;
    return send(res, 200, { usb: host.usb });
  }
  return send(res, 400, { error: 'invalid', message: 'POST /__mock/button {press:"short"|"long"} or /__mock/usb {usb:boolean}' });
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
