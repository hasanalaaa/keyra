# Access tokens and the Agent Gate (design note)

Written 2026-10-08 for ROADMAP wave 5 "Agent Gate" ([FEATURES-2.md](FEATURES-2.md)
M08/D7, [FEATURES.md](FEATURES.md) B7 "arm-only tokens"). The contract that
other work builds on (the Android app, NTAG tap tags) is SPEC §17; this note
records why it looks the way it does.

## The problem

An AI agent (Claude Desktop, Claude Code, a browser agent) or a phone app wants
to log in to something with an account stored on Keyra. Giving it the password
puts the secret into a model's context, its logs and its provider's servers.
Keyra already has the right primitive: it *types* a credential after a physical
press, so the secret goes from the vault straight to the focused field through
the keyboard path and never through the caller. What is missing is a way for a
program, rather than a browser session, to ask for that — without a session
cookie, the master passphrase or a CSRF dance, and without ever being able to
read anything secret.

## Decisions

| Question | Decision | Why |
|---|---|---|
| Credential | A bearer **access token**: `keyra_` + 32 base32 characters (RFC 4648 lowercase, 160 random bits from the hardware RNG). Shown once. | Copy-pasteable into an MCP config and a QR code; the prefix lets secret scanners and the user recognise it. |
| At rest | Only SHA-256(token) plus metadata, in a new vault record `tokens.bin` sealed with the data key (AES-256-GCM, AAD `keyra/tokens/v1`). | Same pattern as the activity log. Nothing about tokens (names, scopes) is readable from flash, and a token can only be checked while the vault is unlocked — which typing needs anyway. Simpler than NVS + its own encryption. |
| Locked vault | Every `/api/agent/*` call → 401 `locked`. | The token hashes are not readable without the DEK, and arming a type action needs the vault unlocked anyway. The agent tells the human to unlock Keyra. |
| Creation | Web app, Settings → *Apps and agents*: `POST /api/tokens` → 202 press (`token_create`) → the same call again within 60 s → 201 with the token. | The recovery-key pattern (SPEC §12.5a): a press grants one creation to the session that asked. A stolen session cannot mint a token silently. |
| Revocation | `DELETE /api/tokens/{id}` → 204, no press. | Taking power away must be instant and never blocked by "press the button" (the device may be in another room). Logged. |
| Limit | 8 tokens; scope `"all"` or up to 32 entry ids. | Bounds the record (~3 KiB) and the per-request work. |
| Kinds | `agent`: list, type, status, cancel. `app`: those plus save and generate. | An agent only needs to name an account and arm it. The Android app also needs to store a new sign-up and propose a password. |
| Listing | Both kinds may list titles and URL hosts in scope — never usernames, never ids outside scope. | The agent must be able to pick the account ("which GitHub?"); a title and a host are what the user sees on the page anyway. Usernames are withheld because they are half of a credential and are not needed to choose. |
| Out of scope | An id outside the scope answers exactly like an unknown id (404 `not_found`). | No oracle for which ids exist. |
| Arming | `POST /api/agent/type` reuses the web app's pending machine: same 60 s, same press, same targets (§8.1), same Caps Lock / layout rules, 409 `busy` while another item waits. The owner of the item is `token:<id>`, so a token can replace only its own item and nobody can replace another's (§12.5a). | One state machine, one button meaning. |
| Status | `GET /api/agent/status` reports *this token's* last request by a serial the machine gives every armed item (`armed`, `waiting`, `typed`, `saved`, `cancelled`, `expired`, `failed`, `none`). | The global `state.last` can be overwritten by the phone; a per-request serial cannot be confused with someone else's action. |
| Rate limit | 10 requests per 10 s per token (sliding window); all unrecognised tokens share one more bucket of 10 per 10 s; 429 `rate_limited` with `Retry-After` and `retryAfterMs`. | Stops a looping agent from flooding the activity log or the LED; the shared bucket keeps guessing cheap to refuse. Guessing is hopeless anyway (160 bits). |
| Comparison | SHA-256 of the presented token compared against every stored hash with a constant-time loop; the token is never logged. | Timing does not reveal which record matched or how much of a hash agreed. |
| Activity log | `token_created`, `token_revoked`, `agent_listed` (consecutive lists of one token are counted, not repeated), `agent_armed` (entry id, what), `agent_saved`, `agent_generated`; the typing itself is the usual `typed` event. Status and cancel polls are not logged. | Every use is visible, but a chatty agent cannot push the rest of the history out of the 200-event window by listing. |
| Auto-lock | Token requests are not user activity. | An agent left running must not keep the vault open forever. |
| Generate (app) | Returns a generated password that is **not stored**. | It was never in the vault: the app needs it to fill a sign-up form's new-password field, and saving it afterwards (`/api/agent/save`) needs a press. Exposing a fresh random string grants nothing over what the app could generate itself; doing it on Keyra gives the hardware RNG and the layout-safe option. |
| Save (app) | `POST /api/agent/save` → 202, the press creates the entry; status then carries its id. A token scoped to a list gets the new id added to its scope (while there is room). | The human approves every write. Adding to scope lets the app type what it just saved. |

