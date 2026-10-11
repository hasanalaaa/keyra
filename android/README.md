# Keyra for Android

An Android companion that asks Keyra to **type** a login after you press Keyra's
button. It never receives, shows or stores a stored password, username or 2FA
secret: it only uses the access-token API (SPEC §17, design note
[docs/research/TOKENS.md](../docs/research/TOKENS.md)), which has no endpoint
that returns one.

- **Logins**: the titles and hosts your token may use, with search. Tap one →
  *Type username / password / both / 2FA code* → Keyra waits up to 60 s for the
  press, the app counts down, polls the result (`typed`, `cancelled`,
  `expired`, `failed`) and can cancel.
- **Generate password**: Keyra's hardware generator (`/api/agent/generate`).
  The password is shown on a screen that blocks screenshots, and copying marks
  the clip sensitive and clears it after 60 s. It is not stored anywhere.
- **Autofill (Mode A, button-gated typing)**: on a login form the suggestions
  are "Keyra · *title*" for logins whose host is the page's host or a
  subdomain either way (`github.com` ↔ `gist.github.com`; page domains are
  trusted only from known browsers, so an app cannot pose as a site), or, in
  an app, logins saved from that very app (`androidapp://<package>`), plus
  "Keyra · Choose a login…". A suggestion holds **no value**: choosing it asks
  Keyra to arm (`both` from the username field, `password` from the password
  field, `username` on a username-only step), you return to the field and
  press Keyra's button, and Keyra types as a Bluetooth/USB keyboard. Choices
  made with "Choose a login…" can be remembered for that app or site; the
  picker shows the app's package next to its name, since any app can call
  itself "GitHub". Package names are never used to guess a login, and fields
  of a frame from another domain inside the page are ignored.
- **Save**: after a sign-in or sign-up Android offers "Save to Keyra"; the app
  shows what will be saved and sends it once to `/api/agent/save`; Keyra stores
  it only when you press its button. The values live in that screen's memory
  only.
- English and Arabic (right-to-left), light and dark (follows the system).
- No accessibility service, no Google Play Services, no runtime libraries
  besides the Kotlin standard library (framework Views). The unsigned release APK is about 138 KB (138,376 bytes at 0.3.0).

## Build

