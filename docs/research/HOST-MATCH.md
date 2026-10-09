# Host matching (shared rule)

Which stored login is offered on which page. One rule, implemented three times —
the device (`keyra_api`, `POST /api/agent/match` and the `host` check on
`POST /api/agent/type`), the browser extension (`extension/src/host.ts`) and the
Android app (`android/.../match/HostMatcher.kt`) — and each implementation's
tests run the table below. Contract: SPEC §9.4.

## Why this rule

Choosing an offered login and pressing Keyra's button types it into whatever has
focus. An offer on the wrong site is therefore a phishing aid, so the rule only
says yes when ownership is obvious without a public-suffix list:

- the same host, or
- one host is a subdomain of the other (the owner of `github.com` owns
  `gist.github.com`).

Siblings (`mail.google.com` vs `accounts.google.com`) and shared hosting
(`alice.github.io` vs `mallory.github.io`) are not matched; the user picks
"Other login…" once (the extension warns when the hosts differ; the Android app
can remember the choice).

## Normalising

Lowercase, trim surrounding spaces and one trailing `.`, then drop one leading
`www.`. Empty → no match.

## Matching

1. Equal after normalising → match.
2. Either is an IP address (IPv4 dotted quad, or contains `:`) → no match.
3. Let *short* be the shorter and *long* the longer. Match when *long* ends with
   `.` + *short* and *short* is site-like: at least two labels, and not a
   two-label country second level — a two-letter last label under one of
   `ac co com edu gob gov go mil ne net or org sch` (`co.uk`, `gov.iq`,
   `com.au`).

## Cases

| Login host | Page host | Match |
|---|---|---|
| `github.com` | `WWW.GitHub.com.` | yes |
| `github.com` | `gist.github.com` | yes |
| `accounts.google.com` | `google.com` | yes |
| `rafidain-bank.gov.iq` | `online.rafidain-bank.gov.iq` | yes |
| `192.168.1.1` | `192.168.1.1` | yes |
| `github.com` | `github.com.evil.example` | no |
| `github.com` | `evilgithub.com` | no |
| `github.com` | `gitlab.com` | no |
| `alice.github.io` | `mallory.github.io` | no |
| `alice.some-new-host.dev` | `mallory.some-new-host.dev` | no |
| `mail.google.com` | `accounts.google.com` | no |
| `bbc.co.uk` | `evil.co.uk` | no |
| `co.uk` | `evil.co.uk` | no |
| `com` | `github.com` | no |
| `192.168.1.1` | `192.168.1.2` | no |
| `1.1` | `192.168.1.1` | no |
| *(empty)* | `github.com` | no |
