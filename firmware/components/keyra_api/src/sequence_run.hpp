#pragma once
// Runs an auto-type sequence (SPEC §10.4) over an abstract keyboard, so the
// rules are host-tested: nothing is typed unless every character of the part
// can be, and a failure stops the part at once.
#include <cstdint>
#include <string>
#include <string_view>

#include "actions.hpp"

namespace keyra::api::seqrun {

// Field values, read from the vault for each part (wiped by the owner).
struct Fields {
  std::string username, password, totp;
};

class Keys {
 public:
  virtual ~Keys() = default;
  virtual bool typeable(const std::string& text) = 0;  // on the target's layout
  virtual actions::Code text(const std::string& text) = 0;
  virtual actions::Code key(uint8_t hidKeycode) = 0;  // Tab, Enter or Space only
  virtual void delayMs(uint32_t ms) = 0;
};

// Parses `src` into `out` (steps, parts, masked preview).
seq::Error build(std::string_view src, actions::SeqJob& out);
// What "Both" means without a custom sequence: username, Tab or Enter,
// password, and Enter when "submit after Both" is on.
std::string builtIn(bool enterBetween, bool submit);

struct Needs {
  bool username = false, password = false, totp = false;
};
Needs needs(const actions::SeqJob& job);

// Types part `part` (0-based; parts are separated by {PRESS}). Before the
// first key it checks the whole part: UnsupportedChar (a character the
// layout cannot type) or Failed (a field it names is empty) mean nothing was typed.
actions::Code runPart(const actions::SeqJob& job, uint8_t part, const Fields& f, Keys& keys);
// The same check over every part, done at the first press so a sequence
// never stops halfway for a reason known up front.
actions::Code checkAll(const actions::SeqJob& job, const Fields& f, Keys& keys);

}  // namespace keyra::api::seqrun
