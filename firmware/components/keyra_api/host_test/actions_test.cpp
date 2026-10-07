#include <memory>

#include "actions.hpp"
#include "keyra_test.hpp"

using namespace keyra::actions;

namespace {

int64_t g_now = 1000;
Machine make() { return Machine([] { return g_now; }); }
TypeRequest req(uint32_t id, What w = What::Password) { return {id, "Mail", w, false, {}, nullptr, nullptr, 0}; }

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

void netOps() {
  auto m = make();
  // Changing the home network is armed through a session, so lock drops it.
  m.awaitPresence(Op::HomeWifi, [] { return true; });
  m.dropSessionItems();
  CHECK(!m.presence().has_value());
  CHECK(m.opResult()->op == Op::HomeWifi && m.opResult()->code == OpCode::Cancelled);
  // Trusting a browser is requested before any session exists: it never
  // displaces a pending action and survives a lock.
  m.arm(req(13));
  CHECK(!m.tryAwaitPresence(Op::TrustBrowser, [] { return true; }).has_value());
  m.cancel();
  CHECK(m.tryAwaitPresence(Op::TrustBrowser, [] { return true; }).has_value());
  m.dropSessionItems();
  CHECK(m.presence() && m.presence()->op == Op::TrustBrowser);
  CHECK(std::string(opName(Op::HomeWifi)) == "home_wifi");
  CHECK(std::string(opName(Op::TrustBrowser)) == "trust_browser");
}

// On demand: the action waits for its Bluetooth host before a press types.
void bluetoothTargetWaitsForLink() {
  auto m = make();
  TypeRequest r = req(21);
  r.target = {Target::Kind::Ble, {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A}};
  m.arm(r);
  CHECK(m.onButton(Button::Short, true).effect == Effect::Blink);  // connecting: press ignored
  CHECK(m.pending().has_value());                                  // ... and still armed
  m.setLinkReady(true);
  Decision d = m.onButton(Button::Short, true);
  CHECK(d.effect == Effect::Run && d.run.target == r.target);
  m.typingFinished(d.run, Code::Typed);

  // Never connected within 60 s: no_host, not a generic expiry.
  m.setLinkReady(false);
  m.arm(r);
  g_now += kExpiryMs;
  CHECK(!m.pending().has_value());
  CHECK(m.last()->code == Code::NoHost);

  // Connected but nobody pressed: an ordinary expiry.
  m.arm(r);
  m.setLinkReady(true);
  g_now += kExpiryMs;
  m.pending();
  CHECK(m.last()->code == Code::Expired);

  // USB targets never wait.
  m.setLinkReady(false);
  TypeRequest u = req(22);
  u.target = {Target::Kind::Usb, {}};
  m.arm(u);
  CHECK(m.onButton(Button::Short, true).effect == Effect::Run);
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

TypeRequest usbReq(uint32_t id) {
  TypeRequest r = req(id);
  r.target.kind = Target::Kind::Usb;
  return r;
}

// N8: an action armed for one USB connection never types into the next.
void usbActionIsBoundToItsHost() {
  auto m = make();
  m.setUsbMounted(true);
  m.arm(usbReq(1));
  m.setUsbMounted(true);  // same connection: nothing changes
  CHECK(m.pending().has_value());
  m.setUsbMounted(false);  // unplugged, suspended or re-enumerating
  CHECK(!m.pending().has_value());
  auto last = m.last();
  CHECK(last && !last->ok && last->code == Code::HostChanged && last->title == "Mail");
  CHECK(m.indicator(true, true, false) == Indicator::Error);
  m.setUsbMounted(true);  // another (or the same) computer: still nothing to type
  CHECK(m.onButton(Button::Short, true).effect == Effect::Blink);
  CHECK(std::string(codeName(Code::HostChanged)) == "host_changed");

  // Armed on the new connection, it types there.
  m.arm(usbReq(2));
  CHECK(m.onButton(Button::Short, true).effect == Effect::Run);
}

void unboundActionsIgnoreUsbChanges() {
  auto m = make();
  m.arm(usbReq(1));  // nothing plugged in: no host to bind (no_usb at the press)
  m.setUsbMounted(true);
  m.setUsbMounted(false);
  CHECK(m.pending().has_value());
  TypeRequest ble = req(2);
  ble.target.kind = Target::Kind::Ble;
  m.setUsbMounted(true);
  m.arm(ble);  // a Bluetooth action is bound by address, not by USB
  m.setUsbMounted(false);
  CHECK(m.pending().has_value());
  // A presence op is not a typing action.
  m.setUsbMounted(true);
  m.awaitPresence(Op::Reveal, [] { return true; });
  m.setUsbMounted(false);
  CHECK(m.presence().has_value());
}

void revealOpsEndWithTheSession() {
  auto m = make();
  for (Op op : {Op::Reveal, Op::Backup, Op::Recovery, Op::Unprotect}) {
    m.awaitPresence(op, [] { return true; });
    m.dropSessionItems();
    CHECK(!m.presence().has_value());
    auto r = m.opResult();
    CHECK(r && r->op == op && r->code == OpCode::Cancelled);
  }
  CHECK(std::string(opName(Op::Reveal)) == "reveal");
  CHECK(std::string(opName(Op::Unprotect)) == "unprotect");
}

// A4: lock when the computer goes away, never on charger-only power.
void hostWatchUsb() {
  HostWatch w;
  int64_t t = 0;
  // Charger only: unlocked, never enumerated.
  for (; t < 5000; t += 100) CHECK(!w.pollUsb(t, true, false, true));
  // A computer shows up, then is unplugged: locks once it has been gone 1 s.
  CHECK(!w.pollUsb(t, true, true, true));
  t += 100;
  CHECK(!w.pollUsb(t, true, false, true));
  CHECK(!w.pollUsb(t + HostWatch::kUsbGoneMs - 1, true, false, true));
  CHECK(w.pollUsb(t + HostWatch::kUsbGoneMs, true, false, true));
  CHECK(!w.pollUsb(t + 5000, true, false, true));  // only once
  // A short bus reset (back within the second) does not lock.
  t += 10000;
  CHECK(!w.pollUsb(t, true, true, true));
  CHECK(!w.pollUsb(t + 100, true, false, true));
  CHECK(!w.pollUsb(t + 600, true, true, true));
  CHECK(!w.pollUsb(t + 2000, true, false, true));
  CHECK(!w.pollUsb(t + 2900, true, false, true));
  CHECK(w.pollUsb(t + 3000, true, false, true));
  // Setting off: never locks.
  t += 10000;
  CHECK(!w.pollUsb(t, true, true, false));
  CHECK(!w.pollUsb(t + 5000, true, false, false));
  // Locking forgets the host: unlocking later on a charger does not lock.
  t += 10000;
  CHECK(!w.pollUsb(t, true, true, true));
  CHECK(!w.pollUsb(t + 100, false, false, true));
  CHECK(!w.pollUsb(t + 200, true, false, true));
  CHECK(!w.pollUsb(t + 5000, true, false, true));
}

void hostWatchBle() {
  HostWatch w;
  const BtAddr a{1, 2, 3, 4, 5, 6}, b{9, 9, 9, 9, 9, 9};
  CHECK(!w.bleLost(a, true, true));  // never typed into it
  w.bleUsed(a);
  CHECK(!w.bleLost(b, true, true));   // another host
  CHECK(!w.bleLost(a, true, false));  // setting off
  CHECK(w.bleLost(a, true, true));
  CHECK(!w.bleLost(a, true, true));   // once
  w.bleUsed(a);
  CHECK(!w.pollUsb(0, false, false, true));  // the lock forgets it
  CHECK(!w.bleLost(a, true, true));
}

}  // namespace

// A cancelled "press the button" screen withdraws its op: a later press must
// not run it (it used to wipe the vault after a cancelled factory reset).
void testCancelPresence() {
  Machine m = make();
  bool ran = false;
  m.awaitPresence(Op::FactoryReset, [&] { ran = true; return true; });
  CHECK(!m.cancelPresence(Op::Setup));  // another op: untouched
  CHECK(m.presence().has_value());
  CHECK(m.cancelPresence(Op::FactoryReset));
  CHECK(!m.presence().has_value());
  const Decision d = m.onButton(Button::Short, true);
  CHECK(d.effect != Effect::Approve);
  CHECK(!ran);
  CHECK(parseOp("factory_reset") == Op::FactoryReset);
  CHECK(!parseOp("nope"));
}

int main() {
  testCancelPresence();
  usbActionIsBoundToItsHost();
  unboundActionsIgnoreUsbChanges();
  revealOpsEndWithTheSession();
  hostWatchUsb();
  hostWatchBle();
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
  netOps();
  bluetoothTargetWaitsForLink();
  names();
  return KEYRA_TEST_RESULT();
}
