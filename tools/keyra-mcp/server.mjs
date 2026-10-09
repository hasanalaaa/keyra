#!/usr/bin/env node
// keyra-mcp: a Model Context Protocol server (stdio) that lets an AI agent ask Keyra to
// type a login — and never see it (SPEC §17). Dependency-free: Node ≥ 20 only.
//
//   KEYRA_URL    http://keyra.local (default), or Keyra's home-network address
//   KEYRA_TOKEN  an access token from Keyra: Settings → Apps and agents (kind "AI agent")
//
// Tools: keyra_list_logins (account names and hosts in the token's scope) and
// keyra_type_login (arms typing, waits for the human's press on Keyra, answers
// "typed", "denied" or "expired"). No endpoint this token can reach returns a
// password, a username or a 2FA secret, so no tool result can contain one.
import { createInterface } from 'node:readline';

const URL_BASE = (process.env.KEYRA_URL || 'http://keyra.local').replace(/\/+$/, '');
const TOKEN = process.env.KEYRA_TOKEN || '';
// How often keyra_type_login asks whether the press happened (Keyra allows 10 requests per 10 s).
const POLL_MS = Math.max(200, Number(process.env.KEYRA_POLL_MS) || 2000);
const VERSION = '0.1.0';
const PROTOCOLS = ['2025-06-18', '2025-03-26', '2024-11-05'];

const TOOLS = [
  {
    name: 'keyra_list_logins',
    description:
      'List the logins stored on the Keyra hardware password manager that this agent may use: id, title and website host. Never returns usernames or passwords.',
    inputSchema: {
      type: 'object',
      properties: { query: { type: 'string', description: 'Optional text to filter titles and hosts (case-insensitive).' } },
      additionalProperties: false,
    },
  },
  {
    name: 'keyra_type_login',
    description:
      "Ask Keyra to type a stored login into the field that has keyboard focus on the user's computer. First click into the login field. Keyra waits up to 60 s for the user to press its button, then types it as a keyboard; the secret never passes through this tool. Returns typed, denied or expired.",
    inputSchema: {
      type: 'object',
      properties: {
        id: { type: 'integer', description: 'The login id from keyra_list_logins.' },
        what: { type: 'string', enum: ['username', 'password', 'both', 'totp'], description: 'What to type. "both" = username, Tab, password. Default "both".' },
        submit: { type: 'boolean', description: 'Press Enter afterwards.' },
        target: { type: 'string', description: 'Optional: "usb" or a paired Bluetooth device address. Default: Keyra\'s own choice.' },
      },
      required: ['id'],
      additionalProperties: false,
    },
  },
];

