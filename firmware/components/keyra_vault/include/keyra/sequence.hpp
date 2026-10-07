// Auto-type sequences (SPEC §10.4): a strict whitelist grammar, so a sequence
// that arrives in an import or a restored backup can never make Keyra press a
// shortcut. Shared by the vault (every entry write, import and restore), the
// API and the typing task.
//
//   sequence := ( literal | token )+
//   token    := {USERNAME} | {PASSWORD} | {TOTP} | {TAB} | {ENTER} | {SPACE}
//             | {DELAY n}  (n = 100..3000 ms) | {PRESS}  (wait for another button press)
//             | {{} | {}}  (a literal brace)
//   literal  := printable characters other than { and }
//
// There is no token for Ctrl, Alt, Cmd, Win, arrows or function keys, and
// characters such as + ^ % ~ are typed as themselves. Limits: 256 bytes of
// source, 32 steps, 4 {PRESS}, 10 s of {DELAY} in total, and at most 1024
// characters typed even with the longest field values. A {PRESS} needs
// something to type on both sides of it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keyra::seq {

enum class Kind : uint8_t { Text, Username, Password, Totp, Tab, Enter, Space, Delay, Press };

struct Step {
  Kind kind = Kind::Text;
  std::string text;  // Kind::Text
  uint16_t ms = 0;   // Kind::Delay
};

inline constexpr size_t kMaxSource = 256, kMaxSteps = 32, kMaxPresses = 4, kMaxTyped = 1024;
inline constexpr uint16_t kMinDelayMs = 100, kMaxDelayMs = 3000;
inline constexpr uint32_t kMaxTotalDelayMs = 10000;

enum class Error { None, Empty, TooLong, BadText, Unclosed, Stray, Unknown, Delay, TooManySteps, TooManyPresses,
                   PressPlacement, TooMuchDelay, TooMuchTyping };

// Parses `src` into steps (adjacent literal text merged). `out` may be null to
// only validate. An empty source is Error::Empty.
Error parse(std::string_view src, std::vector<Step>* out);
// What a stored entry may hold: empty (no custom sequence) or a valid one.
inline bool valid(std::string_view src) { return src.empty() || parse(src, nullptr) == Error::None; }
const char* message(Error e);  // English, for a 400 response

// For the pending card on the phone: the tokens as written, literal text
// masked as one '•' per character (it may hold a secret).
std::string preview(const std::vector<Step>& steps);

// Overwrites literal text before the steps are dropped.
void wipe(std::vector<Step>& steps);

}  // namespace keyra::seq
