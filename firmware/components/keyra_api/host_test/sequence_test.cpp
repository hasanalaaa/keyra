// Auto-type sequences (SPEC §10.4) end to end on the host: parsed with the
// vault's grammar, run through the real typing engine over a fake keyboard
// link, and armed again by the actions machine at every {PRESS}.
#include <memory>
#include <string>
#include <vector>

#include "actions.hpp"
#include "keymap.hpp"
#include "keyra_test.hpp"
#include "sequence_run.hpp"
#include "typer.hpp"

using namespace keyra;
using actions::Code;

namespace {

struct Report {
  uint8_t mod, key;
};

class FakeLink : public hid::Transport {
 public:
  std::vector<Report> sent;
  int failAt = -1;  // index of the one send that fails (a glitch); -1 = never
  int attempts = 0;
  bool ready() override { return true; }
  bool capsLock() override { return false; }
  void delayMs(uint32_t) override {}
  bool send(uint8_t mod, uint8_t key) override {
    if (attempts++ == failAt) return false;
    sent.push_back({mod, key});
    return true;
  }
  bool released() const { return !sent.empty() && sent.back().mod == 0 && sent.back().key == 0; }
  size_t presses() const {
    size_t n = 0;
    for (const Report& r : sent) n += r.key != 0;
    return n;
  }
  bool any(uint8_t key) const {
    for (const Report& r : sent)
      if (r.key == key) return true;
    return false;
  }
  bool onlyShiftOrAltGr() const {
    for (const Report& r : sent)
      if (r.mod & ~(hid::MOD_LEFT_SHIFT | hid::MOD_RIGHT_ALT)) return false;
    return true;
  }
};

Code fromHid(hid::Result r) {
  switch (r) {
    case hid::Result::Ok: return Code::Typed;
    case hid::Result::Unsupported: return Code::UnsupportedChar;
    case hid::Result::NotMounted: return Code::NoUsb;
    default: return Code::Failed;
  }
}

class EngineKeys : public api::seqrun::Keys {
 public:
  FakeLink link;
  hid::Typer typer;
  hid::Options opt;
  uint32_t waited = 0;
  bool typeable(const std::string& t) override { return hid::typeable(t.c_str(), opt.layout); }
  Code text(const std::string& t) override { return fromHid(typer.type(link, t.c_str(), opt)); }
  Code key(uint8_t k) override { return fromHid(typer.tap(link, k, opt)); }
  void delayMs(uint32_t ms) override { waited += ms; }
};

std::shared_ptr<actions::SeqJob> job(const char* src) {
  auto j = std::make_shared<actions::SeqJob>();
  CHECK(api::seqrun::build(src, *j) == seq::Error::None);
  return j;
}

api::seqrun::Fields fields() { return {"ann", "pw!", "123456"}; }

void typesAPart() {
  auto j = job("{USERNAME}{TAB}{PASSWORD}{DELAY 500}{ENTER}");
  EngineKeys k;
  CHECK(api::seqrun::runPart(*j, 0, fields(), k) == Code::Typed);
  // a n n, Tab, p w Shift+1, Enter: every key released after it.
  CHECK_EQ(k.link.presses(), size_t{8});
  CHECK(k.link.any(0x2B) && k.link.any(0x28));
  CHECK(k.link.released() && k.link.onlyShiftOrAltGr());
  CHECK_EQ(k.waited, 500u);
  CHECK(j->preview == "{USERNAME}{TAB}{PASSWORD}{DELAY 500}{ENTER}");
}

void literalTextIsMaskedAndTyped() {
  auto j = job("{USERNAME}@corp{TAB}{TOTP}");
  CHECK(j->preview == "{USERNAME}\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2{TAB}{TOTP}");
  EngineKeys k;
  CHECK(api::seqrun::runPart(*j, 0, fields(), k) == Code::Typed);
  CHECK_EQ(k.link.presses(), size_t{3 + 5 + 1 + 6});
  auto n = api::seqrun::needs(*j);
  CHECK(n.username && !n.password && n.totp);
}

void unsupportedTypesNothing() {
  auto j = job("{USERNAME}{TAB}{PASSWORD}");
  EngineKeys k;
  CHECK(hid::findLayout("ar", k.opt.layout));  // no Latin letters on an Arabic host
  CHECK(api::seqrun::checkAll(*j, fields(), k) == Code::UnsupportedChar);
  CHECK(api::seqrun::runPart(*j, 0, fields(), k) == Code::UnsupportedChar);
  CHECK(k.link.sent.empty());
  // A password in a later part is checked at the first press too.
  auto j2 = job("123{PRESS}{PASSWORD}");
  CHECK(api::seqrun::checkAll(*j2, fields(), k) == Code::UnsupportedChar);
  api::seqrun::Fields empty = fields();
  empty.password.clear();  // the entry lost its password since it was armed
  k.opt.layout = hid::kLayoutUs;
  CHECK(api::seqrun::checkAll(*j2, empty, k) == Code::Failed);
}

void failureStopsAndReleases() {
  // A glitch at any key press: the part stops there (nothing after it is
  // typed, so no Enter submits a half-typed form) and every key is released.
  auto j = job("{USERNAME}{TAB}{PASSWORD}{ENTER}");
  EngineKeys whole;
  CHECK(api::seqrun::runPart(*j, 0, fields(), whole) == Code::Typed);
  for (size_t at = 0; at < whole.link.sent.size(); ++at) {
    if (whole.link.sent[at].key == 0) continue;  // glitches on key-downs (a failed release is retried)
    EngineKeys k;
    k.link.failAt = static_cast<int>(at);
    CHECK(api::seqrun::runPart(*j, 0, fields(), k) == Code::Failed);
    CHECK(k.link.released());
    CHECK(!k.link.any(0x28));  // Enter is the last key: a glitch anywhere means no submit
  }
}

void partsSplitAtPress() {
  auto j = job("{USERNAME}{ENTER}{PRESS}{PASSWORD}{ENTER}");
  CHECK_EQ(j->parts, 2);
  EngineKeys a, b;
  CHECK(api::seqrun::runPart(*j, 0, fields(), a) == Code::Typed);
  CHECK_EQ(a.link.presses(), size_t{4});  // a n n Enter
  CHECK(api::seqrun::runPart(*j, 1, fields(), b) == Code::Typed);
  CHECK_EQ(b.link.presses(), size_t{4});  // p w ! Enter
  EngineKeys c;
  CHECK(api::seqrun::runPart(*j, 2, fields(), c) == Code::Failed);
  CHECK(c.link.sent.empty());
}

void builtInMatchesBoth() {
  CHECK(api::seqrun::builtIn(false, false) == "{USERNAME}{TAB}{PASSWORD}");
  CHECK(api::seqrun::builtIn(true, true) == "{USERNAME}{ENTER}{PASSWORD}{ENTER}");
}

// The actions machine: one press per part, a fresh 60 s for each, and the
// result only at the end.
int64_t g_now = 1000;

void machineWaitsForEachPress() {
  actions::Machine m([] { return g_now; });
  actions::TypeRequest r{9, "Bank", actions::What::Sequence, false, {}, nullptr, job("a{PRESS}b{PRESS}c"), 0};
  m.arm(r);
  auto p = m.pending();
  CHECK(p && p->req.part == 0 && p->req.seq->parts == 3);
  for (uint8_t part = 0; part < 3; ++part) {
    actions::Decision d = m.onButton(actions::Button::Short, true);
    CHECK(d.effect == actions::Effect::Run && d.run.part == part);
    CHECK(!m.pending().has_value());
    g_now += 5000;
    m.typingFinished(d.run, Code::Typed);
    p = m.pending();
    if (part < 2) {
      CHECK(p && p->req.part == part + 1 && p->expiresInMs == actions::kExpiryMs);
      CHECK(!m.last().has_value());
      CHECK(m.indicator(true, true, false) == actions::Indicator::Pending);
    } else {
      CHECK(!p.has_value());
      CHECK(m.last() && m.last()->code == Code::Typed);
    }
  }
}

void waitingPartExpiresOrCancels() {
  actions::Machine m([] { return g_now; });
  actions::TypeRequest r{9, "Bank", actions::What::Sequence, false, {}, nullptr, job("a{PRESS}b"), 0};
  m.arm(r);
  actions::Decision d = m.onButton(actions::Button::Short, true);
  m.typingFinished(d.run, Code::Typed);
  g_now += actions::kExpiryMs;
  CHECK(!m.pending().has_value());
  CHECK(m.last()->code == Code::Expired);

  m.arm(r);
  d = m.onButton(actions::Button::Short, true);
  m.typingFinished(d.run, Code::Typed);
  CHECK(m.onButton(actions::Button::Long, true).effect == actions::Effect::Cancelled);
  CHECK(m.last()->code == Code::Cancelled);

  // A failed part ends the sequence.
  m.arm(r);
  d = m.onButton(actions::Button::Short, true);
  m.typingFinished(d.run, Code::UnsupportedChar);
  CHECK(!m.pending().has_value());
  CHECK(m.last()->code == Code::UnsupportedChar);

  // Something else armed while a part typed: the sequence does not come back.
  m.arm(r);
  d = m.onButton(actions::Button::Short, true);
  actions::TypeRequest other{1, "Mail", actions::What::Password, false, {}, nullptr, nullptr, 0};
  m.arm(other);
  m.typingFinished(d.run, Code::Typed);
  CHECK(m.pending() && m.pending()->req.id == 1);
  CHECK(m.last()->code == Code::Cancelled);

  // Lock drops a waiting sequence like any armed action.
  m.cancel();
  m.arm(r);
  d = m.onButton(actions::Button::Short, true);
  m.typingFinished(d.run, Code::Typed);
  m.dropSessionItems();
  CHECK(!m.pending().has_value());
}

}  // namespace

int main() {
  typesAPart();
  literalTextIsMaskedAndTyped();
  unsupportedTypesNothing();
  failureStopsAndReleases();
  partsSplitAtPress();
  builtInMatchesBoth();
  machineWaitsForEachPress();
  waitingPartExpiresOrCancels();
  return KEYRA_TEST_RESULT();
}
