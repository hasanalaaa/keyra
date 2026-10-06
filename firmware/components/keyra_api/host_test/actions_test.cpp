#include <memory>

#include "actions.hpp"
#include "keyra_test.hpp"

using namespace keyra::actions;

namespace {

int64_t g_now = 1000;
Machine make() { return Machine([] { return g_now; }); }
TypeRequest req(uint32_t id, What w = What::Password) { return {id, "Mail", w, false}; }

void shortPressRunsPendingOnce() {
  auto m = make();
  m.arm(req(7));
  CHECK(m.pending().has_value());
  CHECK(m.indicator(true, true, false) == Indicator::Pending);
  Decision d = m.onButton(Button::Short, true);
  CHECK(d.effect == Effect::Run);
  CHECK_EQ(d.run.id, 7u);
  CHECK(!m.pending().has_value());
  CHECK(m.indicator(true, true, false) == Indicator::Typing);
  // A second press while typing must not start another run.
  CHECK(m.onButton(Button::Short, true).effect == Effect::None);
  m.typingFinished(d.run, Code::Typed);
  auto last = m.last();
  CHECK(last && last->ok && last->code == Code::Typed && last->title == "Mail");
  CHECK(m.indicator(true, true, false) == Indicator::Success);
  g_now += kFlashMs;
  CHECK(m.indicator(true, true, false) == Indicator::Idle);
  // One-shot: nothing left to run.
  CHECK(m.onButton(Button::Short, true).effect == Effect::Blink);
}

void armReplacesPending() {
  auto m = make();
  m.arm(req(1));
  m.arm(req(2, What::Both));
  auto p = m.pending();
  CHECK(p && p->req.id == 2 && p->req.what == What::Both);
}

void pendingExpiresAfter60s() {
  auto m = make();
  m.arm(req(3));
  g_now += kExpiryMs - 1;
  CHECK(m.pending().has_value());
  CHECK_EQ(m.pending()->expiresInMs, 1);
  g_now += 1;
  CHECK(!m.pending().has_value());
  auto last = m.last();
  CHECK(last && !last->ok && last->code == Code::Expired);
  CHECK(m.onButton(Button::Short, true).effect == Effect::Blink);
}

void longPressCancelsThenLocks() {
  auto m = make();
  m.arm(req(4));
  CHECK(m.onButton(Button::Long, true).effect == Effect::Cancelled);
  CHECK(m.last()->code == Code::Cancelled);
  CHECK(m.onButton(Button::Long, true).effect == Effect::Lock);
  CHECK(m.onButton(Button::Long, false).effect == Effect::Blink);
}

void cancelEndpoint() {
  auto m = make();
  CHECK(!m.cancel());
  m.arm(req(5));
  CHECK(m.cancel());
  CHECK(m.last()->code == Code::Cancelled);
  CHECK(!m.pending().has_value());
}

void presenceApproveRunsCommitOnce() {
  auto m = make();
  int runs = 0;
  m.awaitPresence(Op::Setup, [&runs] { ++runs; return true; });
  auto pr = m.presence();
  CHECK(pr && pr->awaiting && pr->op == Op::Setup && pr->expiresInMs == kExpiryMs);
  CHECK(m.indicator(false, false, false) == Indicator::AwaitPresence);
  Decision d = m.onButton(Button::Short, false);
  CHECK(d.effect == Effect::Approve && d.op == Op::Setup && d.commit);
  pr = m.presence();
  CHECK(pr && !pr->awaiting && pr->op == Op::Setup);  // running: op stays visible
  CHECK(m.onButton(Button::Long, false).effect == Effect::None);
  CHECK(d.commit());
  m.commitFinished(true);
  CHECK(!m.presence().has_value());
  CHECK_EQ(runs, 1);
  CHECK(m.onButton(Button::Short, false).effect == Effect::Blink);
}

// Secrets captured by a dropped op must be destroyed, not kept around.
void droppedPresenceReleasesCapturedState() {
  auto m = make();
  auto secret = std::make_shared<int>(42);
  std::weak_ptr<int> watch = secret;
  m.awaitPresence(Op::FactoryReset, [secret] { return *secret == 42; });
  secret.reset();
  CHECK(!watch.expired());
  CHECK(m.onButton(Button::Long, true).effect == Effect::Cancelled);
  CHECK(watch.expired());
  CHECK(!m.last().has_value());  // presence cancel is not a typing result

  auto again = std::make_shared<int>(1);
  watch = again;
  m.awaitPresence(Op::Wifi, [again] { return true; });
  again.reset();
  g_now += kExpiryMs;
  CHECK(!m.presence().has_value());
  CHECK(watch.expired());
}

void presenceReplacesTypeAndViceVersa() {
  auto m = make();
  m.arm(req(9));
  m.awaitPresence(Op::Wifi, [] { return true; });
  CHECK(!m.pending().has_value());
  CHECK(m.presence().has_value());
  m.arm(req(9));
  CHECK(!m.presence().has_value());
  CHECK(m.pending().has_value());
}

void unauthenticatedOpsNeverDisplace() {
  auto m = make();
  m.arm(req(12));
  CHECK(!m.tryAwaitPresence(Op::FactoryReset, [] { return true; }).has_value());
  CHECK(m.pending().has_value());
  m.cancel();
  CHECK(m.tryAwaitPresence(Op::Setup, [] { return true; }) == kExpiryMs);
  CHECK(!m.tryAwaitPresence(Op::Setup, [] { return true; }).has_value());
  Decision d = m.onButton(Button::Short, false);
  CHECK(!m.tryAwaitPresence(Op::FactoryReset, [] { return true; }).has_value());  // still committing
  m.commitFinished(d.commit());
  g_now += kExpiryMs;
  CHECK(m.tryAwaitPresence(Op::FactoryReset, [] { return true; }).has_value());
}

void lockDropsSessionItemsOnly() {
  auto m = make();
  m.arm(req(10));
  m.dropSessionItems();
  CHECK(!m.pending().has_value());
  CHECK(m.last()->code == Code::Cancelled);

  m.awaitPresence(Op::RestoreReplace, [] { return true; });
  m.dropSessionItems();
  CHECK(!m.presence().has_value());

  m.awaitPresence(Op::FactoryReset, [] { return true; });
  m.dropSessionItems();
  CHECK(m.presence().has_value());  // works without a session by design
}

void failureFlashesError() {
  auto m = make();
  m.arm(req(11));
  Decision d = m.onButton(Button::Short, true);
  m.typingFinished(d.run, Code::NoUsb);
  CHECK(m.indicator(true, true, false) == Indicator::Error);
  CHECK(!m.last()->ok);
  CHECK(m.last()->code == Code::NoUsb);
  g_now += 500;
  CHECK_EQ(m.last()->agoMs, 500);
}

void baseIndicators() {
  auto m = make();
  g_now += 10 * kFlashMs;
  CHECK(m.indicator(false, false, false) == Indicator::Setup);
  CHECK(m.indicator(true, false, false) == Indicator::Locked);
  CHECK(m.indicator(true, true, false) == Indicator::Idle);
  m.onButton(Button::Short, true);
  CHECK(m.indicator(true, true, false) == Indicator::Off);  // blink
}

void presenceOutcomesAreReported() {
  auto m = make();
  CHECK(!m.opResult().has_value());

  m.awaitPresence(Op::Wifi, [] { return false; });
  Decision d = m.onButton(Button::Short, true);
  m.commitFinished(d.commit());
  auto r = m.opResult();
  CHECK(r && r->op == Op::Wifi && r->code == OpCode::Failed);

  m.awaitPresence(Op::Setup, [] { return true; });
  d = m.onButton(Button::Short, false);
  m.commitFinished(d.commit());
  CHECK(m.opResult()->code == OpCode::Done);
  g_now += 250;
  CHECK_EQ(m.opResult()->agoMs, 250);

  m.awaitPresence(Op::RestoreReplace, [] { return true; });
  g_now += kExpiryMs;
  r = m.opResult();
  CHECK(r && r->op == Op::RestoreReplace && r->code == OpCode::Expired && r->agoMs == 0);

  m.awaitPresence(Op::FactoryReset, [] { return true; });
  m.onButton(Button::Long, true);
  CHECK(m.opResult()->code == OpCode::Cancelled);

  m.awaitPresence(Op::Wifi, [] { return true; });
  m.dropSessionItems();
  CHECK(m.opResult()->op == Op::Wifi && m.opResult()->code == OpCode::Cancelled);
  CHECK(std::string(opCodeName(OpCode::Done)) == "done");
}

void blePairOp() {
  auto m = make();
  int opened = 0;
  m.awaitPresence(Op::BlePair, [&opened] { ++opened; return true; });
  CHECK(m.presence()->op == Op::BlePair);
  CHECK(m.indicator(true, true, false) == Indicator::AwaitPresence);
  Decision d = m.onButton(Button::Short, true);
  CHECK(d.effect == Effect::Approve && d.op == Op::BlePair);
  m.commitFinished(d.commit());
  CHECK_EQ(opened, 1);
  CHECK(m.opResult()->op == Op::BlePair && m.opResult()->code == OpCode::Done);
  // Armed through a session: locking drops it like a Wi-Fi change.
  m.awaitPresence(Op::BlePair, [] { return true; });
  m.dropSessionItems();
  CHECK(!m.presence().has_value());
  CHECK(m.opResult()->code == OpCode::Cancelled);
}

void pairingIndicatorYields() {
  auto m = make();
  g_now += 10 * kFlashMs;
  CHECK(m.indicator(true, true, true) == Indicator::Pairing);
  CHECK(m.indicator(true, false, true) == Indicator::Pairing);
  CHECK(m.indicator(false, false, true) == Indicator::Pairing);
  m.arm(req(3));
  CHECK(m.indicator(true, true, true) == Indicator::Pending);  // a ready action wins
  Decision d = m.onButton(Button::Short, true);
  CHECK(m.indicator(true, true, true) == Indicator::Typing);
  m.typingFinished(d.run, Code::NoHost);
  CHECK(m.indicator(true, true, true) == Indicator::Error);    // the result flash wins
  g_now += kFlashMs;
  CHECK(m.indicator(true, true, true) == Indicator::Pairing);
  m.awaitPresence(Op::Wifi, [] { return true; });
  CHECK(m.indicator(true, true, true) == Indicator::AwaitPresence);
}

void names() {
  CHECK(parseWhat("both") == What::Both);
  CHECK(!parseWhat("test").has_value());
  CHECK(!parseWhat("").has_value());
  CHECK(std::string(codeName(Code::UnsupportedChar)) == "unsupported_char");
  CHECK(std::string(codeName(Code::NoUsb)) == "no_usb");
  CHECK(std::string(codeName(Code::NoHost)) == "no_host");
  CHECK(std::string(opName(Op::BlePair)) == "ble_pair");
  CHECK(std::string(opName(Op::FactoryReset)) == "factory_reset");
  CHECK(std::string(whatName(What::Totp)) == "totp");
}

}  // namespace

int main() {
  shortPressRunsPendingOnce();
  armReplacesPending();
  pendingExpiresAfter60s();
  longPressCancelsThenLocks();
  cancelEndpoint();
  presenceApproveRunsCommitOnce();
  droppedPresenceReleasesCapturedState();
  presenceReplacesTypeAndViceVersa();
  unauthenticatedOpsNeverDisplace();
  lockDropsSessionItemsOnly();
  failureFlashesError();
  baseIndicators();
  presenceOutcomesAreReported();
  blePairOp();
  pairingIndicatorYields();
  names();
  return KEYRA_TEST_RESULT();
}
