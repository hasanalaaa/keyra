# Fixtures: data written by firmware 3ce814a

Devices updated over the air to 0.3.0 run 3ce814a ("fix(ble): a bonded host may
renew its keys…") before the update. These files are what that firmware leaves on
the flash. `firmware/test/upgrade/upgrade_test.cpp` (ctest `keyra_upgrade_from_3ce814a`,
built by `firmware/test/host`) opens them with the current code and checks every field.

All secrets here are test values (passphrase and backup passphrase are in
`firmware/test/upgrade/common.hpp`). No real account is in them.

## Contents

| Path | Written by | What it is |
|---|---|---|
| `device/vault/…` | 3ce814a | Every file of the `vault` LittleFS partition, byte for byte: `meta.bin`, `e/<id>.bin` (6 accounts), `f/<id>.bin` (2 discoverable passkeys), `fido.bin` (wrap-key salt), `activity.bin` (18 events, kinds 1–18) |
| `device/nvs.txt` | 3ce814a | The NVS values kept beside it: the vault's unlock-failure counter and keyra_fido's signature counter |
| `passkeys.txt` | 3ce814a | The 3 credentials made (2 discoverable, 1 not): rp, user, discoverable, credential ID, public key — the test checks signatures against these keys |
| `recovery.txt` | 3ce814a | The recovery key created on that vault |
| `backup.json` | 3ce814a | An encrypted backup with passkeys, exported by that version |
| `after-rollback/…` | 0.3.0, then 3ce814a | `device/` opened by this tree, which added an account, activity events of kinds 19–28, a passkey with hmac-secret and credProtect 2, a FIDO PIN, `tokens.bin` and `tags.bin`; then 3ce814a booted it again (an update rolled back), used every passkey, added an account and a Lock event |

The accounts cover 2FA (otpauth URI and plain base32), Arabic and emoji in every
text field, every field at its byte limit, a full password history (11 changes,
10 kept), an auto-type sequence, burn-after-typing, favorite, last-used times and
a nearly empty entry. The exact values are `expectedEntries()` in `common.hpp`.

## Regenerating

```sh
firmware/test/upgrade/regen.sh
```

It checks out 3ce814a in a temporary git worktree, builds
`firmware/test/upgrade/gen_old.cpp` against those sources
(`firmware/test/upgrade/old/CMakeLists.txt`), runs `gen_old v020`, then
`keyra_upgrade_test write030` (this tree) and `gen_old rollback`, copies the
result here and runs the test. Keys, IVs and ids are random, so every run gives
different bytes with the same content. Regenerate only if the test itself
changes; the point of these files is that they were written by the old firmware.
