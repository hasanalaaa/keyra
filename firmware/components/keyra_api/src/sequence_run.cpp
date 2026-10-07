#include "sequence_run.hpp"

namespace keyra::api::seqrun {
namespace {

using actions::Code;
using seq::Kind;

constexpr uint8_t kKeyEnter = 0x28, kKeyTab = 0x2B, kKeySpace = 0x2C;

const std::string* fieldText(const seq::Step& s, const Fields& f) {
  switch (s.kind) {
    case Kind::Text: return &s.text;
    case Kind::Username: return &f.username;
    case Kind::Password: return &f.password;
    case Kind::Totp: return &f.totp;
    default: return nullptr;
  }
}

// Calls fn(step) for the steps of one part.
template <typename Fn>
void forPart(const actions::SeqJob& job, uint8_t part, Fn fn) {
  uint8_t at = 0;
  for (const seq::Step& s : job.steps) {
    if (s.kind == Kind::Press) {
      if (++at > part) return;
      continue;
    }
    if (at == part && !fn(s)) return;
  }
}

// Typed when the part can be typed; Failed when a field it names is empty
// (the entry changed since it was armed); UnsupportedChar otherwise.
Code checkPart(const actions::SeqJob& job, uint8_t part, const Fields& f, Keys& keys) {
  Code c = Code::Typed;
  forPart(job, part, [&](const seq::Step& s) {
    if (const std::string* t = fieldText(s, f)) {
      if (t->empty()) c = Code::Failed;
      else if (!keys.typeable(*t)) c = Code::UnsupportedChar;
    }
    return c == Code::Typed;
  });
  return c;
}

}  // namespace

seq::Error build(std::string_view src, actions::SeqJob& out) {
  const seq::Error e = seq::parse(src, &out.steps);
  if (e != seq::Error::None) return e;
  out.parts = 1;
  for (const seq::Step& s : out.steps) out.parts += s.kind == Kind::Press;
  out.preview = seq::preview(out.steps);
  return e;
}

std::string builtIn(bool enterBetween, bool submit) {
  std::string s = "{USERNAME}";
  s += enterBetween ? "{ENTER}" : "{TAB}";
  s += "{PASSWORD}";
  if (submit) s += "{ENTER}";
  return s;
}

Needs needs(const actions::SeqJob& job) {
  Needs n;
  for (const seq::Step& s : job.steps) {
    n.username |= s.kind == Kind::Username;
    n.password |= s.kind == Kind::Password;
    n.totp |= s.kind == Kind::Totp;
  }
  return n;
}

Code checkAll(const actions::SeqJob& job, const Fields& f, Keys& keys) {
  for (uint8_t p = 0; p < job.parts; ++p)
    if (const Code c = checkPart(job, p, f, keys); c != Code::Typed) return c;
  return Code::Typed;
}

Code runPart(const actions::SeqJob& job, uint8_t part, const Fields& f, Keys& keys) {
  if (part >= job.parts) return Code::Failed;
  Code c = checkPart(job, part, f, keys);
  if (c != Code::Typed) return c;
  forPart(job, part, [&](const seq::Step& s) {
    switch (s.kind) {
      case Kind::Tab: c = keys.key(kKeyTab); break;
      case Kind::Enter: c = keys.key(kKeyEnter); break;
      case Kind::Space: c = keys.key(kKeySpace); break;
      case Kind::Delay: keys.delayMs(s.ms); break;
      case Kind::Press: break;
      default: c = keys.text(*fieldText(s, f)); break;
    }
    return c == Code::Typed;
  });
  return c;
}

}  // namespace keyra::api::seqrun
