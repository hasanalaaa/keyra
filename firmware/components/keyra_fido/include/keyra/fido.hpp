#pragma once
// Keyra as a USB FIDO2/U2F security key (docs/FIDO.md). The FIDO HID
// interface itself is part of keyra_hid's USB device; this component runs the
// CTAP protocol in its own task and stores passkeys in the vault.
#include <cstdint>
#include <string>
#include <vector>

namespace keyra::fido {

// After hid::init(): starts the FIDO task. Returns false if it could not.
bool start();

// Button routing (keyra_api owns the button): while a FIDO request waits the
// LED shows the FIDO pattern and presses go here (short = approve, long = refuse).
bool awaitingTouch();
void press(bool shortPress);
bool ledActive();  // awaitingTouch(), or a host WINK in the last 1.5 s

// Factory reset: drops this Keyra's U2F attestation key and certificate; a new
// pair is made on the next U2F registration. The signature counter stays.
bool forgetAttestation();

// Passkeys in backups (docs/research/PASSKEY-BACKUP.md): a backup stores the
// signature counter, and after restoring one the counter is raised to at least
// the backup's + 1000 (never lowered).
bool signatureCounter(uint32_t& out);
bool raiseCounterAfterRestore(uint32_t backupCounter);

// Discoverable credentials for Settings → Passkeys (vault must be unlocked).
struct Passkey {
  uint32_t id = 0;  // vault record id
  std::string rpId, userName, displayName;
  int64_t created = 0;  // unix seconds, 0 = unknown
};
enum class Result { Ok, Locked, NotFound, Error };
constexpr size_t kMaxPasskeys = 50;
Result list(std::vector<Passkey>& out);  // newest first
Result remove(uint32_t id);

}  // namespace keyra::fido
