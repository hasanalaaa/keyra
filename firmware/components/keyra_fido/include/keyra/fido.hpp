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
// `pressedAt`: when keyra_io recognised the press (esp_timer ms); a press made
// before the waiting request began answers nothing.
bool awaitingTouch();
void press(bool shortPress, int64_t pressedAt);
bool ledActive();  // awaitingTouch(), or a host WINK in the last 1.5 s

// Factory reset: drops this Keyra's U2F attestation key and certificate; a new
// pair is made on the next U2F registration. The signature counter stays.
bool forgetAttestation();

// The signature counter (docs/FIDO.md "Counter"): NVS plus a reservation in
// the vault, so neither an erased NVS partition nor a restore lets it go back
// (the restore raises the reservation itself, keyra/vault.hpp). Vault unlocked.
// signatureCounter: the last value issued, for backups. secureCounter: run at
// every unlock, makes sure the vault holds a reservation (a Keyra updated from
// a version without one gets it here, before NVS could be lost).
bool signatureCounter(uint32_t& out);
bool secureCounter();

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

// The security key PIN (set and changed from the computer's own dialogs;
// docs/FIDO.md "ClientPIN"). Vault must be unlocked.
struct PinStatus {
  bool set = false;
  uint8_t retries = 8;  // wrong PINs left before it blocks (8 when none is set)
};
Result pinStatus(PinStatus& out);

}  // namespace keyra::fido
