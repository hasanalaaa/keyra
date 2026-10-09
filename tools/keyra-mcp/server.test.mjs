// End-to-end test of keyra-mcp against the device mock (web/mock/server.mjs):
//   node --test tools/keyra-mcp/server.test.mjs
// Creates an agent token with a (simulated) press, then drives the MCP server over stdio.
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createInterface } from 'node:readline';
import { after, before, test } from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';

const MOCK = fileURLToPath(new URL('../../web/mock/server.mjs', import.meta.url));
const SERVER = fileURLToPath(new URL('./server.mjs', import.meta.url));
const PORT = 18000 + Math.floor(Math.random() * 1000);
// "localhost" reaches the mock as Keyra's own Wi-Fi (no trusted-browser step).
const BASE = `http://localhost:${PORT}`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let mock;
let cookie = '';
let csrf = '';

async function api(method, path, body) {
  const res = await fetch(BASE + path, {
    method,
    headers: { 'Content-Type': 'application/json', Cookie: cookie, 'X-Keyra-CSRF': csrf, 'X-Keyra-Time': String(Date.now()) },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const set = res.headers.get('set-cookie');
  if (set) cookie = set.split(';')[0];
  return { status: res.status, body: res.status === 204 ? null : await res.json() };
}
const press = (kind = 'short') => api('POST', '/__mock/button', { press: kind });

async function waitFor(pred, ms = 5000) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (await pred()) return;
    await sleep(50);
  }
  throw new Error('timed out');
}

/** Creates a token through the web app's flow: 202 → press → the same call again → 201. */
async function createToken(name, scope) {
  const first = await api('POST', '/api/tokens', { name, kind: 'agent', scope });
  assert.equal(first.status, 202);
  assert.equal(first.body.op, 'token_create');
  await press();
  await waitFor(async () => {
    const p = (await api('GET', '/api/state')).body.presence;
    return p.op === null && p.result?.op === 'token_create' && p.result.code === 'done';
  });
  const r = await api('POST', '/api/tokens', { name, kind: 'agent', scope });
  assert.equal(r.status, 201);
  assert.match(r.body.token, /^keyra_[a-z2-7]{32}$/);
  return r.body;
}

function mcp(token) {
  const child = spawn(process.execPath, [SERVER], {
    env: { ...process.env, KEYRA_URL: BASE, KEYRA_TOKEN: token, KEYRA_POLL_MS: '200' },
    stdio: ['pipe', 'pipe', 'inherit'],
  });
  const waiting = new Map();
  const transcript = [];
  createInterface({ input: child.stdout }).on('line', (line) => {
    transcript.push(line);
    const msg = JSON.parse(line);
    waiting.get(msg.id)?.(msg);
  });
  let next = 1;
  const request = (method, params) => {
    const id = next++;
    child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
    return new Promise((resolve) => waiting.set(id, resolve));
  };
  const notify = (method) => child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method }) + '\n');
  const tool = async (name, args) => (await request('tools/call', { name, arguments: args })).result;
  return { child, request, notify, tool, transcript };
}

before(async () => {
  mock = spawn(process.execPath, [MOCK], { env: { ...process.env, PORT: String(PORT) }, stdio: 'ignore' });
  await waitFor(async () => {
    try {
      return (await fetch(`${BASE}/api/state`)).ok;
    } catch {
      return false;
    }
  });
  const u = await api('POST', '/api/unlock', { passphrase: 'keyra demo vault' });
  assert.equal(u.status, 200);
  csrf = u.body.csrf;
});

after(() => mock?.kill());

test('an agent lists and types logins without ever receiving a secret', async () => {
  const entries = (await api('GET', '/api/entries')).body.entries;
  const github = entries.find((e) => e.title === 'GitHub');
  const google = entries.find((e) => e.title === 'Google');
  const tok = await createToken('Claude test', [github.id]);

  const s = mcp(tok.token);
  try {
    const init = await s.request('initialize', { protocolVersion: '2025-06-18', capabilities: {}, clientInfo: { name: 'test', version: '1' } });
    assert.equal(init.result.serverInfo.name, 'keyra-mcp');
    assert.equal(init.result.protocolVersion, '2025-06-18');
    s.notify('notifications/initialized');
    const tools = (await s.request('tools/list', {})).result.tools.map((t) => t.name);
    assert.deepEqual(tools, ['keyra_list_logins', 'keyra_type_login']);
    assert.equal((await s.request('nope/x', {})).error.code, -32601);

    // Scope: only GitHub, with its host and no username.
    const list = await s.tool('keyra_list_logins', {});
    assert.equal(list.isError, false);
    assert.equal(list.content[0].text, `${github.id}: GitHub (github.com)`);

    // Typed after the press.
    const typing = s.tool('keyra_type_login', { id: github.id, what: 'password' });
    await waitFor(async () => (await api('GET', '/api/state')).body.pending?.by === 'Claude test');
    await press();
    assert.equal((await typing).content[0].text, 'typed');

    // Denied with a long press.
    const denied = s.tool('keyra_type_login', { id: github.id, what: 'both' });
    await waitFor(async () => (await api('GET', '/api/state')).body.pending !== null);
    await press('long');
    assert.equal((await denied).content[0].text, 'denied');

    // Outside the scope answers like an unknown login.
    const outside = await s.tool('keyra_type_login', { id: google.id });
    assert.equal(outside.isError, true);
    assert.match(outside.content[0].text, /No such login/);

    // Nothing secret ever crossed the MCP channel.
    const all = s.transcript.join('\n');
    for (const secret of ['gh!R3d-Lantern-Fox', 'hasanalaaa', 'GEZDGNBVGY3TQOJQ', tok.token]) assert.ok(!all.includes(secret), secret);

    // Revoked: the agent is refused.
    assert.equal((await api('DELETE', `/api/tokens/${tok.id}`)).status, 204);
    const refused = await s.tool('keyra_list_logins', {});
    assert.equal(refused.isError, true);
    assert.match(refused.content[0].text, /unknown or revoked/);
  } finally {
    s.child.kill();
  }
  // Every use is in the activity log.
  const kinds = (await api('GET', '/api/activity')).body.events.map((e) => e.kind);
  for (const k of ['token_created', 'agent_listed', 'agent_armed', 'typed', 'token_revoked']) assert.ok(kinds.includes(k), k);
});

test('a locked Keyra and a missing token are explained, not crashed on', async () => {
  const s = mcp('');
  try {
    const r = await s.tool('keyra_list_logins', {});
    assert.equal(r.isError, true);
    assert.match(r.content[0].text, /KEYRA_TOKEN/);
  } finally {
    s.child.kill();
  }
  const tok = await createToken('Locked test', 'all');
  await api('POST', '/api/lock');
  const t = mcp(tok.token);
  try {
    const r = await t.tool('keyra_list_logins', {});
    assert.equal(r.isError, true);
    assert.match(r.content[0].text, /locked/);
  } finally {
    t.child.kill();
    await once(t.child, 'exit');
  }
});