Requirements: JDK 17+ (Android Studio's bundled JBR works), Android SDK with
platform 36 and build-tools 35. Node.js only for the API tests.

```sh
cd android
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"   # macOS example
echo "sdk.dir=$HOME/Library/Android/sdk" > local.properties                        # or set ANDROID_HOME
./gradlew testDebugUnitTest assembleDebug lint
```

The debug APK is `app/build/outputs/apk/debug/app-debug.apk`.

Tests:

- JVM unit tests: host ↔ domain/package matching, address validation, form
  field detection, token storage (sealed with AES-GCM), and the API client
  against the device mock `web/mock/server.mjs` (started on a free port by the
  test; unlock, token creation and button presses are driven through the
  mock). Set `KEYRA_MOCK=/path/to/server.mjs` to use another copy; if the mock
  is missing, or predates the token API, those tests are skipped and say why.
- On a device or emulator, against a running mock:
  ```sh
  PORT=8787 node ../web/mock/server.mjs &
  # create an app token (web app at http://localhost:8787 → Settings → Apps and agents,
  # or POST /api/tokens twice around POST /__mock/button {"press":"short"})
  ./gradlew connectedDebugAndroidTest \
    -Pandroid.testInstrumentationRunnerArguments.keyraUrl=http://10.0.2.2:8787 \
    -Pandroid.testInstrumentationRunnerArguments.keyraToken=keyra_…
  ```
  The test APK also contains `LoginFormActivity` (package
  `app.keyra.android.test`), a plain native login form for trying autofill and
  save by hand (`adb shell am start -n app.keyra.android.test/app.keyra.android.LoginFormActivity`).

## Install and set up

1. `adb install -r app/build/outputs/apk/debug/app-debug.apk` (or copy the APK
   to the phone and open it; allow installing from that source).
2. In Keyra's web app: **Settings → Apps and agents → New**, kind **App**,
   choose all logins or only some, press Keyra's button, then copy the token
   (`keyra_…`, shown once). To move it to the phone you can scan its QR code
   with the camera app and copy the text, or share the text to "Keyra".
3. In the app: enter Keyra's address (`http://keyra.local`, or the IP such as
   `http://192.168.4.1` on Keyra's own Wi-Fi or its home-network IP), paste the
   token, **Test connection**, **Save**. Keyra must be unlocked; a locked Keyra
   answers "locked".
4. Autofill: **Use Keyra for autofill** in the app's settings, or Android
   **Settings → Passwords & accounts** (on some phones *Passwords, passkeys &
   autofill* / *Autofill service*) → choose Keyra. In Chrome also pick
   **Settings → Autofill services → Autofill using another service** (Chrome
   135+; Chrome restarts).

Revoke the token in Keyra's web app at any time; the app then gets "Keyra does
not accept this token" and can be given a new one.

## Privacy: what the app stores

Only three things, in its private app storage, never backed up or transferred
(`allowBackup=false`, data-extraction rules exclude everything):

| What | How |
|---|---|
| Keyra's address | plain text |
| The access token | AES-256-GCM with a non-exportable key in the Android Keystore (`keyra-token`) |
| Remembered app/site → login choices | `web:<domain>` or `app:<package>` → login id and title |

No password, username, 2FA secret or generated password is written anywhere.
**Disconnect from Keyra** in settings removes all three. The token's power is
limited by design: it can list titles and hosts in its scope and *arm* typing
or a save, every one of which still needs your press on Keyra.

## Network

Keyra speaks plain HTTP on the local network (SPEC §6), so cleartext is needed.
Android's `network_security_config` cannot express IP ranges, so it permits
cleartext and the app itself refuses `http://` to anything but `localhost`,
`*.local`, `*.home.arpa`, private and link-local IPv4 (`10/8`, `172.16/12`,
`192.168/16`, `169.254/16`) and link-local/unique-local IPv6, and connects only
to the configured address. On Keyra's own access point (no internet) the app
sends its requests over the Wi-Fi network rather than mobile data.
Limitations: the token crosses the LAN in clear text (anyone on the same
network can capture it and arm, never read — see TOKENS.md); `keyra.local`
needs mDNS resolution, which some Android versions lack — use the IP then.

## Known limits

- The app does not require the screen lock to use the token (no Keystore
  user-authentication binding); anyone using the unlocked phone can arm, and
  Keyra's press is still required.
- Matching is deliberately simple (the shared rule in
  [docs/research/HOST-MATCH.md](../docs/research/HOST-MATCH.md)): no
  public-suffix list and no list of shared-hosting domains. In a browser a
  login is offered when its host equals the page's host or one is a subdomain of
  the other (`github.com` ↔ `gist.github.com`), never between siblings
  (`mail.google.com` ↔ `accounts.google.com`). In an app, the only logins
  offered are the ones saved from that very app (`androidapp://<package>`, kept
  by Keyra as the login's address); package names are never used to guess a
  site. Everything else, such as logins made on the web or for apps whose
  package says nothing about the site (e.g. X/Twitter), is reached once through
  "Choose a login…", and the choice can be remembered.
- Logins saved from an app get the title of the app's package name when Android
  hides other apps' names from Keyra (package visibility); edit the title on
  the save screen.
- Save covers forms where the password field is on the screen being submitted;
  a username from an earlier screen of the same flow is used when Android
  passes it along.
- On the Android 16 emulator the "Save to Keyra?" confirmation sometimes did
  not open after tapping Save in Android's prompt (the system delivered the
  save request while Keyra's app process was frozen, and the form's app had
  already closed). It opened in the other runs. Not yet checked on a phone.
- Typing goes to whatever field is focused on the device Keyra types into.
  Mode B (returning a credential to the form) is deliberately not implemented.

## Release signing

Release builds are minified and **unsigned**; no key is in the repository and
none should be. To make your own:

```sh
keytool -genkeypair -v -keystore ~/keyra-release.jks -alias keyra \
  -keyalg RSA -keysize 4096 -validity 10000
./gradlew assembleRelease
$ANDROID_HOME/build-tools/35.0.0/zipalign -p -f 4 \
  app/build/outputs/apk/release/app-release-unsigned.apk keyra-aligned.apk
$ANDROID_HOME/build-tools/35.0.0/apksigner sign --ks ~/keyra-release.jks \
  --out keyra-release.apk keyra-aligned.apk
apksigner verify --print-certs keyra-release.apk
```

Keep the keystore and its passwords outside the repository (`*.jks`,
`*.keystore` and `keystore.properties` are ignored). Losing it means users must
uninstall to update.
