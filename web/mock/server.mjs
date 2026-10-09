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
//   MOCK_HOME_ONLINE=1                 Keyra is on the home network (Firmware update can check GitHub)
//   MOCK_BACKUP_DAYS=3                 days since the seeded vault's last backup (> 30 shows the reminder)
//   PORT=8787                          listen port
//
// Simulated hardware: POST /__mock/button {press:"short"|"long"} · POST /__mock/usb {usb:bool} (unplugging
//   a computer that was plugged in while unlocked auto-locks, SPEC §12.4)
//   POST /__mock/ble {pair:"<device name>"} (a device pairs while the window is open) · {connected:bool}
//     · {autoConnect:bool} (default true: the wanted device connects ~1.5 s after an action is armed)
//   POST /__mock/fido {pinSet:bool, pinRetries?:0-8} (the computer set, changed or used the security key PIN)
//   POST /__mock/sun {id, ctr?} → {url}: what an NTAG 424 DNA secure tag (SPEC §18) would open on its next read
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
  bothSequence: '', // SPEC §10.4: '' = built in
  layoutUsb: 'us', // SPEC §10.1
  layoutBle: 'us',
  // password is write-only, never sent; MOCK_HOME_ONLINE starts already joined
  homeWifi: process.env.MOCK_HOME_ONLINE === '1' ? { enabled: true, ssid: 'Al-Rashid Home', password: 'home-wifi-pass' } : { enabled: false, ssid: '', password: '' },
  apMode: 'always',
  protectReveal: true,
  passkeysInBackup: true, // docs/research/PASSKEY-BACKUP.md
  lockOnUsb: true,
  rotateSince: 0, // SPEC §13.1
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
let failedBefore = 0; // wrong guesses before the latest successful unlock (Vault::failedBeforeUnlock)
let lockedUntil = 0;
let timeValid = false;
let lastActivity = Date.now();
const sessions = new Map(); // token → { csrf, lastUsed, trustId, grace: { reveal|backup|recovery: until } }
const GRACE_MS = 60000; // SPEC §12.3: after a press, this session may see secrets this long
let usbSeen = false; // a computer was plugged in since the unlock (charger-only never locks)
let usbSession = 1; // bumps on every plug-in; a USB action is bound to the one it was armed on
const trusted = new Map(); // sha256(kt) → { id, name, created, lastSeen }
const MAX_TRUSTED = 8;
// MOCK_HOME_ONLINE=1: already on the home network (update checks need the internet, SPEC §14).
const homeLink = process.env.MOCK_HOME_ONLINE === '1' ? { connected: true, ip: '192.168.1.50', rssi: -48, error: '', timer: null } : { connected: false, ip: null, rssi: null, error: '', timer: null };
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

// Machine::track (SPEC §17): every armed item gets a serial; how it ended is kept by serial.
let serialSeq = 0;
const outcomes = new Map(); // serial → { code, presence }
function noteOutcome(serial, code, presence) {
  if (!serial) return;
  outcomes.set(serial, { code, presence });
  if (outcomes.size > 8) outcomes.delete(outcomes.keys().next().value);
}

function expire(now = Date.now()) {
  const s = machine.slot;
  if (!s || now < s.deadline) return;
  if (s.kind === 'type') {
    // A Bluetooth host that never connected is the better explanation.
    const code = s.req.target.kind === 'ble' && ble.connected !== s.req.target.addr ? 'no_host' : 'expired';
    machine.last = { ok: false, code, at: s.deadline, title: s.req.title, what: s.req.what };
    noteOutcome(s.req.serial, code, false);
  } else {
    machine.opResult = { op: s.op, code: 'expired', at: s.deadline };
    noteOutcome(s.serial, 'expired', true);
  }
  machine.slot = null;
}

// The session the current request came from (Machine::arm's `owner`); '' = none.
let requester = '';

/**
 * Like Machine::takeSlotLocked: the slot is taken only when free or holding an item of the same
 * session, so another browser can never swap what the user is about to approve. Replaced items
 * of the same session end as cancelled.
 */
function takeSlot(owner, presence) {
  expire();
  if (presence && machine.running) return false;
  const s = machine.slot;
  if (!s) return true;
  if (!owner || owner !== s.owner) return false;
  endCancelled(s);
  machine.slot = null;
  return true;
}

/** Records a waiting item as cancelled (type → last, presence → opResult). */
function endCancelled(s) {
  if (s.kind === 'type') {
    machine.last = { ok: false, code: 'cancelled', at: Date.now(), title: s.req.title, what: s.req.what };
    noteOutcome(s.req.serial, 'cancelled', false);
  } else {
    machine.opResult = { op: s.op, code: 'cancelled', at: Date.now() };
    noteOutcome(s.serial, 'cancelled', true);
  }
}

const BUSY = "Keyra is waiting for another request; long-press its button to cancel it";

function arm(req) {
  if (!takeSlot(requester, false)) fail(409, 'busy', BUSY);
  req.usbSession = req.target.kind === 'usb' && host.usb ? usbSession : 0;
  req.serial = ++serialSeq;
  machine.slot = { kind: 'type', req, deadline: Date.now() + EXPIRY_MS, owner: requester };
  syncDemand();
  autoPress(machine.slot);
  const { serial: _s, usbSession: _u, ...view } = req;
  return { ...view, target: targetText(req.target), expiresIn: EXPIRY_MS };
}

/** Session ops are bound to their session (409 busy otherwise); tryOnly (no session) belongs to nobody. */
function awaitPresence(op, commit, { tryOnly = false } = {}) {
  const owner = tryOnly ? '' : requester;
  if (!takeSlot(owner, true)) {
    if (tryOnly) return null;
    fail(409, 'busy', BUSY);
  }
  // Like Machine::cancelPresence: only the requester gets the token that withdraws it.
  machine.slot = { kind: 'presence', op, commit, deadline: Date.now() + EXPIRY_MS, cancel: randomBytes(16).toString('hex'), owner, serial: ++serialSeq };
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
  if (s.kind === 'type' || !['setup', 'factory_reset', 'trust_browser'].includes(s.op)) {
    endCancelled(s);
    machine.slot = null;
  }
}

// ---------- activity log (SPEC §15; firmware keyra_api/src/activity*.cpp) ----------

const ACTIVITY_MAX = 200;
/** Appends while unlocked (the firmware cannot write the encrypted log when locked). */
function logEvent(kind, { id = 0, title = '', detail = 0, n = 0, host } = {}) {
  if (!unlocked || !vault) return;
  vault.activity ??= [];
  // host: SPEC §9.4, the page an extension typed another site's login into.
  vault.activity.push({ kind, at: timeValid ? nowSec() : 0, id, n, detail, title: [...title].join('').slice(0, 64), ...(host ? { host } : {}) });
  if (vault.activity.length > ACTIVITY_MAX) vault.activity.splice(0, vault.activity.length - ACTIVITY_MAX);
}

const LOCK_WHY = { manual: 0, idle: 1, usb: 2, ble: 3, button: 4 };

function lockAll(why = 'manual') {
  logEvent('lock', { detail: LOCK_WHY[why] });
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
      machine.runningSerial = s.serial;
      setTimeout(() => {
        let ok = false;
        try {
          ok = s.commit() !== false;
        } catch (e) {
          console.error(`[mock] ${s.op} commit failed:`, e.message);
        }
        machine.opResult = { op: s.op, code: ok ? 'done' : 'failed', at: Date.now() };
        noteOutcome(s.serial, ok ? 'done' : 'failed', true);
        machine.running = null;
        machine.runningSerial = 0;
      }, 300);
      return `approved ${s.op}`;
    }
    const t = s?.kind === 'type' ? s.req.target : null;
    if (t?.kind === 'ble' && ble.connected !== t.addr) return 'connecting (blink)'; // stays armed
    if (s?.kind === 'type' && !machine.typing) {
      machine.slot = null;
      machine.typing = true;
      machine.typingSerial = s.req.serial;
      ble.wanted = null; // the job owns the link now
      const text = typedText(s.req);
      setTimeout(() => {
        machine.typing = false;
        machine.typingSerial = 0;
        let code = 'typed';
        if (t.kind === 'none') code = 'no_host';
        else if (t.kind === 'usb' && !host.usb) code = 'no_usb';
        else if (t.kind === 'ble' && ble.connected !== t.addr) code = 'no_host';
        else if (text === null) code = 'failed';
        else if (s.req.what !== 'probe' && !typeable(text, layoutFor(t), { tabEnter: true })) code = 'unsupported_char';
        // Machine::typingFinished: a sequence part typed fine with more to come is armed again
        // for its next part (fresh 60 s), unless something else was armed meanwhile.
        const more = code === 'typed' && s.req.what === 'sequence' && s.req.part + 1 < s.req.seq.parts;
        if (more && !machine.slot) {
          machine.slot = { kind: 'type', req: { ...s.req, part: s.req.part + 1 }, deadline: Date.now() + EXPIRY_MS, owner: s.owner };
          syncDemand();
          autoPress(machine.slot);
        } else {
          if (more) code = 'cancelled';
          machine.last = { ok: code === 'typed', code, at: Date.now(), title: s.req.title, what: s.req.what };
          noteOutcome(s.req.serial, code, false);
        }
        if (t.kind === 'ble') releaseBle(LINGER_MS);
        const e = vault?.entries.get(s.req.id);
        if (code === 'typed' && e) e.lastUsed = nowSec();
        // SPEC §16: typing the password uses one of the entry's remaining uses.
        if (code === 'typed' && !more && e?.burnAfter && ['password', 'both', 'sequence'].includes(s.req.what) && --e.burnAfter === 0) {
          vault.entries.delete(e.id);
          logEvent('entry_burned', { id: e.id, title: e.title });
          console.log(`[mock] "${e.title}" deleted after its last allowed use`);
        }
        if (code === 'typed' && s.req.what !== 'test' && s.req.what !== 'probe') {
          const detail = t.kind === 'ble' ? 1 : 0;
          if (s.req.what === 'text') logEvent('text_typed', { detail });
          else logEvent('typed', { id: s.req.id, title: s.req.title ?? '', detail });
        }
      }, 250 + Math.min(1500, (text?.length ?? 0) * settings.keyDelayMs));
      // Free text: only its length, so e2e can check "twice" without the mock echoing secrets.
      if (s.req.what === 'sequence') return `typing sequence part ${s.req.part + 1}/${s.req.seq.parts} · ${s.req.title}`;
      if (s.req.what === 'probe') return `typing probe · ${text}`; // what the computer shows (no secret in it)
      return s.req.what === 'text' ? `typing text (${text.length} chars)` : `typing ${s.req.what} · ${s.req.title}`;
    }
    return 'nothing to do (blink)';
  }
  if (s) {
    endCancelled(s);
    machine.slot = null;
    return 'cancelled';
  }
  if (machine.typing || machine.running) return 'busy (cannot interrupt)';
  if (unlocked) {
    lockAll('button');
    return 'locked';
  }
  return 'nothing to do (blink)';
}