## Request flow (agent)

```
agent ── GET  /api/agent/entries ───────────▶ titles + hosts in scope
agent ── POST /api/agent/type {id, what} ───▶ 202 {pending, expiresIn}      LED pulses, phone shows "Ready"
human ── presses Keyra's button ───────────▶ Keyra types into the focused field
agent ── GET  /api/agent/status ───────────▶ {state:"typed"}   (or cancelled / expired / failed)
```

The MCP server (`tools/keyra-mcp/`) wraps exactly this: `keyra_list_logins`,
and `keyra_type_login`, which arms, polls status every 2 s and returns one word
(`typed`, `denied` or `expired`, with the reason for a denial). No tool result
can contain a password, because no endpoint a token reaches returns one
(generate, app tokens only, returns a password that was never stored).

## Threat model

- **LAN sniffing.** Keyra speaks plain HTTP (SPEC §6). On the home network a
  bearer token can be captured by anyone who can see the traffic. With it an
  attacker can list titles and hosts in scope and *arm* typing — never read a
  password, a username or a 2FA secret — and every typing still waits for the
  human's press, which they see on the LED and on the phone ("requested by …").
  Revoking the token in Settings stops it at once. Recommendation in the UI:
  scope a token to the accounts it needs.
- **Loopjacking-style approval hijack** ([arXiv 2609.21081](https://arxiv.org/pdf/2609.21081)):
  an attacker arms their own action just before the human expects one, so the
  human's approval goes to the wrong request. Mitigations: one item at a time
  and nobody can replace another owner's item (409 `busy`); the phone's pill and
  the agent's own status name the entry and the requester; a press types into
  whatever is focused, so the attacker gains nothing unless they also control
  the focused field; arming and its result are logged. Residual risk: if the
  attacker has the token *and* controls the focused field (e.g. the agent's own
  browser is compromised), the human's press can type a credential there. The
  answer is the same as for any approval prompt: press only when you asked for
  it and the phone or LED names the account you expected.
- **Prompt injection of the agent.** A web page can tell the agent to "log in
  to evil.example with your GitHub account". The agent can arm GitHub; the
  human sees "GitHub" requested while the focused page is evil.example. The
  press is the check, so the human must look. Scoping a token to the accounts
  an agent really needs bounds the damage. (A host check — arm only when the
  focused page's host matches — needs the browser extension, still deferred.)
- **Stolen browser session.** It cannot create a token without a press, and
  every token it could revoke is visible in the log.
- **Token at rest on the client.** The MCP config holds the token in plain
  text; anyone who can read it can arm (not read). That is why tokens are
  revocable, listed with their last use, and logged.
- **Denial of service.** A token holder can keep the slot busy (one item at a
  time). The human cancels with a long press and revokes the token.

## Not done here

- Per-token single-use or expiry (D7 asks "one use"): a token here is reusable
  until revoked; single-use tokens fit the NFC tag work (D11) better.
- Host check against the focused page (D3/M09): needs the browser extension.
- TLS: unchanged honest limit (SPEC §6).
