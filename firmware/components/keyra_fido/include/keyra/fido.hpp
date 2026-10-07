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