/** What the HID engine would type for a request (null = entry vanished). */
function typedText(req) {
  if (req.what === 'test') return 'Keyra test 123';
  // The keys are fixed; the simulated computer uses the layout Keyra is set to for that output.
  if (req.what === 'probe') return layoutById(req.target.kind === 'ble' ? settings.layoutBle : settings.layoutUsb).probe;
  if (req.what === 'text') return req.text + (req.twice ? (req.enterBetween ? '\n' : '\t') + req.text : '');
  const e = vault?.entries.get(req.id);
  if (!e) return null;
  const sep = settings.bothSeparator === 'enter' ? '\n' : '\t';
  if (req.what === 'sequence') {
    // seqrun::checkAll at the first press: a reason known up front stops it before any part.
    const all = Array.from({ length: req.seq.parts }, (_, p) => seqPartText(req.seq, p, e));
    if (req.part === 0 && all.some((x) => x === null)) return null;
    return (req.part === 0 && all.find((x) => !typeable(x, layoutFor(req.target), { tabEnter: true }))) || all[req.part];
  }
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
const MAX_PASSKEYS = 50;
const MAX_FIDO_KEYS = 4;

function exportBackup(pass, withPasskeys) {
  const salt = randomBytes(16);
  const iv = randomBytes(12);
  const key = pbkdf2Sync(pass, salt, BACKUP_ITER, 32, 'sha256');
  const c = createCipheriv('aes-256-gcm', key, iv);
  const body = { entries: [...vault.entries.values()] };
  // A firmware record is opaque bytes; the mock's is the passkey as JSON (with its credential).
  if (withPasskeys) {
    const records = [...vault.passkeys.values()].map((p) => Buffer.from(JSON.stringify(p)).toString('base64'));
    body.passkeys = { keys: vault.fidoKeys, records, counter: vault.fidoCounter };
  }
  const plain = Buffer.from(JSON.stringify(body));
  const data = Buffer.concat([c.update(plain), c.final(), c.getAuthTag()]);
  return JSON.stringify({
    format: 'keyra-backup',
    v: 3,
    kdf: { alg: 'pbkdf2-sha256', iter: BACKUP_ITER, salt: salt.toString('base64') },
    iv: iv.toString('base64'),
    data: data.toString('base64'),
  });
}

/** v3 "passkeys" section → { keys, records, counter }, or 'invalid'. */
function readPasskeys(pk) {
  if (!pk || typeof pk !== 'object' || Array.isArray(pk)) return 'invalid';
  const { keys, records, counter } = pk;
  if (!Array.isArray(keys) || keys.length > MAX_FIDO_KEYS || !Array.isArray(records) || records.length > MAX_PASSKEYS) return 'invalid';
  if (!Number.isInteger(counter) || counter < 0 || counter > 0xffffffff) return 'invalid';
  if (keys.some((k) => typeof k !== 'string' || Buffer.from(k, 'base64').length !== 32)) return 'invalid';
  const out = [];
  for (const r of records) {
    let p;
    try {
      p = JSON.parse(Buffer.from(String(r), 'base64').toString('utf8'));
    } catch {
      return 'invalid';
    }
    if (!p || typeof p.cred !== 'string' || !p.cred || typeof p.rpId !== 'string' || !Number.isInteger(p.id) || p.id <= 0 || p.id > 0xffffffff) return 'invalid';
    out.push({ id: p.id, rpId: p.rpId, userName: String(p.userName ?? ''), displayName: String(p.displayName ?? ''), created: Number.isInteger(p.created) ? p.created : 0, cred: p.cred });
  }
  return { keys: [...keys], records: out, counter };
}

/** → { entries, passkeys (null when the file has none) }, or 'invalid' / 'wrong'. */
function openBackup(pass, b) {
  if (b?.format !== 'keyra-backup' || ![1, 2, 3].includes(b.v) || b.kdf?.alg !== 'pbkdf2-sha256' || !Number.isInteger(b.kdf.iter)) return 'invalid';
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
  let parsed;
  try {
    parsed = JSON.parse(plain.toString('utf8'));
  } catch {
    return 'invalid';
  }
  // v1/v2: a bare array of entries; v3: { entries, passkeys? }.
  const v3 = b.v === 3;
  if (v3 ? !parsed || typeof parsed !== 'object' || Array.isArray(parsed) : !Array.isArray(parsed)) return 'invalid';
  const list = v3 ? parsed.entries : parsed;
  if (!Array.isArray(list)) return 'invalid';
  let passkeys = null;
  if (v3 && parsed.passkeys !== undefined) {
    passkeys = readPasskeys(parsed.passkeys);
    if (passkeys === 'invalid') return 'invalid';
  }
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
  return { entries: out, passkeys };
}

/**
 * The passkeys a restore leaves (docs/research/PASSKEY-BACKUP.md): merge adds the
 * backup's wrap keys and the records whose credential isn't here; replace takes the
 * backup's section whole, and a backup without one leaves local passkeys alone.
 * Changes nothing → { keys, records, counter, added } for commitPasskeys, or 'passkeys_full'.
 */
function restorePasskeys(pk, replace) {
  if (!pk) return { keys: vault.fidoKeys, records: vault.passkeys, counter: vault.fidoCounter, added: 0 };
  // Sites that check the signature counter must never see it go backwards.
  const counter = Math.max(vault.fidoCounter, pk.counter + 1000);
  if (replace) return { keys: pk.keys, records: new Map(pk.records.map((p) => [p.id, p])), counter, added: pk.records.length };
  const keys = [...vault.fidoKeys, ...pk.keys.filter((k) => !vault.fidoKeys.includes(k))];
  const records = new Map(vault.passkeys);
  const creds = new Set([...records.values()].map((p) => p.cred));
  let added = 0;
  for (const p of pk.records) {
    if (creds.has(p.cred)) continue;
    creds.add(p.cred);
    const id = records.has(p.id) ? freshId(records) : p.id;
    records.set(id, { ...p, id });
    added++;
  }
  if (keys.length > MAX_FIDO_KEYS || records.size > MAX_PASSKEYS) return 'passkeys_full';
  return { keys, records, counter, added };
}

function commitPasskeys(r) {
  vault.fidoKeys = r.keys;
  vault.passkeys = r.records;
  vault.fidoCounter = r.counter;
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

// ---------- keyboard layouts (SPEC §10.1-10.3; firmware keyra_hid keymap.cpp + layouts/gen_layouts.py) ----------
// Read from the firmware's own source table, so ids, names, probe strings and
// layout-proof characters are exactly the device's.

const LAYOUTS_TXT = fileURLToPath(new URL('../../firmware/components/keyra_hid/layouts/layouts.txt', import.meta.url));
const MAX_LAYOUTS_SAFE = 8;
const LAYOUT_ERR = 'layoutUsb/layoutBle must be a layout id from GET /api/keyboard';
const LAYOUTS_ERR = 'layouts must be 1-8 layout ids';
const SHIFT = 0x02;
const MAC_SWAP_KEYS = new Set([0x35, 0x64]);
// typeProbe(): Q W Y Z Space ; Shift+2 Shift+3 / (US positions).
const PROBE_KEYS = [[0x14, 0], [0x1a, 0], [0x1c, 0], [0x1d, 0], [0x2c, 0], [0x33, 0], [0x1f, 1], [0x20, 1], [0x38, 0]];

function parseLayouts(path) {
  const out = [];
  for (const raw of readFileSync(path, 'utf8').split('\n')) {
    const line = raw.split('  #')[0].trim();
    if (!line || line.startsWith('#')) continue;
    const parts = line.split(/\s+/);
    if (parts[0] === 'layout') {
      const platform = { any: 'any', win: 'windows', mac: 'mac' }[parts[2]];
      out.push({ id: parts[1], platform, name: parts.slice(3).join(' '), keys: [] });
      continue;
    }
    const cells = parts.slice(1, 5).map((tok) => (tok === '-' ? null : { cp: parseInt(tok.replace('*', '').slice(2), 16), dead: tok.endsWith('*') }));
    out.at(-1).keys.push({ usage: parseInt(parts[0], 16), cells });
  }
  for (const l of out) {
    // gen_layouts.py glyphs(): the key press Keyra uses for each character.
    l.glyphs = new Map();
    for (const { usage, cells } of l.keys) {
      cells.forEach((c, layer) => {
        if (!c) return;
        const rank = [c.dead, l.platform === 'mac' && MAC_SWAP_KEYS.has(usage), layer, usage].map(Number);
        const best = l.glyphs.get(c.cp);
        const i = best ? rank.findIndex((v, j) => v !== best.rank[j]) : -1;
        if (!best || (i >= 0 && rank[i] < best.rank[i])) l.glyphs.set(c.cp, { rank, usage, layer, dead: c.dead });
      });
    }
    // charFor(): what the key press leaves on the host (nothing for a dead key).
    const charFor = (usage, layer) => {
      const c = l.keys.find((k) => k.usage === usage)?.cells[layer];
      return c && !c.dead ? String.fromCodePoint(c.cp) : '';
    };
    l.probe = PROBE_KEYS.map(([usage, shift]) => charFor(usage, shift)).join('');
  }
  return out;
}

const LAYOUTS = parseLayouts(LAYOUTS_TXT);
const layoutById = (id) => LAYOUTS.find((l) => l.id === id);

/** keymap.cpp typeable(): every character has a key press (or dead key + Space) on `layout`; no control characters. */
function typeable(text, layout, { tabEnter = false } = {}) {
  for (const ch of text) {
    if (tabEnter && (ch === '\t' || ch === '\n')) continue; // typed as keys, not looked up
    const cp = ch.codePointAt(0);
    if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0) || !layout.glyphs.has(cp)) return false;
  }
  return true;
}
/** keymap.cpp layoutChars(): every character typeable() accepts, in code point order. */
const layoutChars = (layout) =>
  [...layout.glyphs.keys()]
    .filter((cp) => typeable(String.fromCodePoint(cp), layout))
    .sort((a, b) => a - b)
    .map((cp) => String.fromCodePoint(cp))
    .join('');
