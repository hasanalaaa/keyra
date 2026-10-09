# keyra-mcp

A [Model Context Protocol](https://modelcontextprotocol.io) server that lets an
AI agent (Claude Desktop, Claude Code, any MCP client) log in with an account
stored on Keyra **without ever seeing it**. The agent asks; Keyra's LED pulses
and the phone shows "requested by …"; you press Keyra's button; Keyra types the
login into the focused field as a keyboard. The tool only ever answers
`typed`, `denied` or `expired`.

One file, no dependencies, Node.js 20 or newer. Contract: SPEC §17; design
and threat model: [docs/research/TOKENS.md](../../docs/research/TOKENS.md).

## Setup

1. Keyra must be reachable from the computer running the agent: the computer
   is on Keyra's Wi‑Fi, or Keyra has joined your home network (Settings →
   Home Wi‑Fi).
2. In the Keyra web app: **Settings → Apps and agents → New access token**.
   Choose **AI agent**, and preferably **Only these** with just the accounts
   the agent needs. Press Keyra's button, then copy the token (it is shown
   once).
3. Configure your client with `KEYRA_URL` and `KEYRA_TOKEN`.

**Claude Code**

```sh
claude mcp add keyra \
  -e KEYRA_URL=http://keyra.local \
  -e KEYRA_TOKEN=keyra_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \
  -- node /path/to/Keyra/tools/keyra-mcp/server.mjs
```

**Claude Desktop** (`claude_desktop_config.json`)

```json
{
  "mcpServers": {
    "keyra": {
      "command": "node",
      "args": ["/path/to/Keyra/tools/keyra-mcp/server.mjs"],
      "env": {
        "KEYRA_URL": "http://keyra.local",
        "KEYRA_TOKEN": "keyra_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
      }
    }
  }
}
```

If `keyra.local` does not resolve on your network, use Keyra's IP address
(shown in Settings → Home Wi‑Fi), e.g. `http://192.168.1.42`.

## Tools

| Tool | Arguments | Returns |
|---|---|---|
| `keyra_list_logins` | `query?` | One line per login in the token's scope: `id: title (host)`. No usernames, no passwords. |
| `keyra_type_login` | `id`, `what?` (`username` \| `password` \| `both` \| `totp`, default `both`), `submit?`, `target?` (`usb` or a Bluetooth address) | `typed`, `denied` (long press, or Keyra could not type, with the reason) or `expired` (no press within 60 s). |

Before `keyra_type_login`, the agent must put the keyboard focus in the login
field: Keyra types wherever the cursor is.

Errors come back as tool errors with a plain explanation: Keyra locked (unlock
it in the web app), token unknown or revoked, Keyra busy with another request,
or rate limited (10 requests per 10 s per token).

`KEYRA_POLL_MS` (default 2000) sets how often `keyra_type_login` checks for
the press.

## Security notes

- The token can only list titles and hosts in its scope and *arm* typing.
  Every typing needs your press; nothing a token can call returns a secret.
- Keyra speaks plain HTTP. On a shared network the token can be sniffed; the
  thief then gets the same arm-only power, which still needs your press.
  Revoke a token in Settings → Apps and agents the moment you doubt it.
- Press only when you asked the agent to log in and the phone or the agent
  names the account you expect: a press types into whatever is focused.
- Every list, request and token change is in Settings → Activity.

## Test

```sh
node --test tools/keyra-mcp/server.test.mjs   # runs it against web/mock/server.mjs
```
