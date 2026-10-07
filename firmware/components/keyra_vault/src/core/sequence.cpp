#include "keyra/sequence.hpp"

#include <cstring>

#include "keyra/vault.hpp"
#include "text.hpp"

namespace keyra::seq {
namespace {

struct Named {
  const char* name;
  Kind kind;
  size_t typed;  // most characters it can type
};
constexpr Named kTokens[] = {
    {"USERNAME", Kind::Username, vault::kMaxUsername},
    {"PASSWORD", Kind::Password, vault::kMaxPassword},
    {"TOTP", Kind::Totp, 10},
    {"TAB", Kind::Tab, 1},
    {"ENTER", Kind::Enter, 1},
    {"SPACE", Kind::Space, 1},
    {"PRESS", Kind::Press, 0},
};

bool printable(std::string_view s) {
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7F) return false;
  return vault::text::validUtf8(std::string(s));
}

// Code points of valid UTF-8.
size_t chars(std::string_view s) {
  size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

bool typesSomething(Kind k) { return k != Kind::Delay && k != Kind::Press; }

}  // namespace

Error parse(std::string_view src, std::vector<Step>* out) {
  if (src.empty()) return Error::Empty;
  if (src.size() > kMaxSource) return Error::TooLong;
  if (!printable(src)) return Error::BadText;
  std::vector<Step> steps;
  auto addText = [&](std::string_view t) {
    if (!steps.empty() && steps.back().kind == Kind::Text) {
      steps.back().text.append(t);
    } else {
      steps.push_back({Kind::Text, std::string(t), 0});
    }
  };
  size_t presses = 0, typed = 0;
  uint32_t delay = 0;
  Error err = Error::None;
  for (size_t i = 0; i < src.size() && err == Error::None;) {
    const char c = src[i];
    if (c == '}') {
      err = Error::Stray;
      break;
    }
    if (c != '{') {
      const size_t end = src.find_first_of("{}", i);
      const size_t stop = end == std::string_view::npos ? src.size() : end;
      addText(src.substr(i, stop - i));
      typed += chars(src.substr(i, stop - i));
      i = stop;
      continue;
    }
    if (src.substr(i, 3) == "{{}" || src.substr(i, 3) == "{}}") {
      addText(src.substr(i + 1, 1));
      ++typed;
      i += 3;
      continue;
    }
    const size_t close = src.find('}', i + 1);
    if (close == std::string_view::npos) {
      err = Error::Unclosed;
      break;
    }
    const std::string_view body = src.substr(i + 1, close - i - 1);
    i = close + 1;
    if (body.substr(0, 6) == "DELAY ") {
      const std::string_view num = body.substr(6);
      unsigned long ms = 0;
      bool digits = !num.empty() && num.size() <= 4 && num[0] != '0';
      for (char d : num) {
        digits = digits && d >= '0' && d <= '9';
        ms = ms * 10 + static_cast<unsigned long>(d - '0');
      }
      if (!digits || ms < kMinDelayMs || ms > kMaxDelayMs) {
        err = Error::Delay;
        break;
      }
      delay += ms;
      steps.push_back({Kind::Delay, std::string(), static_cast<uint16_t>(ms)});
      continue;
    }
    const Named* found = nullptr;
    for (const Named& t : kTokens)
      if (body == t.name) found = &t;
    if (found == nullptr) {
      err = Error::Unknown;  // includes anything chord-like: {CTRL}, {ALT+F4}, {WIN}, ^v stays literal
      break;
    }
    if (found->kind == Kind::Press) ++presses;
    typed += found->typed;
    steps.push_back({found->kind, std::string(), 0});
  }
  if (err == Error::None) {
    if (steps.size() > kMaxSteps) err = Error::TooManySteps;
    else if (presses > kMaxPresses) err = Error::TooManyPresses;
    else if (delay > kMaxTotalDelayMs) err = Error::TooMuchDelay;
    else if (typed > kMaxTyped) err = Error::TooMuchTyping;
  }
  if (err == Error::None) {
    // Every part between {PRESS} tokens (and the whole) must type something.
    bool typedInPart = false;
    for (const Step& s : steps) {
      if (s.kind == Kind::Press) {
        if (!typedInPart) err = Error::PressPlacement;
        typedInPart = false;
      } else if (typesSomething(s.kind)) {
        typedInPart = true;
      }
    }
    if (!typedInPart) err = presses > 0 ? Error::PressPlacement : Error::Empty;
  }
  if (err != Error::None || out == nullptr) {
    wipe(steps);
    return err;
  }
  wipe(*out);
  *out = std::move(steps);
  return Error::None;
}

const char* message(Error e) {
  switch (e) {
    case Error::None: return "ok";
    case Error::Empty: return "sequence must type something";
    case Error::TooLong: return "sequence must be at most 256 bytes";
    case Error::BadText: return "sequence must be printable UTF-8 (no control characters)";
    case Error::Unclosed: return "sequence has a { without a matching }";
    case Error::Stray: return "a literal } must be written {}}";
    case Error::Unknown:
      return "unknown token: use {USERNAME} {PASSWORD} {TOTP} {TAB} {ENTER} {SPACE} {DELAY ms} {PRESS} {{} {}}";
    case Error::Delay: return "{DELAY n}: n must be 100-3000 (milliseconds)";
    case Error::TooManySteps: return "sequence has more than 32 steps";
    case Error::TooManyPresses: return "sequence has more than 4 {PRESS}";
    case Error::PressPlacement: return "{PRESS} needs something to type before and after it";
    case Error::TooMuchDelay: return "{DELAY} adds up to more than 10 seconds";
    case Error::TooMuchTyping: return "sequence could type more than 1024 characters";
  }
  return "invalid sequence";
}

std::string preview(const std::vector<Step>& steps) {
  std::string out;
  for (const Step& s : steps) {
    switch (s.kind) {
      case Kind::Text:
        for (size_t i = 0, n = chars(s.text); i < n; ++i) out += "\xE2\x80\xA2";  // •
        break;
      case Kind::Username: out += "{USERNAME}"; break;
      case Kind::Password: out += "{PASSWORD}"; break;
      case Kind::Totp: out += "{TOTP}"; break;
      case Kind::Tab: out += "{TAB}"; break;
      case Kind::Enter: out += "{ENTER}"; break;
      case Kind::Space: out += "{SPACE}"; break;
      case Kind::Press: out += "{PRESS}"; break;
      case Kind::Delay: out += "{DELAY " + std::to_string(s.ms) + "}"; break;
    }
  }
  return out;
}

void wipe(std::vector<Step>& steps) {
  for (Step& s : steps) vault::wipe(s.text);
  steps.clear();
}

}  // namespace keyra::seq