/** kbdapi::layoutFor(): the layout set for the output a target types into. */
const layoutFor = (target) => layoutById(target.kind === 'ble' ? settings.layoutBle : settings.layoutUsb);

/** keymap.cpp sameOnAll(): `ch` comes from the very same single key press on every layout. */
function sameOnAll(ch, layouts) {
  const strokes = layouts.map((l) => {
    const g = l.glyphs.get(ch.codePointAt(0));
    if (!g || g.dead) return null;
    const alt = l.platform === 'mac' ? 0x04 : 0x40; // Option (left Alt) on a Mac, AltGr elsewhere
    return `${g.usage}:${(g.layer & 1 ? SHIFT : 0) | (g.layer & 2 ? alt : 0)}`;
  });
  return strokes[0] !== null && strokes.every((s) => s === strokes[0]);
}

/** handlers_kbd.cpp layoutSafeChars(): → allowed characters, null (not restricted) or an error message. */
function layoutSafeChars(b) {
  if (b.layoutSafe !== undefined && typeof b.layoutSafe !== 'boolean') return { error: 'layoutSafe must be a boolean' };
  if (!b.layoutSafe) return b.layouts !== undefined ? { error: 'layouts needs layoutSafe' } : { allowed: null };
  let chosen;
  if (b.layouts === undefined) {
    chosen = [layoutById(settings.layoutUsb)];
    if (settings.layoutBle !== settings.layoutUsb) chosen.push(layoutById(settings.layoutBle));
  } else {
    if (!Array.isArray(b.layouts) || b.layouts.length < 1 || b.layouts.length > MAX_LAYOUTS_SAFE) return { error: LAYOUTS_ERR };
    chosen = b.layouts.map((id) => (typeof id === 'string' ? layoutById(id) : undefined));
    if (chosen.some((l) => !l)) return { error: LAYOUTS_ERR };
  }
  let allowed = '';
  for (let c = 0x21; c < 0x7f; c++) if (sameOnAll(String.fromCharCode(c), chosen)) allowed += String.fromCharCode(c);
  return { allowed };
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
  const safe = layoutSafeChars(b);
  if (safe.error) return safe.error;
  if (L < 8 || L > 128) return 'length must be 8-128';
  if (!b.lower && !b.upper && !b.digits && !b.symbols) return 'enable at least one of lower, upper, digits, symbols';
  const minD = b.minDigits ?? 0;
  const minS = b.minSymbols ?? 0;
  const minErr = 'minDigits/minSymbols need their class enabled and must fit in the length';
  if (minD > L || minS > L || (!b.digits && minD > 0) || (!b.symbols && minS > 0)) return minErr;
  const strip = (s) => [...s].filter((c) => !(b.avoidAmbiguous && GEN_AMBIGUOUS.includes(c)) && (safe.allowed === null || safe.allowed.includes(c))).join('');
  const classes = [
    [b.lower, 'abcdefghijklmnopqrstuvwxyz', 1],
    [b.upper, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 1],
    [b.digits, '0123456789', Math.max(1, minD)],
    [b.symbols, symbolSet, Math.max(1, minS)],
  ]
    .filter(([on]) => on)
    .map(([, chars, min]) => ({ chars: strip(chars), min }));
  if (classes.some((c) => !c.chars)) return 'avoidAmbiguous or layoutSafe leaves an enabled class without characters';
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
  hasSequence: !!e.sequence,
  burnAfter: e.burnAfter ?? 0,
  updated: e.updated,
  lastUsed: e.lastUsed,
});

// ---------- auto-type sequences (SPEC §10.4; keyra_vault sequence.cpp, keyra_api sequence_run.cpp) ----------

const SEQ_TOKENS = { USERNAME: ['username', 256], PASSWORD: ['password', 256], TOTP: ['totp', 10], TAB: ['tab', 1], ENTER: ['enter', 1], SPACE: ['space', 1], PRESS: ['press', 0] };
const SEQ_MSG = {
  empty: 'sequence must type something',
  tooLong: 'sequence must be at most 256 bytes',
  badText: 'sequence must be printable UTF-8 (no control characters)',
  unclosed: 'sequence has a { without a matching }',
  stray: 'a literal } must be written {}}',
  unknown: 'unknown token: use {USERNAME} {PASSWORD} {TOTP} {TAB} {ENTER} {SPACE} {DELAY ms} {PRESS} {{} {}}',
  delay: '{DELAY n}: n must be 100-3000 (milliseconds)',
  steps: 'sequence has more than 32 steps',
  presses: 'sequence has more than 4 {PRESS}',
  placement: '{PRESS} needs something to type before and after it',
  delayTotal: '{DELAY} adds up to more than 10 seconds',
  typing: 'sequence could type more than 1024 characters',
};

/** Like seq::parse: { steps, parts, preview } or { error } (the firmware's English 400 message). */
function seqParse(src) {
  const no = (k) => ({ error: SEQ_MSG[k] });
  if (src === '') return no('empty');
  if (Buffer.byteLength(src) > 256) return no('tooLong');
  // Control characters, DEL, and lone surrogates (not valid UTF-8).
  if (/[\x00-\x1f\x7f]|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/.test(src)) return no('badText');
  const steps = [];
  const addText = (t) => (steps.at(-1)?.kind === 'text' ? (steps.at(-1).text += t) : steps.push({ kind: 'text', text: t }));
  let presses = 0;
  let typed = 0;
  let delay = 0;
  for (let i = 0; i < src.length; ) {
    if (src[i] === '}') return no('stray');
    if (src[i] !== '{') {
      const m = /^[^{}]+/.exec(src.slice(i))[0];
      addText(m);
      typed += [...m].length;
      i += m.length;
      continue;
    }
    const three = src.slice(i, i + 3);
    if (three === '{{}' || three === '{}}') {
      addText(three[1]);
      typed++;
      i += 3;
      continue;
    }
    const close = src.indexOf('}', i + 1);
    if (close < 0) return no('unclosed');
    const body = src.slice(i + 1, close);
    i = close + 1;
    if (body.startsWith('DELAY ')) {
      const num = body.slice(6);
      if (!/^[1-9][0-9]{0,3}$/.test(num) || +num < 100 || +num > 3000) return no('delay');
      delay += +num;
      steps.push({ kind: 'delay', ms: +num });
      continue;
    }
    const tok = Object.hasOwn(SEQ_TOKENS, body) ? SEQ_TOKENS[body] : null;
    if (!tok) return no('unknown');
    if (tok[0] === 'press') presses++;
    typed += tok[1];
    steps.push({ kind: tok[0] });
  }
  if (steps.length > 32) return no('steps');
  if (presses > 4) return no('presses');
  if (delay > 10000) return no('delayTotal');
  if (typed > 1024) return no('typing');
  let typedInPart = false;
  for (const st of steps) {
    if (st.kind === 'press') {
      if (!typedInPart) return no('placement');
      typedInPart = false;
    } else if (st.kind !== 'delay') typedInPart = true;
  }
  if (!typedInPart) return no(presses ? 'placement' : 'empty');
  const preview = steps.map((st) => (st.kind === 'text' ? '•'.repeat([...st.text].length) : st.kind === 'delay' ? `{DELAY ${st.ms}}` : `{${st.kind.toUpperCase()}}`)).join('');
  return { steps, parts: presses + 1, preview };
}

/** seqrun::builtIn: what "Both" means without a custom sequence. */
const seqBuiltIn = () => `{USERNAME}${settings.bothSeparator === 'enter' ? '{ENTER}' : '{TAB}'}{PASSWORD}${settings.submitAfterBoth ? '{ENTER}' : ''}`;

/** The text one part types (null: a field it names is empty, like checkPart's Failed). */
function seqPartText(seq, part, e) {
  let at = 0;
  let out = '';
  const code = seq.steps.some((st) => st.kind === 'totp') ? (totpCode(e.totp)?.code ?? '') : '';
  for (const st of seq.steps) {
    if (st.kind === 'press') {
      if (++at > part) break;
      continue;
    }
    if (at !== part) continue;
    const field = { text: st.text, username: e.username, password: e.password, totp: code }[st.kind];
    if (field !== undefined && !field) return null;
    out += field ?? { tab: '\t', enter: '\n', space: ' ', delay: '' }[st.kind];
  }
  return out;
}

/** The pending card's view of a type request (handlers_kbd addPending: part is 1-based). */
function pendingView(req) {
  const v = { id: req.id, title: req.title, what: req.what, submit: req.submit };
  if (req.host) v.host = req.host; // SPEC §9.4: the page an extension token armed it for
  if (req.what === 'sequence') Object.assign(v, { preview: req.seq.preview, part: req.part + 1, parts: req.seq.parts });
  return v;
}