class KeyraError extends Error {
  constructor(status, code, message, retryAfterMs = 0) {
    super(message);
    this.status = status;
    this.code = code;
    this.retryAfterMs = retryAfterMs;
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function call(method, path, body) {
  if (!TOKEN) throw new KeyraError(0, 'no_token', 'KEYRA_TOKEN is not set: create a token in Keyra → Settings → Apps and agents.');
  let res;
  try {
    res = await fetch(`${URL_BASE}/api/agent${path}`, {
      method,
      headers: {
        Authorization: `Bearer ${TOKEN}`,
        'X-Keyra-Time': String(Date.now()),
        ...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
      },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(15000),
    });
  } catch {
    throw new KeyraError(0, 'network', `Keyra is not reachable at ${URL_BASE}.`);
  }
  if (res.status === 204) return null;
  const json = await res.json().catch(() => ({}));
  if (!res.ok) throw new KeyraError(res.status, json.error ?? `http_${res.status}`, json.message ?? res.statusText, json.retryAfterMs ?? 0);
  return json;
}

/** Retries once after Keyra's rate limit says how long to wait. */
async function callPatient(method, path, body) {
  try {
    return await call(method, path, body);
  } catch (e) {
    if (e instanceof KeyraError && e.status === 429) {
      await sleep(Math.min(10000, e.retryAfterMs || 1000));
      return call(method, path, body);
    }
    throw e;
  }
}

function explain(e) {
  if (!(e instanceof KeyraError)) return 'Keyra request failed.';
  switch (e.code) {
    case 'locked': return 'Keyra is locked. Ask the user to unlock it in the Keyra web app, then try again.';
    case 'invalid_token': return 'Keyra refused the access token (unknown or revoked). Ask the user for a new one.';
    case 'busy': return 'Keyra is waiting for another request. Ask the user to finish or cancel it (long-press the button), then try again.';
    case 'not_found': return 'No such login for this agent. Use keyra_list_logins.';
    case 'rate_limited': return 'Too many requests to Keyra. Wait a few seconds.';
    default: return `${e.message} (${e.code})`;
  }
}

async function listLogins(args) {
  const { entries } = await callPatient('GET', '/entries');
  const q = typeof args.query === 'string' ? args.query.toLowerCase() : '';
  const rows = entries.filter((e) => !q || e.title.toLowerCase().includes(q) || e.host.includes(q));
  if (rows.length === 0) return q ? `No logins match "${args.query}".` : 'No logins are shared with this agent.';
  return rows.map((e) => `${e.id}: ${e.title}${e.host ? ` (${e.host})` : ''}`).join('\n');
}

async function typeLogin(args) {
  if (!Number.isInteger(args.id) || args.id < 1) throw new KeyraError(400, 'invalid', '"id" must be a login id from keyra_list_logins.');
  const body = { id: args.id, what: args.what ?? 'both' };
  if (typeof args.submit === 'boolean') body.submit = args.submit;
  if (typeof args.target === 'string') body.target = args.target;
  const armed = await callPatient('POST', '/type', body);
  const deadline = Date.now() + (armed.expiresIn ?? 60000) + 5000;
  while (Date.now() < deadline) {
    await sleep(POLL_MS);
    const s = await callPatient('GET', '/status');
    if (s.state === 'typed') return 'typed';
    if (s.state === 'expired') return 'expired';
    if (s.state === 'cancelled') return 'denied';
    if (s.state === 'failed') return `denied: Keyra could not type (${s.code ?? 'failed'})`;
    if (s.state === 'none') return 'denied';
  }
  // Keyra stopped answering about it: withdraw it so a later press types nothing.
  await call('POST', '/cancel').catch(() => {});
  return 'expired';
}

async function callTool(name, args) {
  try {
    const text = name === 'keyra_list_logins' ? await listLogins(args) : await typeLogin(args);
    return { content: [{ type: 'text', text }], isError: false };
  } catch (e) {
    return { content: [{ type: 'text', text: explain(e) }], isError: true };
  }
}

// ---------- JSON-RPC 2.0 over stdio (one message per line) ----------

const send = (msg) => process.stdout.write(JSON.stringify({ jsonrpc: '2.0', ...msg }) + '\n');
const reply = (id, result) => send({ id, result });
const error = (id, code, message) => send({ id, error: { code, message } });

async function handle(msg) {
  const { id, method, params = {} } = msg;
  const isRequest = id !== undefined && id !== null;
  switch (method) {
    case 'initialize': {
      const asked = params.protocolVersion;
      return reply(id, {
        protocolVersion: PROTOCOLS.includes(asked) ? asked : PROTOCOLS[0],
        capabilities: { tools: { listChanged: false } },
        serverInfo: { name: 'keyra-mcp', version: VERSION },
        instructions:
          'Keyra types logins as a keyboard after its owner presses its button. Click into the login field first, then call keyra_type_login. You never receive the password.',
      });
    }
    case 'ping':
      return isRequest && reply(id, {});
    case 'tools/list':
      return reply(id, { tools: TOOLS });
    case 'tools/call': {
      const tool = TOOLS.find((t) => t.name === params.name);
      if (!tool) return error(id, -32602, `Unknown tool: ${params.name}`);
      return reply(id, await callTool(tool.name, params.arguments ?? {}));
    }
    default:
      // Notifications (no id) need no answer, known or not.
      if (isRequest) error(id, -32601, `Method not found: ${method}`);
  }
}

const rl = createInterface({ input: process.stdin, crlfDelay: Infinity });
rl.on('line', (line) => {
  if (!line.trim()) return;
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return send({ id: null, error: { code: -32700, message: 'Parse error' } });
  }
  if (Array.isArray(msg) || typeof msg !== 'object' || msg === null) return send({ id: null, error: { code: -32600, message: 'Invalid Request' } });
  handle(msg).catch((e) => msg.id != null && error(msg.id, -32603, String(e?.message ?? e)));
});