/** Copies present fields onto `base`; returns an error message on a wrong type. */
function readEntry(src, withTimestamps, base = { title: '', url: '', username: '', password: '', totp: '', notes: '', sequence: '', favorite: false, created: 0, updated: 0, lastUsed: 0, history: [] }) {
  const e = { ...base };
  for (const k of [...STR_FIELDS, 'sequence']) {
    if (src[k] === undefined) continue;
    if (typeof src[k] !== 'string') return `"${k}" must be a string`;
    e[k] = src[k];
  }
  if (src.favorite !== undefined) {
    if (typeof src.favorite !== 'boolean') return '"favorite" must be a boolean';
    e.favorite = src.favorite;
  }
  if (src.burnAfter !== undefined) {
    if (!Number.isInteger(src.burnAfter) || src.burnAfter < 0 || src.burnAfter > 99) return '"burnAfter" must be 0-99';
    e.burnAfter = src.burnAfter;
  }
  if (e.sequence) {
    const r = seqParse(e.sequence);
    if (r.error) return `sequence: ${r.error}`;
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
  const out = { checked: withPw.length, clock: !!now, weak: weak.sort(byId), reused, old: old.sort(byId) };
  if (settings.rotateSince > 0) {
    const setAt = (e) => (e.history.length ? e.history[0].changedAt : e.created);
    out.rotate = { since: settings.rotateSince, pending: withPw.filter((e) => setAt(e) < settings.rotateSince).map((e) => e.id) };
  }
  return out;
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
// mock lists, deletes, backs up and restores them.
// A few days of history so Settings → Activity is not empty (oldest first, like the firmware).
function seedActivity() {
  const now = nowSec();
  const h = 3600;
  return [
    { kind: 'unlock', at: now - 50 * h, id: 0, n: 0, detail: 0, title: '' },
    { kind: 'typed', at: now - 50 * h + 60, id: 0, n: 0, detail: 0, title: 'GitHub' },
    { kind: 'lock', at: now - 49 * h, id: 0, n: 0, detail: 1, title: '' },
    { kind: 'failed_unlocks', at: now - 26 * h, id: 0, n: 2, detail: 0, title: '' },
    { kind: 'unlock', at: now - 26 * h, id: 0, n: 0, detail: 0, title: '' },
    { kind: 'typed', at: now - 26 * h + 30, id: 0, n: 0, detail: 1, title: 'Instagram' },
    { kind: 'backup', at: now - 25 * h, id: 0, n: 0, detail: 0, title: '' },
    { kind: 'lock', at: now - 25 * h + 600, id: 0, n: 0, detail: 2, title: '' },
  ];
}

function seedPasskeys() {
  const now = nowSec();
  const day = 86400;
  const list = [
    { id: 3141592653, rpId: 'github.com', userName: 'hasan-dev', displayName: 'Hasan', created: now - 40 * day },
    { id: 2718281828, rpId: 'accounts.google.com', userName: 'hasan@gmail.com', displayName: 'Hasan Ali', created: now - 12 * day },
    { id: 1618033988, rpId: 'www.amazon.com', userName: 'hasan@example.com', displayName: '', created: now - 2 * day },
  ];
  // `cred` stands in for the credential ID (restore matches on it); never listed.
  return new Map(list.map((p) => [p.id, { ...p, cred: randomBytes(16).toString('base64') }]));
}

if (!FRESH) {
  vault = { passphrase: DEMO_PASSPHRASE, entries: seed(), passkeys: seedPasskeys(), fidoKeys: [randomBytes(32).toString('base64')], fidoCounter: 57, activity: seedActivity() };
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

/** SPEC §9.4: bearer requests to /api/agent/* may also come from a browser extension. */
const EXT_ORIGIN = /^(chrome-extension|moz-extension|safari-web-extension):\/\/[A-Za-z0-9._-]+$/;

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
    case 'keyboard': return one('GET', 'keyboard');
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
    case 'health/rotate': return one('POST', 'healthRotate');
    case 'activity': return one('GET', 'activity');
    case 'ble': return one('GET', 'ble');
    case 'ble/pair': return one('POST', 'blePair');
    case 'tokens':
      return method === 'GET' ? { route: 'tokens' } : method === 'POST' ? { route: 'createToken' } : { notAllowed: true };
    case 'agent/entries': return one('GET', 'agentEntries');
    case 'agent/type': return one('POST', 'agentType');
    case 'agent/status': return one('GET', 'agentStatus');
    case 'agent/cancel': return one('POST', 'agentCancel');
    case 'agent/save': return one('POST', 'agentSave');
    case 'agent/generate': return one('POST', 'agentGenerate');
    case 'agent/match': return one('POST', 'agentMatch');
    case 'tags':
      return method === 'GET' ? { route: 'tags' } : method === 'POST' ? { route: 'createTag' } : { notAllowed: true };
    case 'tag/tap': return one('POST', 'tagTap');
    case 'tag/status': return one('POST', 'tagStatus');
  }
  const tg = /^tags\/([0-9]{1,10})$/.exec(p);
  if (tg) return Number(tg[1]) > 0 && Number(tg[1]) <= 0xffffffff ? (method === 'DELETE' ? { route: 'deleteTag', id: Number(tg[1]) } : { notAllowed: true }) : null;
  const tk = /^tokens\/([0-9]{1,10})$/.exec(p);
  if (tk) return Number(tk[1]) > 0 && Number(tk[1]) <= 0xffffffff ? (method === 'DELETE' ? { route: 'deleteToken', id: Number(tk[1]) } : { notAllowed: true }) : null;
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

const OPEN = new Set(['state', 'setup', 'unlock', 'unlockRecovery', 'factoryReset', 'presenceCancel', 'tagTap', 'tagStatus']);
const AGENT = new Set(['agentEntries', 'agentType', 'agentStatus', 'agentCancel', 'agentSave', 'agentGenerate', 'agentMatch']);
const BODY = new Set(['createToken', 'createTag', 'tagTap', 'tagStatus', 'agentType', 'agentSave', 'agentGenerate', 'agentMatch', 'healthRotate', 'setup', 'unlock', 'unlockRecovery', 'create', 'update', 'import', 'type', 'generate', 'putSettings', 'passphrase', 'backup', 'restore', 'wifiHome', 'bleSetOs', 'presenceCancel']);

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
  // A custom sequence may hold literal secrets: it follows the password (SPEC §10.4).
  const { password, totp, history, sequence, ...rest } = e;
  if (revealed) return { ...rest, revealed, hasPassword: !!password, hasTotp: !!totp, hasSequence: !!sequence, password, totp, sequence: sequence ?? '', history };
  return { ...rest, revealed, hasPassword: !!password, hasTotp: !!totp, hasSequence: !!sequence, history: history.map((h) => ({ changedAt: h.changedAt })) };
}

// SPEC §12.3: a press grants what it was asked for — reveal for 60 s, or one backup / one recovery-key change.
const graceLeft = (sess, op = 'reveal') => (sess ? Math.max(0, (sess.grace?.[op] ?? 0) - Date.now()) : 0);
function consumeGrace(sess, op) {
  if (graceLeft(sess, op) <= 0) return false;
  sess.grace[op] = 0;
  return true;
}
const mayReveal = (sess) => !settings.protectReveal || graceLeft(sess) > 0;
const mayBackup = (sess) => !settings.protectReveal || consumeGrace(sess, 'backup');
/** Arms `op`; the press opens this session's grace for that op only. */
function requestPress(res, op, token) {
  const exp = awaitPresence(op, () => {
    const s = sessions.get(token);
    if (!s) return false;
    s.grace = { ...s.grace, [op]: Date.now() + GRACE_MS };
  });
  return send(res, 202, { awaiting: 'button', op, expiresIn: exp, cancel: machine.slot?.cancel ?? '' });
}

/** A new session for this browser (+ the renewed trust cookie). */
function issueSession(res, req, known, how = 0) {
  unlocked = true;
  const failedAttempts = failedBefore;
  if (failedAttempts > 0) logEvent('failed_unlocks', { n: failedAttempts });
  logEvent('unlock', { detail: how });
  usbSeen = host.usb;
  lastActivity = Date.now();
  if (sessions.size >= MAX_SESSIONS) {
    const oldest = [...sessions.entries()].sort((x, y) => x[1].lastUsed - y[1].lastUsed)[0][0];
    sessions.delete(oldest);
  }
  const tok = randomBytes(32).toString('hex');
  const csrf = randomBytes(32).toString('hex');
  sessions.set(tok, { csrf, lastUsed: Date.now(), trustId: known?.id ?? 0, grace: {} });
  const cookies = [`ks=${tok}; HttpOnly; SameSite=Strict; Path=/`];
  if (known) cookies.push(`kt=${cookie(req, 'kt')}; HttpOnly; SameSite=Strict; Path=/; Max-Age=31536000`);
  return send(res, 200, { csrf, failedAttempts }, { 'Set-Cookie': cookies });
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

// ---------- access tokens (SPEC §17; firmware keyra_api/src/tokens.cpp, handlers_agent.cpp) ----------

const MAX_TOKENS = 8;
const MAX_SCOPE = 32;
const RATE_MAX = 10;
const RATE_WINDOW_MS = 10000;
const TOKEN_RE = /^keyra_[a-z2-7]{32}$/;
const TOKEN_KINDS = ['agent', 'app', 'extension']; // index = kind byte in tokens.bin and token_created detail
const tokenRate = new Map(); // token id (0 = unrecognised) → request times
const tokenLast = new Map(); // token id → { serial, save, id, title?, what? }

function base32Encode(buf) {
  const A = 'abcdefghijklmnopqrstuvwxyz234567';
  let bits = 0;
  let v = 0;
  let out = '';
  for (const byte of buf) {
    v = (v << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      out += A[(v >>> (bits - 5)) & 31];
      bits -= 5;
    }
  }
  return out;
}

const tokenView = (t) => ({ id: t.id, name: t.name, kind: t.kind, scope: t.scope, created: t.created, lastUsed: t.lastUsed });
const inScope = (t, id) => t.scope === 'all' || t.scope.includes(id);
const byOf = (owner) => {
  const [, kind, id] = /^(token|tag):(\d+)$/.exec(owner ?? '') ?? [];
  if (!id) return {};
  return { by: vault?.[kind === 'tag' ? 'tags' : 'tokens']?.find((t) => t.id === Number(id))?.name ?? '?' };
};

/** tokens.cpp urlHost: the host name only. */
function urlHost(url = '') {
  let u = url.replace(/^[^:/?#]*:\/\//, '');
  u = u.split(/[/?#]/)[0];
  u = u.slice(u.lastIndexOf('@') + 1);
  u = u.startsWith('[') ? (u.includes(']') ? u.slice(1, u.indexOf(']')) : '') : u.split(':')[0];
  return /[\x00-\x20\x7f]/.test(u) ? '' : u.toLowerCase();
}

// SPEC §9.4 host rule (docs/research/HOST-MATCH.md; extension/src/host.ts runs the same table).
const COUNTRY_SLD = new Set(['ac', 'co', 'com', 'edu', 'gob', 'gov', 'go', 'mil', 'ne', 'net', 'or', 'org', 'sch']);
const normHost = (h) => String(h ?? '').trim().toLowerCase().replace(/\.$/, '').replace(/^www\./, '');
const isIp = (h) => h.includes(':') || /^\d{1,3}(\.\d{1,3}){3}$/.test(h);
function siteLike(h) {
  const l = h.split('.');
  return l.length >= 2 && l.every(Boolean) && !(l.length === 2 && l[1].length === 2 && COUNTRY_SLD.has(l[0]));
}
function hostMatch(login, page) {
  const a = normHost(login);
  const b = normHost(page);
  if (!a || !b) return false;
  if (a === b) return true;
  if (isIp(a) || isIp(b)) return false;
  const [short, long] = a.length <= b.length ? [a, b] : [b, a];
  return long.endsWith('.' + short) && siteLike(short);
}

/** RateLimit::allow: 10 per sliding 10 s per key. Returns the wait in ms, 0 = allowed. */
function rateWait(key, rate = tokenRate) {
  const now = Date.now();
  const times = (rate.get(key) ?? []).filter((t) => now - t < RATE_WINDOW_MS);
  if (times.length >= RATE_MAX) {
    rate.set(key, times);
    return times[0] + RATE_WINDOW_MS - now;
  }
  times.push(now);
  rate.set(key, times);
  return 0;
}

function rateFail(wait, message = 'Too many requests for this token') {
  fail(429, 'rate_limited', message, { retryAfterMs: wait }, { 'Retry-After': String(Math.ceil(wait / 1000)) });
}

/** handlers_agent.cpp authenticate: vault unlocked, a known token, within its rate. */
function authenticateToken(req) {
  if (!unlocked || !vault) fail(401, 'locked', 'Vault is locked');
  const h = req.headers.authorization ?? '';
  const presented = /^bearer +(.*)$/i.exec(h)?.[1]?.trim() ?? '';
  const hash = TOKEN_RE.test(presented) ? sha(presented) : '';
  const t = hash ? (vault.tokens ?? []).find((x) => safeEqual(x.hash, hash)) : undefined;
  if (!t) {
    const wait = rateWait(0);
    if (wait) rateFail(wait);
    fail(401, 'invalid_token', 'Unknown or revoked access token', undefined, { 'WWW-Authenticate': 'Bearer' });
  }
  const wait = rateWait(t.id);
  if (wait) rateFail(wait);
  if (timeValid && nowSec() - t.lastUsed >= 60) t.lastUsed = nowSec();
  return t;
}

/** activity::append(…, coalesce): a repeat of the newest event only counts. */
function logCoalesced(kind, { id, title, detail = 0 }) {
  const last = vault?.activity?.at(-1);
  if (unlocked && last && last.kind === kind && last.id === id && last.title === title) {
    last.n += 1;
    if (timeValid) last.at = nowSec();
    return;
  }
  logEvent(kind, { id, title, n: 1, detail });
}

const TYPE_STATE = { typed: 'typed', cancelled: 'cancelled', expired: 'expired' };
const SAVE_STATE = { done: 'saved', cancelled: 'cancelled', expired: 'expired', failed: 'failed' };

/** handlers_agent.cpp getStatus, by the serial of this token's last request. */
function agentStatus(t) {
  expire();
  const last = tokenLast.get(t.id);
  if (!last) return { state: 'none' };
  const out = { request: last.save ? 'save' : 'type' };
  const s = machine.slot;
  const o = outcomes.get(last.serial);
  if (s && (s.kind === 'type' ? s.req.serial : s.serial) === last.serial) {
    const connecting = s.kind === 'type' && s.req.target.kind === 'ble' && ble.connected !== s.req.target.addr;
    Object.assign(out, { state: connecting ? 'waiting' : 'armed', expiresIn: s.deadline - Date.now() });
  } else if ((machine.typing && machine.typingSerial === last.serial) || (machine.running && machine.runningSerial === last.serial)) {
    out.state = 'waiting';
  } else if (o) {
    if (last.save) out.state = SAVE_STATE[o.code] ?? 'failed';
    else Object.assign(out, { state: TYPE_STATE[o.code] ?? 'failed', code: o.code });
  } else out.state = 'none';
  if (last.id) out.id = last.id;
  if (!last.save) Object.assign(out, { title: last.title, what: last.what });
  return out;
}

const WHATS = ['username', 'password', 'both', 'totp'];

/** typereq::readTarget + entryRequest + arm (handlers.cpp): what POST /api/agent/type and a tag tap share. */
function armEntry(id, what, targetIn, submitIn, host) {
  let target = pickTarget();
  if (targetIn !== undefined) {
    if (targetIn === 'usb') target = { kind: 'usb' };
    else if (typeof targetIn === 'string' && /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/.test(targetIn)) {
      if (!settings.bleEnabled) fail(409, 'ble_disabled', 'Bluetooth is turned off');
      const addr = targetIn.toUpperCase();
      if (!ble.bonds.some((x) => x.addr === addr)) fail(404, 'not_found', 'No such device');
      target = { kind: 'ble', addr };
    } else bad('"target" must be "usb" or a device address');
  }
  const e = getEntry(id);
  const missing =
    (what === 'username' && !e.username) ||
    (what === 'password' && !e.password) ||
    (what === 'both' && (!e.username || !e.password)) ||
    (what === 'totp' && !e.totp);
  if (missing) bad('Entry has no value for that field');
  if (what === 'totp' && !timeValid) fail(409, 'no_time', 'Device clock is not set');
  const submit = submitIn ?? (what === 'both' && settings.submitAfterBoth);
  return { e, p: arm({ id: e.id, title: e.title, what, submit, target, ...(host ? { host } : {}) }) };
}

// ---------- NFC tap tags (SPEC §18; firmware keyra_api/src/tags.cpp, handlers_tags.cpp) ----------

const MAX_TAGS = 16;
const tagRate = new Map(); // tag id (0 = unknown tag, wrong secret or MAC) → tap times
const tagLast = new Map(); // tag id → { serial, ticket }
const simTags = new Map(); // tag id → the simulated NTAG 424 DNA: { uid, ctr }
const TAG_HOST = 'keyra.local';

const tagView = (t) => ({
  id: t.id, name: t.name, kind: t.kind, entry: t.entry, what: t.what, target: t.target || null, created: t.created, lastUsed: t.lastUsed,
  ...(t.kind === 'secure' ? { bound: t.bound, counter: t.counter } : {}),
});

function aesEcb(key, block, decrypt = false) {
  const c = (decrypt ? createDecipheriv : createCipheriv)('aes-128-ecb', key, null);
  c.setAutoPadding(false);
  return Buffer.concat([c.update(block), c.final()]);
}

/** AES-CMAC (RFC 4493), like tags.cpp cmac(). */
function cmac(key, msg) {
  const shift = (b) => {
    const o = Buffer.alloc(16);
    for (let i = 0; i < 16; i++) o[i] = ((b[i] << 1) | (i < 15 ? b[i + 1] >> 7 : 0)) & 0xff;
    if (b[0] & 0x80) o[15] ^= 0x87;
    return o;
  };
  const k1 = shift(aesEcb(key, Buffer.alloc(16)));
  const k2 = shift(k1);
  const n = Math.max(1, Math.ceil(msg.length / 16));
  const whole = msg.length > 0 && msg.length % 16 === 0;
  const last = Buffer.alloc(16);
  msg.copy(last, 0, (n - 1) * 16);
  if (!whole) last[msg.length - (n - 1) * 16] = 0x80;
  let x = Buffer.alloc(16);
  for (let b = 0; b < n - 1; b++) {
    for (let i = 0; i < 16; i++) x[i] ^= msg[16 * b + i];
    x = aesEcb(key, x);
  }
  for (let i = 0; i < 16; i++) x[i] ^= last[i] ^ (whole ? k1 : k2)[i];
  return aesEcb(key, x);
}

/** SDMMAC (AN12196): session key = CMAC(file key, SV2), MAC over empty input, odd bytes. */
function sunMac(fileKey, uidCtr) {
  const session = cmac(fileKey, Buffer.concat([Buffer.from('3cc300010080', 'hex'), uidCtr]));
  const full = cmac(session, Buffer.alloc(0));
  return Buffer.from([1, 3, 5, 7, 9, 11, 13, 15].map((i) => full[i]));
}

/** tags.cpp verifySun → 'ok' | 'bad' | 'chip' | 'replay', with the tap's uid and counter. */
function verifySun(t, pHex, mHex) {
  const plain = aesEcb(Buffer.from(t.metaKey, 'hex'), Buffer.from(pHex, 'hex'), true);
  const uid = plain.subarray(1, 8).toString('hex');
  const ctr = plain[8] | (plain[9] << 8) | (plain[10] << 16);
  const mac = sunMac(Buffer.from(t.fileKey, 'hex'), plain.subarray(1, 11));
  if (plain[0] !== 0xc7 || !timingSafeEqual(mac, Buffer.from(mHex, 'hex'))) return { v: 'bad' };
  if (t.bound && t.uid !== uid) return { v: 'chip' };
  if (t.bound && ctr <= t.counter) return { v: 'replay' };
  return { v: 'ok', uid, ctr };
}

/** The URL a secure tag would open on its next read (or at counter `ctr`), for e2e. */
function simulateSun(t, ctr) {
  let sim = simTags.get(t.id);
  if (!sim) simTags.set(t.id, (sim = { uid: Buffer.concat([Buffer.from([0x04]), randomBytes(6)]), ctr: 0 }));
  const c = Number.isInteger(ctr) ? ctr : ++sim.ctr;
  const plain = Buffer.concat([Buffer.from([0xc7]), sim.uid, Buffer.from([c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff]), randomBytes(5)]);
  const p = aesEcb(Buffer.from(t.metaKey, 'hex'), plain).toString('hex').toUpperCase();
  const m = sunMac(Buffer.from(t.fileKey, 'hex'), plain.subarray(1, 11)).toString('hex').toUpperCase();
  return `/t/${t.id}?p=${p}&m=${m}`;
}

/** handlers_tags.cpp status, by the ticket the tap answered with. */
function tagStatus(id, ticket) {
  expire();
  const last = tagLast.get(id);
  if (!last || last.ticket !== ticket) return { state: 'none' };
  const s = machine.slot;
  const o = outcomes.get(last.serial);
  if (s?.kind === 'type' && s.req.serial === last.serial) {
    const connecting = s.req.target.kind === 'ble' && ble.connected !== s.req.target.addr;
    return { state: connecting ? 'waiting' : 'armed', expiresIn: s.deadline - Date.now() };
  }
  if (machine.typing && machine.typingSerial === last.serial) return { state: 'waiting' };
  if (o) return { state: TYPE_STATE[o.code] ?? 'failed', code: o.code };
  return { state: 'none' };
}

async function api(req, res, path) {
  const method = req.method;
  const via = viaOf(req);
  const cap = path === '/api/update' ? MAX_IMAGE : path === '/api/restore' ? MAX_RESTORE_BODY : MAX_BODY;
  if (Number(req.headers['content-length'] || 0) > cap) fail(413, 'too_large', 'Request body too large');
  const m = match(method, path);
  if (!m) fail(404, 'not_found', 'No such endpoint');
  if (m.notAllowed) fail(405, 'method_not_allowed', 'Method not allowed');
  if (method !== 'GET' && !originAllowed(req) && !(AGENT.has(m.route) && EXT_ORIGIN.test(req.headers.origin))) fail(403, 'csrf', 'Cross-origin request refused');

  expire();
  syncDemand();
  if (unlocked && Date.now() - lastActivity > settings.autoLockMin * 60000) {
    console.log('[mock] idle auto-lock');
    lockAll('idle');
  }

  // SPEC §17: /api/agent/… takes a bearer token, never the session; not user activity.
  let bearer = null;
  if (AGENT.has(m.route)) bearer = authenticateToken(req);

  const token = cookie(req, 'ks');
  requester = bearer ? `token:${bearer.id}` : token && sessions.has(token) ? token : '';
  const sess = token ? sessions.get(token) : undefined;
  if (sess) sess.lastUsed = Date.now();
  const session = !!sess && unlocked;
  if (!OPEN.has(m.route) && !bearer) {
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
        device: { ...device, name: settings.deviceName, powerDip: process.env.MOCK_POWER_DIP === '1' },
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
            ...(session ? { usbOs: settings.osUsb } : {}), // handlers.cpp getState: session only
          };
        })(),
        pending:
          session && s?.kind === 'type'
            ? { kind: 'type', ...pendingView(s.req), expiresIn: s.deadline - Date.now(), target: targetText(s.req.target), ...byOf(s.owner) }
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
          vault = { passphrase, entries: new Map(), passkeys: new Map(), fidoKeys: [], fidoCounter: 0 };
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
      failedBefore = failures - 1; // throttle() counted this (right) attempt too, like Vault::attempt
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
      failedBefore = failures - 1; // throttle() counted this (right) attempt too, like Vault::attempt
      failures = 0;
      lockedUntil = 0;
      const known = knownBrowser(req);
      if (via === 'home' && !known) return trustRequest(res, req);
      vault.passphrase = next;
      console.log('[mock] unlocked with the recovery key; passphrase replaced');
      return issueSession(res, req, known, 1);
    }

    case 'getRecovery':
      return send(res, 200, { enabled: !!vault.recovery, created: vault.recovery?.created ?? 0 });

    case 'createRecovery': {
      if (!consumeGrace(sess, 'recovery')) return requestPress(res, 'recovery', token);
      const key = randomBytes(20).toString('hex');
      vault.recovery = { key, created: nowSec() };
      logEvent('recovery_created');
      return send(res, 200, { recoveryKey: key, created: vault.recovery.created });
    }

    case 'deleteRecovery':
      if (!consumeGrace(sess, 'recovery')) return requestPress(res, 'recovery', token);
      if (!vault.recovery) fail(404, 'not_found', 'No recovery key');
      vault.recovery = null;
      logEvent('recovery_removed');
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
      logEvent('revealed', { id: e.id, title: e.title });
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

    case 'delete': {
      getEntry(m.id);
      const id = m.id;
      // Like commitDeleteEntry: on the press, only if still unlocked and still there.
      const exp = awaitPresence('delete_entry', () => {
        const gone = unlocked && vault?.entries.get(id);
        if (!gone) return false;
        vault.entries.delete(id);
        logEvent('entry_deleted', { id, title: gone.title });
      });
      return send(res, 202, { awaiting: 'button', op: 'delete_entry', expiresIn: exp, cancel: machine.slot?.cancel ?? '' });
    }

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
      if (b.probe !== undefined && typeof b.probe !== 'boolean') bad('"probe" must be a boolean');
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
        // validate::typeText: 1-256 code points, each typeable on the layout set for this output.
        const n = [...b.text].length;
        if (n < 1 || n > 256 || !typeable(b.text, layoutFor(target)))
          bad('text must be 1-256 characters the keyboard layout set for this output can type (no control characters)');
        if (b.repeat !== undefined && b.repeat !== 1 && b.repeat !== 2) bad('repeat must be 1 or 2');
        if (b.separator !== undefined && b.separator !== 'tab' && b.separator !== 'enter') bad('separator must be "tab" or "enter"');
        const req = { id: 0, title: null, what: 'text', submit: false, target, text: b.text, twice: b.repeat === 2, enterBetween: b.separator === 'enter' };
        const { text: _t, twice: _w, enterBetween: _e, ...pending } = arm(req);
        return send(res, 202, { pending: { kind: 'type', ...pending } });
      }
      if (b.test) return send(res, 202, { pending: { kind: 'type', ...arm({ id: 0, title: 'Keyra test', what: 'test', submit: false, target }) } });
      // Layout Doctor (SPEC §10.3).
      if (b.probe) return send(res, 202, { pending: { kind: 'type', ...arm({ id: 0, title: 'Keyboard check', what: 'probe', submit: false, target }) } });
      if (!Number.isInteger(b.id) || b.id < 1 || b.id > 0xffffffff) bad('"id" (entry id) is required');
      if (!['username', 'password', 'both', 'totp', 'sequence'].includes(b.what)) bad('"what" must be username, password, both, totp or sequence');
      if (b.submit !== undefined && typeof b.submit !== 'boolean') bad('"submit" must be a boolean');
      if (b.what === 'sequence' && b.submit !== undefined) bad('a sequence says itself whether to press Enter; "submit" is not allowed');
      const submit = b.submit ?? (b.what === 'both' && settings.submitAfterBoth);
      const e = getEntry(b.id);
      if (b.what === 'sequence') {
        // kbdapi::sequenceRequest: the entry's own, else settings.bothSequence, else the built-in Both order.
        const seq = seqParse(e.sequence || settings.bothSequence || seqBuiltIn());
        if (seq.error) fail(500, 'corrupt', seq.error);
        const needs = (k) => seq.steps.some((st) => st.kind === k);
        if ((needs('username') && !e.username) || (needs('password') && !e.password) || (needs('totp') && !e.totp)) bad('Entry has no value for a field its sequence types');
        if (needs('totp') && !timeValid) fail(409, 'no_time', 'Device clock is not set');
        const p = arm({ id: e.id, title: e.title, what: 'sequence', submit: false, target, seq, part: 0 });
        return send(res, 202, { pending: { kind: 'type', ...pendingView(p), expiresIn: p.expiresIn, target: p.target } });
      }
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

    case 'keyboard':
      return send(res, 200, {
        // Only US is confirmed on real hardware (SPEC §10.1).
        layouts: LAYOUTS.map((l) => ({ id: l.id, name: l.name, platform: l.platform, experimental: l.id !== 'us', probe: l.probe, chars: layoutChars(l) })),
        usb: settings.layoutUsb,
        ble: settings.layoutBle,
      });

    case 'presenceCancel': {
      // Like Machine::cancelPresence: only the named op, and nothing runs later.
      if (typeof b.op !== 'string') bad('op must name a presence operation');
      if (typeof b.cancel !== 'string') bad('cancel must be the token from the 202 answer');
      expire();
      const s = machine.slot;
      if (!(s?.kind === 'presence' && s.op === b.op && s.cancel && safeEqual(b.cancel, s.cancel)))
        fail(409, 'not_cancelled', 'Nothing of yours is waiting for the button');
      endCancelled(s);
      machine.slot = null;
      console.log(`[mock] ${s.op} cancelled from the app`);
      return send(res, 204);
    }

    case 'tokens':
      return send(res, 200, { tokens: (vault.tokens ?? []).map(tokenView), max: MAX_TOKENS });

    case 'createToken': {
      // handlers_agent.cpp createToken: checked first, then the press, then the same call again.
      if (typeof b.name !== 'string' || !b.name || Buffer.byteLength(b.name) > 48 || /[\x00-\x1f\x7f]/.test(b.name)) bad('"name" must be 1-48 bytes of text');
      if (!TOKEN_KINDS.includes(b.kind)) bad('"kind" must be "agent", "app" or "extension"');
      let scope = 'all';
      if (b.scope !== 'all') {
        if (!Array.isArray(b.scope) || b.scope.length < 1 || b.scope.length > MAX_SCOPE) bad('"scope" must be "all" or 1-32 entry ids');
        if (!b.scope.every((id) => Number.isInteger(id) && vault.entries.has(id))) bad('"scope" names an account that does not exist');
        scope = [...new Set(b.scope)];
      }
      vault.tokens ??= [];
      if (vault.tokens.length >= MAX_TOKENS) fail(409, 'tokens_full', 'Keyra holds at most 8 access tokens');
      if (!consumeGrace(sess, 'token_create')) return requestPress(res, 'token_create', token);
      const secret = 'keyra_' + base32Encode(randomBytes(20));
      let id;
      do id = randomBytes(4).readUInt32BE();
      while (!id || vault.tokens.some((x) => x.id === id));
      const t = { id, hash: sha(secret), name: b.name, kind: b.kind, scope, created: timeValid ? nowSec() : 0, lastUsed: 0 };
      vault.tokens.push(t);
      logEvent('token_created', { title: t.name, detail: TOKEN_KINDS.indexOf(t.kind) });
      console.log(`[mock] access token ${id} created (${t.kind})`);
      return send(res, 201, { token: secret, ...tokenView(t) });
    }

    case 'deleteToken': {
      const i = (vault.tokens ?? []).findIndex((x) => x.id === m.id);
      if (i < 0) fail(404, 'not_found', 'No such access token');
      const [t] = vault.tokens.splice(i, 1);
      tokenRate.delete(t.id);
      tokenLast.delete(t.id);
      const s = machine.slot;
      if (s && s.owner === `token:${t.id}`) {
        endCancelled(s);
        machine.slot = null;
        syncDemand();
      }
      logEvent('token_revoked', { title: t.name });
      return send(res, 204);
    }

    case 'tags':
      return send(res, 200, { tags: (vault.tags ?? []).map(tagView), max: MAX_TAGS });

    case 'createTag': {
      // handlers_tags.cpp createTag: checked first, then the press, then the same call again.
      if (typeof b.name !== 'string' || !b.name || Buffer.byteLength(b.name) > 48 || /[\x00-\x1f\x7f]/.test(b.name)) bad('"name" must be 1-48 bytes of text');
      if (b.kind !== 'simple' && b.kind !== 'secure') bad('"kind" must be "simple" or "secure"');
      if (!WHATS.includes(b.what)) bad('"what" must be username, password, both or totp');
      if (!Number.isInteger(b.entry) || b.entry < 1 || b.entry > 0xffffffff) bad('"entry" (account id) is required');
      const e = vault.entries.get(b.entry) ?? bad('"entry" names an account that does not exist');
      const missing = (b.what === 'username' && !e.username) || (b.what === 'password' && !e.password) || (b.what === 'both' && (!e.username || !e.password)) || (b.what === 'totp' && !e.totp);
      if (missing) bad('Entry has no value for that field');
      if (b.target !== undefined && (typeof b.target !== 'string' || !/^(usb|([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2})?$/.test(b.target))) bad('"target" must be "usb" or a device address');
      if (b.target && b.target !== 'usb') {
        if (!settings.bleEnabled) fail(409, 'ble_disabled', 'Bluetooth is turned off');
        if (!ble.bonds.some((x) => x.addr === b.target.toUpperCase())) fail(404, 'not_found', 'No such device');
      }
      vault.tags ??= [];
      if (vault.tags.length >= MAX_TAGS) fail(409, 'tags_full', 'Keyra holds at most 16 tags');
      if (!consumeGrace(sess, 'tag_create')) return requestPress(res, 'tag_create', token);
      let id;
      do id = randomBytes(4).readUInt32BE();
      while (!id || vault.tags.some((x) => x.id === id));
      const t = { id, name: b.name, kind: b.kind, entry: e.id, what: b.what, target: b.target ?? '', created: timeValid ? nowSec() : 0, lastUsed: 0 };
      let out;
      if (t.kind === 'simple') {
        const secret = base32Encode(randomBytes(15));
        t.hash = sha(secret);
        out = { url: `http://${TAG_HOST}/t/${id}/${secret}` };
      } else {
        Object.assign(t, { metaKey: randomBytes(16).toString('hex'), fileKey: randomBytes(16).toString('hex'), bound: false, uid: '', counter: 0 });
        out = { url: `http://${TAG_HOST}/t/${id}?p=${'0'.repeat(32)}&m=${'0'.repeat(16)}`, keys: { meta: t.metaKey, file: t.fileKey } };
      }
      vault.tags.push(t);
      logEvent('tag_created', { title: t.name, detail: t.kind === 'secure' ? 1 : 0 });
      console.log(`[mock] tag ${id} created (${t.kind})`);
      return send(res, 201, { ...out, ...tagView(t) });
    }

    case 'deleteTag': {
      const i = (vault.tags ?? []).findIndex((x) => x.id === m.id);
      if (i < 0) fail(404, 'not_found', 'No such tag');
      const [t] = vault.tags.splice(i, 1);
      tagRate.delete(t.id);
      tagLast.delete(t.id);
      const s = machine.slot;
      if (s && s.owner === `tag:${t.id}`) {
        endCancelled(s);
        machine.slot = null;
        syncDemand();
      }
      logEvent('tag_revoked', { title: t.name });
      return send(res, 204);
    }

    case 'tagTap': {
      // handlers_tags.cpp tap: not a session, not user activity; the tag's secret or SUN message is the key.
      if (!unlocked || !vault) fail(401, 'locked', 'Vault is locked');
      const refuse = () => {
        const wait = rateWait(0, tagRate);
        if (wait) rateFail(wait, 'Too many taps; wait a moment');
        fail(401, 'invalid_tag', 'This tag is not known to Keyra, or was revoked');
      };
      if (!Number.isInteger(b.id) || b.id < 1 || b.id > 0xffffffff) refuse();
      const simple = typeof b.secret === 'string';
      const secure = !simple && /^[0-9a-fA-F]{32}$/.test(b.p ?? '') && /^[0-9a-fA-F]{16}$/.test(b.m ?? '');
      if ((simple && !/^[a-z2-7]{24}$/.test(b.secret)) || (!simple && !secure)) refuse();
      const tag = (vault.tags ?? []).find((x) => x.id === b.id && x.kind === (simple ? 'simple' : 'secure'));
      if (!tag) refuse();
      const r = simple ? { v: safeEqual(tag.hash, sha(b.secret)) ? 'ok' : 'bad' } : verifySun(tag, b.p, b.m);
      if (r.v !== 'ok') {
        logCoalesced('tag_refused', { id: tag.id, title: tag.name, detail: r.v === 'replay' ? 1 : r.v === 'chip' ? 2 : 0 });
        if (r.v === 'replay') fail(409, 'replayed', 'This tap was already used; tap the tag again');
        refuse();
      }
      const wait = rateWait(tag.id, tagRate);
      if (wait) rateFail(wait, 'Too many taps; wait a moment');
      // The counter is used up even when arming fails below.
      if (!simple) Object.assign(tag, { bound: true, uid: r.uid, counter: r.ctr });
      if (timeValid && nowSec() - tag.lastUsed >= 60) tag.lastUsed = nowSec();
      requester = `tag:${tag.id}`;
      const { e, p } = armEntry(tag.entry, tag.what, tag.target || undefined, undefined);
      const ticket = randomBytes(4).readUInt32BE() || 1;
      tagLast.set(tag.id, { serial: machine.slot.req.serial, ticket });
      logEvent('tag_tapped', { id: e.id, title: tag.name, detail: WHATS.indexOf(tag.what) });
      return send(res, 202, { state: 'armed', title: e.title, what: tag.what, expiresIn: p.expiresIn, ticket });
    }

    case 'tagStatus':
      if (!Number.isInteger(b.id) || !Number.isInteger(b.ticket)) bad('"id" and "ticket" are required');
      return send(res, 200, tagStatus(b.id, b.ticket));

    case 'agentEntries': {
      const list = [...vault.entries.values()].filter((e) => inScope(bearer, e.id)).map((e) => ({ id: e.id, title: e.title, host: urlHost(e.url) }));
      logCoalesced('agent_listed', { id: bearer.id, title: bearer.name });
      return send(res, 200, { entries: list });
    }

    case 'agentType': {
      if (!Number.isInteger(b.id) || b.id < 1 || b.id > 0xffffffff) bad('"id" (entry id) is required');
      if (!['username', 'password', 'both', 'totp'].includes(b.what)) bad('"what" must be username, password, both or totp');
      if (!inScope(bearer, b.id)) fail(404, 'not_found', 'No such entry');
      if (b.submit !== undefined && typeof b.submit !== 'boolean') bad('"submit" must be a boolean');
      // SPEC §9.4: an extension says which page it types into; another site's login needs anyHost.
      let host;
      if (bearer.kind === 'extension') {
        if (typeof b.host !== 'string' || !normHost(b.host) || b.host.length > 253) bad('"host" (the page\'s host) is required');
        if (b.anyHost !== undefined && typeof b.anyHost !== 'boolean') bad('"anyHost" must be a boolean');
        host = normHost(b.host);
        if (!hostMatch(urlHost(getEntry(b.id).url), host) && b.anyHost !== true) fail(409, 'host_mismatch', 'This login is for another site');
      }
      const { e, p } = armEntry(b.id, b.what, b.target, b.submit, host);
      tokenLast.set(bearer.id, { serial: machine.slot.req.serial, save: false, id: e.id, title: e.title, what: b.what });
      const other = host && !hostMatch(urlHost(e.url), host);
      logEvent('agent_armed', { id: e.id, title: bearer.name, detail: ['username', 'password', 'both', 'totp'].indexOf(b.what), ...(other ? { host } : {}) });
      return send(res, 202, { pending: { kind: 'type', ...p, by: bearer.name }, expiresIn: p.expiresIn });
    }

    case 'agentStatus':
      return send(res, 200, agentStatus(bearer));

    case 'agentCancel': {
      expire();
      const s = machine.slot;
      if (!s || s.owner !== `token:${bearer.id}`) fail(409, 'not_cancelled', 'Nothing of this token is waiting for the button');
      endCancelled(s);
      machine.slot = null;
      syncDemand();
      return send(res, 204);
    }

    case 'agentSave': {
      if (bearer.kind === 'agent') fail(403, 'forbidden', 'This token cannot save accounts');
      for (const k of ['title', 'url', 'username', 'password']) {
        if (b[k] !== undefined && (typeof b[k] !== 'string' || Buffer.byteLength(b[k]) > LIMITS[k])) bad(`"${k}" must be a string within the vault's limits`);
      }
      const tokenId = bearer.id;
      const name = bearer.name;
      // SPEC §9.4 "Save or update": replace sets the username (if given) and password of a login in scope.
      if (b.replace !== undefined) {
        if (!Number.isInteger(b.replace) || b.replace < 1 || b.replace > 0xffffffff) bad('"replace" must be an entry id');
        if (!b.password) bad('"password" is required to update a login');
        if (!inScope(bearer, b.replace)) fail(404, 'not_found', 'No such entry');
        const id = getEntry(b.replace).id;
        const exp = awaitPresence('agent_save', () => {
          const old = unlocked && vault?.entries.get(id);
          if (!old) return false;
          const now = nowSec();
          const e = { ...old, username: b.username ?? old.username, password: b.password, updated: now };
          if (old.password && old.password !== b.password) e.history = [{ password: old.password, changedAt: now }, ...old.history].slice(0, MAX_HISTORY);
          vault.entries.set(id, e);
          const last = tokenLast.get(tokenId);
          if (last?.serial === serial) last.id = id;
          logEvent('agent_saved', { id, title: name });
        });
        const serial = machine.slot.serial;
        tokenLast.set(tokenId, { serial, save: true, id: 0 });
        return send(res, 202, { awaiting: 'button', op: 'agent_save', expiresIn: exp, mode: 'update' });
      }
      if (!b.title) bad('"title" (string) is required');
      const exp = awaitPresence('agent_save', () => {
        if (!unlocked) return false;
        const id = freshId();
        const now = nowSec();
        vault.entries.set(id, { id, title: b.title, url: b.url ?? '', username: b.username ?? '', password: b.password ?? '', totp: '', notes: '', sequence: '', favorite: false, created: now, updated: now, lastUsed: 0, history: [], burnAfter: 0 });
        const last = tokenLast.get(tokenId);
        if (last?.serial === serial) last.id = id;
        const t = vault.tokens?.find((x) => x.id === tokenId);
        if (t && t.scope !== 'all' && t.scope.length < MAX_SCOPE) t.scope.push(id);
        logEvent('agent_saved', { id, title: name });
      });
      const serial = machine.slot.serial;
      tokenLast.set(tokenId, { serial, save: true, id: 0 });
      return send(res, 202, { awaiting: 'button', op: 'agent_save', expiresIn: exp, mode: 'create' });
    }

    case 'agentMatch': {
      if (bearer.kind !== 'extension') fail(403, 'forbidden', 'Only a browser extension token can match pages');
      if (typeof b.host !== 'string' || b.host.length > 253) bad('"host" (string) is required');
      if (b.username !== undefined && typeof b.username !== 'string') bad('"username" must be a string');
      const list = [...vault.entries.values()]
        .filter((e) => inScope(bearer, e.id) && hostMatch(urlHost(e.url), b.host))
        .slice(0, 20)
        .map((e) => ({ id: e.id, title: e.title, host: urlHost(e.url), ...(b.username !== undefined ? { sameUser: e.username === b.username } : {}) }));
      logCoalesced('agent_listed', { id: bearer.id, title: bearer.name });
      return send(res, 200, { entries: list });
    }

    case 'agentGenerate': {
      if (bearer.kind === 'agent') fail(403, 'forbidden', 'This token cannot generate passwords');
      logCoalesced('agent_generated', { id: bearer.id, title: bearer.name });
      const r = generate(b);
      if (typeof r === 'string') bad(r);
      return send(res, 200, r);
    }

    case 'typeCancel': {
      const s = machine.slot;
      if (s?.kind === 'type') {
        endCancelled(s);
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
      for (const k of ['layoutUsb', 'layoutBle']) {
        if (b[k] === undefined) continue;
        if (typeof b[k] !== 'string' || !layoutById(b[k])) bad(LAYOUT_ERR);
        next[k] = b[k];
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
      if (b.bothSequence !== undefined) {
        if (typeof b.bothSequence !== 'string') bad('bothSequence must be a string');
        const r = b.bothSequence ? seqParse(b.bothSequence) : {};
        if (r.error) bad(r.error);
        next.bothSequence = b.bothSequence;
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
      for (const k of ['lockOnUsb', 'lockOnBle', 'protectReveal', 'passkeysInBackup']) {
        if (b[k] === undefined) continue;
        if (typeof b[k] !== 'boolean') bad(`${k} must be a boolean`);
        next[k] = b[k];
      }
      // Turning protection off waits for the button (SPEC §12.3); turning it on applies at once.
      const unprotect = settings.protectReveal && next.protectReveal === false;
      if (unprotect) next.protectReveal = true;
      // Putting passkeys (and their wrap keys) into backups waits for the button; leaving them out doesn't.
      const passkeysOn = !settings.passkeysInBackup && next.passkeysInBackup === true;
      if (passkeysOn) next.passkeysInBackup = false;
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
      if (passkeysOn && !(ssid || pw)) {
        return awaiting(
          res,
          awaitPresence('passkeys_backup_on', () => {
            settings.passkeysInBackup = true;
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
      // Vault::changePassphrase goes through attempt(): a wrong `current` is throttled like unlock.
      throttle();
      await sleep(KDF_MS);
      if (cur !== vault.passphrase) wrongAttempt('Wrong passphrase');
      failedBefore = failures - 1;
      failures = 0;
      lockedUntil = 0;
      vault.passphrase = nxt;
      logEvent('passphrase');
      return send(res, 204);
    }

    case 'backup': {
      const pass = str(b, 'passphrase');
      if ([...pass].length < 12 || Buffer.byteLength(pass) > 1024) bad('backup passphrase must be at least 12 characters');
      if (!mayBackup(sess)) return requestPress(res, 'backup', token);
      settings.lastBackupAt = nowSec();
      const d = new Date();
      const ymd = `${d.getUTCFullYear()}${String(d.getUTCMonth() + 1).padStart(2, '0')}${String(d.getUTCDate()).padStart(2, '0')}`;
      const out = exportBackup(pass, settings.passkeysInBackup);
      logEvent('backup');
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
        // postRestore: a wrong passphrase or a bad file is reported now (vault::checkBackup), not after the press.
        await sleep(KDF_MS);
        const checked = openBackup(pass, b.backup);
        if (checked === 'wrong') fail(401, 'wrong', 'Wrong passphrase');
        if (checked === 'invalid') bad('Invalid data');
        return awaiting(
          res,
          awaitPresence('restore', () => {
            const file = openBackup(pass, b.backup);
            if (typeof file === 'string') return false;
            const pk = restorePasskeys(file.passkeys, true);
            const r = importBackup(file.entries, true);
            if (typeof r === 'string') return false;
            commitPasskeys(pk);
            logEvent('restore', { detail: 1, n: r.added });
            return true;
          }),
        );
      }
      await sleep(KDF_MS);
      const file = openBackup(pass, b.backup);
      if (file === 'wrong') fail(401, 'wrong', 'Wrong passphrase');
      if (file === 'invalid') bad('Invalid data');
      // Refused before anything changes.
      const pk = restorePasskeys(file.passkeys, false);
      if (pk === 'passkeys_full') fail(409, 'passkeys_full', 'Keyra holds at most 50 passkeys and 4 wrap keys');
      const r = importBackup(file.entries, false);
      if (r === 'full') fail(507, 'full', 'Vault is full');
      commitPasskeys(pk);
      logEvent('restore', { n: r.added + r.updated });
      return send(res, 200, { ...r, passkeys: pk.added });
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

    case 'healthRotate': {
      if (typeof b.on !== 'boolean') bad('"on" (boolean) is required');
      if (b.on && !timeValid) fail(409, 'no_time', 'Device clock is not set');
      const was = settings.rotateSince;
      settings.rotateSince = b.on ? nowSec() : 0;
      if (b.on) logEvent('rotate_started');
      else if (was) logEvent('rotate_ended');
      return send(res, 200, health([...vault.entries.values()]));
    }

    case 'activity':
      return send(res, 200, { events: [...(vault.activity ?? [])].reverse().map(({ id, n, title, ...e }) => ({ ...e, ...(id ? { id } : {}), ...(n ? { n } : {}), ...(title ? { title } : {}) })), max: ACTIVITY_MAX });

    case 'passkeys': {
      const list = [...vault.passkeys.values()].sort((a, b) => b.created - a.created).map(({ cred, ...p }) => p);
      const pin = vault.fidoPin ?? { set: false, retries: 8 }; // docs/FIDO.md "ClientPIN"; set from the computer
      return send(res, 200, { passkeys: list, max: MAX_PASSKEYS, pinSet: pin.set, pinRetries: pin.retries });
    }

    case 'deletePasskey': {
      if (!vault.passkeys.has(m.id)) fail(404, 'not_found', 'No such passkey');
      const id = m.id;
      const exp = awaitPresence('delete_passkey', () => {
        if (!unlocked || !vault?.passkeys.delete(id)) return false;
        console.log(`[mock] passkey ${id} deleted`);
      });
      return send(res, 202, { awaiting: 'button', op: 'delete_passkey', expiresIn: exp, cancel: machine.slot?.cancel ?? '' });
    }

    case 'untrust': {
      const entry = [...trusted.entries()].find(([, t]) => t.id === m.id);
      if (!entry) fail(404, 'not_found', 'No such trusted browser');
      const wasMine = knownBrowser(req) === entry[1];
      logEvent('trusted_removed', { title: entry[1].name });
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
          logEvent('ble_pairing');
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
      logEvent('ble_forgot', { title: ble.bonds[i].name || m.addr });
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
  bothSequence: settings.bothSequence,
  layoutUsb: settings.layoutUsb,
  layoutBle: settings.layoutBle,
  homeWifi: { enabled: settings.homeWifi.enabled, ssid: settings.homeWifi.ssid },
  apMode: settings.apMode,
  protectReveal: settings.protectReveal,
  passkeysInBackup: settings.passkeysInBackup,
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
  // SPEC §18: a tag's URL serves the tap page, which changes nothing by itself.
  const asset = req.method === 'GET' ? (/^\/t\/./.test(path) ? ['tap.html', 'text/html; charset=utf-8'] : ASSETS[path]) : undefined;
  const file = asset && DIST + asset[0];
  if (!file || !existsSync(file)) {
    const hint = asset ? 'Run `npm run build` first (or use `npm run dev`).' : 'Not found';
    return send(res, 404, { error: 'not_found', message: hint });
  }
  res.writeHead(200, {
    ...SECURITY,
    'Content-Type': asset[1],
    'Cache-Control': asset[0] === 'index.html' ? 'no-cache' : asset[0] === 'tap.html' ? 'no-store' : 'public, max-age=31536000',
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
    noteOutcome(s.req.serial, 'host_changed', false);
    machine.slot = null;
  }
  // The firmware waits 1 s to ride out a bus reset; the mock locks at once.
  if (unlocked && usbSeen && settings.lockOnUsb) {
    console.log('[mock] USB host gone: auto-lock');
    lockAll('usb');
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
  if (path === '/__mock/fido' && typeof b.pinSet === 'boolean' && vault) {
    const retries = Number.isInteger(b.pinRetries) && b.pinRetries >= 0 && b.pinRetries <= 8 ? b.pinRetries : 8;
    vault.fidoPin = { set: b.pinSet, retries: b.pinSet ? retries : 8 };
    return send(res, 200, vault.fidoPin);
  }
  if (path === '/__mock/sun' && Number.isInteger(b.id)) {
    const t = vault?.tags?.find((x) => x.id === b.id && x.kind === 'secure');
    if (!t) return send(res, 404, { error: 'not_found', message: 'No such secure tag' });
    return send(res, 200, { url: simulateSun(t, b.ctr) });
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
