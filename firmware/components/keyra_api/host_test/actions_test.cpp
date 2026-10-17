#include <string>
#include <cstdio>
#include <memory>

#include "actions.hpp"
#include "keyra_test.hpp"
#include "rig.hpp"

using namespace keyra::actions;

namespace {

bool withdrew(keyra::actions::Machine::Withdraw w) { return w == keyra::actions::Machine::Withdraw::Cancelled; }

int64_t g_now = 1000;
Machine make() { return Machine([] { return g_now; }); }
TypeRequest req(uint32_t id, What w = What::Password) { return {id, "Mail", w, false, {}, nullptr, nullptr, 0}; }

void shortPressRunsPendingOnce() {
  auto m = make();
  m.arm(req(7), "s");
  CHECK(m.pending().has_value());
  CHECK(m.indicator(true, true, false) == Indicator::Pending);
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Run);
  CHECK_EQ(d.run.id, 7u);
  CHECK(!m.pending().has_value());
  CHECK(m.indicator(true, true, false) == Indicator::Typing);
  // A second press while typing must not start another run.
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::None);
  m.typingFinished(d.run, Code::Typed);
  auto last = m.last();
  CHECK(last && last->ok && last->code == Code::Typed && last->title == "Mail");
  CHECK(m.indicator(true, true, false) == Indicator::Success);
  g_now += kFlashMs;
  CHECK(m.indicator(true, true, false) == Indicator::Idle);
  // One-shot: nothing left to run.
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Blink);
}

void armReplacesPending() {
  auto m = make();
  m.arm(req(1), "s");
  m.arm(req(2, What::Both), "s");
  auto p = m.pending();
  CHECK(p && p->req.id == 2 && p->req.what == What::Both);
}

void pendingExpiresAfter60s() {
  auto m = make();
  m.arm(req(3), "s");
  g_now += kExpiryMs - 1;
  CHECK(m.pending().has_value());
  CHECK_EQ(m.pending()->expiresInMs, 1);
  g_now += 1;
  CHECK(!m.pending().has_value());
  auto last = m.last();
  CHECK(last && !last->ok && last->code == Code::Expired);
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Blink);
}

void longPressCancelsThenLocks() {
  auto m = make();
  m.arm(req(4), "s");
  CHECK(m.onButton(Button::Long, true, g_now).effect == Effect::Cancelled);
  CHECK(m.last()->code == Code::Cancelled);
  CHECK(m.onButton(Button::Long, true, g_now).effect == Effect::Lock);
  CHECK(m.onButton(Button::Long, false, g_now).effect == Effect::Blink);
}

void cancelEndpoint() {
  // Like postTypeCancel: an access token's or a tap tag's item is not another session's.
  const auto foreign = [](const std::string& o) { return o.rfind("token:", 0) == 0 || o.rfind("tag:", 0) == 0; };
  auto m = make();
  CHECK(!withdrew(m.cancel("s", foreign)));  // nothing waiting
  // 1. Another session (or none) cannot withdraw it: the slot and last stay as they were.
  m.arm(req(5), "other");
  CHECK(!withdrew(m.cancel("s", foreign)) && !withdrew(m.cancel("", foreign)));
  CHECK(m.pending() && m.pending()->req.id == 5 && !m.last().has_value());
  // 2. Its own action: withdrawn.
  CHECK(withdrew(m.cancel("other", foreign)));
  CHECK(m.last()->code == Code::Cancelled);
  CHECK(!m.pending().has_value());
  // 3. One a token or a tag armed (shown on the session's screen): the session may withdraw it.
  m.arm(req(6), "token:3");
  CHECK(withdrew(m.cancel("s", foreign)) && !m.pending().has_value());
  m.arm(req(7), "tag:2");
  CHECK(withdrew(m.cancel("s", foreign)) && !m.pending().has_value());
  // A presence op is not a type action: this route leaves it alone.
  m.awaitPresence(Op::Backup, [] { return true; }, "s");
  CHECK(!withdrew(m.cancel("s", foreign)) && m.presence().has_value());
}

void presenceApproveRunsCommitOnce() {
  auto m = make();
  int runs = 0;
  m.awaitPresence(Op::Setup, [&runs] { ++runs; return true; }, "s");
  auto pr = m.presence();
  CHECK(pr && pr->awaiting && pr->op == Op::Setup && pr->expiresInMs == kExpiryMs);
  CHECK(m.indicator(false, false, false) == Indicator::AwaitPresence);
  Decision d = m.onButton(Button::Short, false, g_now);
  CHECK(d.effect == Effect::Approve && d.op == Op::Setup && d.commit);
  pr = m.presence();
  CHECK(pr && !pr->awaiting && pr->op == Op::Setup);  // running: op stays visible
  CHECK(m.onButton(Button::Long, false, g_now).effect == Effect::None);
  CHECK(d.commit());
  m.commitFinished(true);
  CHECK(!m.presence().has_value());
  CHECK_EQ(runs, 1);
  CHECK(m.onButton(Button::Short, false, g_now).effect == Effect::Blink);
}

// Secrets captured by a dropped op must be destroyed, not kept around.
void droppedPresenceReleasesCapturedState() {
  auto m = make();
  auto secret = std::make_shared<int>(42);
  std::weak_ptr<int> watch = secret;
  m.awaitPresence(Op::FactoryReset, [secret] { return *secret == 42; }, "s");
  secret.reset();
  CHECK(!watch.expired());
  CHECK(m.onButton(Button::Long, true, g_now).effect == Effect::Cancelled);
  CHECK(watch.expired());
  CHECK(!m.last().has_value());  // presence cancel is not a typing result

  auto again = std::make_shared<int>(1);
  watch = again;
  m.awaitPresence(Op::Wifi, [again] { return true; }, "s");
  again.reset();
  g_now += kExpiryMs;
  CHECK(!m.presence().has_value());
  CHECK(watch.expired());
}

void presenceReplacesTypeAndViceVersa() {
  auto m = make();
  m.arm(req(9), "s");
  m.awaitPresence(Op::Wifi, [] { return true; }, "s");
  CHECK(!m.pending().has_value());
  CHECK(m.presence().has_value());
  m.arm(req(9), "s");
  CHECK(!m.presence().has_value());
  CHECK(m.pending().has_value());
}

void unauthenticatedOpsNeverDisplace() {
  auto m = make();
  m.arm(req(12), "s");
  CHECK(!m.tryAwaitPresence(Op::FactoryReset, [] { return true; }).has_value());
  CHECK(m.pending().has_value());
  withdrew(m.cancel("s", nullptr));
  CHECK(m.tryAwaitPresence(Op::Setup, [] { return true; })->expiresIn == kExpiryMs);
  CHECK(!m.tryAwaitPresence(Op::Setup, [] { return true; }).has_value());
  Decision d = m.onButton(Button::Short, false, g_now);
  CHECK(!m.tryAwaitPresence(Op::FactoryReset, [] { return true; }).has_value());  // still committing
  m.commitFinished(d.commit());
  g_now += kExpiryMs;
  CHECK(m.tryAwaitPresence(Op::FactoryReset, [] { return true; }).has_value());
}

void lockDropsSessionItemsOnly() {
  auto m = make();
  m.arm(req(10), "s");
  m.dropSessionItems();
  CHECK(!m.pending().has_value());
  CHECK(m.last()->code == Code::Cancelled);

  m.awaitPresence(Op::Restore, [] { return true; }, "s");
  m.dropSessionItems();
  CHECK(!m.presence().has_value());

  m.awaitPresence(Op::FactoryReset, [] { return true; }, "s");
  m.dropSessionItems();
  CHECK(m.presence().has_value());  // works without a session by design
}

void failureFlashesError() {
  auto m = make();
  m.arm(req(11), "s");
  Decision d = m.onButton(Button::Short, true, g_now);
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
  m.onButton(Button::Short, true, g_now);
  CHECK(m.indicator(true, true, false) == Indicator::Off);  // blink
}

void presenceOutcomesAreReported() {
  auto m = make();
  CHECK(!m.opResult().has_value());

  m.awaitPresence(Op::Wifi, [] { return false; }, "s");
  Decision d = m.onButton(Button::Short, true, g_now);
  m.commitFinished(d.commit());
  auto r = m.opResult();
  CHECK(r && r->op == Op::Wifi && r->code == OpCode::Failed);

  m.awaitPresence(Op::Setup, [] { return true; }, "s");
  d = m.onButton(Button::Short, false, g_now);
  m.commitFinished(d.commit());
  CHECK(m.opResult()->code == OpCode::Done);
  g_now += 250;
  CHECK_EQ(m.opResult()->agoMs, 250);

  m.awaitPresence(Op::Restore, [] { return true; }, "s");
  g_now += kExpiryMs;
  r = m.opResult();
  CHECK(r && r->op == Op::Restore && r->code == OpCode::Expired && r->agoMs == 0);

  m.awaitPresence(Op::FactoryReset, [] { return true; }, "s");
  m.onButton(Button::Long, true, g_now);
  CHECK(m.opResult()->code == OpCode::Cancelled);

  m.awaitPresence(Op::Wifi, [] { return true; }, "s");
  m.dropSessionItems();
  CHECK(m.opResult()->op == Op::Wifi && m.opResult()->code == OpCode::Cancelled);
  CHECK(std::string(opCodeName(OpCode::Done)) == "done");
}

// SPEC §17: a token follows its own request by serial, whatever came after it.
// SPEC §9.4: an extension's login for another site shows that page's host
// with the pending item (state.pending.host); others carry none.
void pendingCarriesPageHost() {
  auto m = make();
  TypeRequest r = req(7);
  r.host = "evil.example";
  CHECK(m.arm(r, "token:1")->req.host == "evil.example");
  CHECK(m.pending() && m.pending()->req.host == "evil.example");
  m.cancelOwned("token:1");
  CHECK(m.arm(req(7), "token:1") && m.pending()->req.host.empty());
}

void tokenTracksItsOwnRequest() {
  using Stage = Machine::Track::Stage;
  auto m = make();
  CHECK(m.track(0).stage == Stage::Unknown && m.track(12345).stage == Stage::Unknown);
  const auto p = m.arm(req(7), "token:1");
  CHECK(p && p->req.serial != 0);
  const uint32_t s1 = p->req.serial;
  auto t = m.track(s1);
  CHECK(t.stage == Stage::Armed && !t.presence && t.expiresInMs == kExpiryMs);
  // Another owner cannot replace or cancel it.
  CHECK(!m.arm(req(8), "sessionA").has_value());
  CHECK(!m.cancelOwned("token:2") && !m.cancelOwned("sessionA") && !m.cancelOwned(""));
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.run.serial == s1);
  CHECK(m.track(s1).stage == Stage::Running);
  m.typingFinished(d.run, Code::NoUsb);
  t = m.track(s1);
  CHECK(t.stage == Stage::Finished && t.code == Code::NoUsb);
  // Later results by others leave this one readable.
  for (int i = 0; i < 3; ++i) {
    m.arm(req(9), "sessionA");
    withdrew(m.cancel("sessionA", nullptr));
  }
  CHECK(m.track(s1).stage == Stage::Finished && m.track(s1).code == Code::NoUsb);

  // Replaced by the same token: the old one ends cancelled, the new one has its own serial.
  const uint32_t s2 = m.arm(req(7), "token:1")->req.serial;
  const uint32_t s3 = m.arm(req(7, What::Both), "token:1")->req.serial;
  CHECK(s2 != s3 && m.track(s2).code == Code::Cancelled && m.track(s3).stage == Stage::Armed);
  CHECK(m.cancelOwned("token:1"));
  CHECK(m.track(s3).stage == Stage::Finished && m.track(s3).code == Code::Cancelled);
  CHECK(!m.cancelOwned("token:1"));

  // Expiry, and a Bluetooth target still connecting.
  TypeRequest ble = req(7);
  ble.target.kind = Target::Kind::Ble;
  const uint32_t s4 = m.arm(ble, "token:1")->req.serial;
  CHECK(m.track(s4).stage == Stage::Connecting);
  g_now += kExpiryMs;
  CHECK(m.track(s4).stage == Stage::Finished && m.track(s4).code == Code::NoHost);

  // Presence ops (an app token's save): armed, done, or cancelled by its owner.
  auto a = m.awaitPresence(Op::AgentSave, [] { return true; }, "token:3");
  CHECK(a && a->serial != 0 && m.track(a->serial).presence && m.track(a->serial).stage == Stage::Armed);
  d = m.onButton(Button::Short, true, g_now);
  CHECK(m.track(a->serial).stage == Stage::Running);
  m.commitFinished(d.commit());
  t = m.track(a->serial);
  CHECK(t.stage == Stage::Finished && t.presence && t.opCode == OpCode::Done);
  a = m.awaitPresence(Op::AgentSave, [] { return true; }, "token:3");
  CHECK(m.cancelOwned("token:3"));
  CHECK(m.track(a->serial).opCode == OpCode::Cancelled);
  a = m.awaitPresence(Op::AgentSave, [] { return true; }, "token:3");
  m.dropSessionItems();  // lock: a token's items go too
  CHECK(m.track(a->serial).opCode == OpCode::Cancelled);
  a = m.awaitPresence(Op::AgentSave, [] { return true; }, "token:3");
  g_now += kExpiryMs;
  CHECK(m.track(a->serial).opCode == OpCode::Expired);
  CHECK(std::string(opName(Op::TokenCreate)) == "token_create" && parseOp("token_create") == Op::TokenCreate);
  CHECK(std::string(opName(Op::TagCreate)) == "tag_create" && parseOp("tag_create") == Op::TagCreate);
  CHECK(std::string(opName(Op::AgentSave)) == "agent_save" && parseOp("agent_save") == Op::AgentSave);
}

void sequencePartStaysTracked() {
  using Stage = Machine::Track::Stage;
  auto m = make();
  auto job = std::make_shared<SeqJob>();
  job->parts = 2;
  TypeRequest r = req(5, What::Sequence);
  r.seq = job;
  const uint32_t s = m.arm(r, "token:1")->req.serial;
  Decision d = m.onButton(Button::Short, true, g_now);
  m.typingFinished(d.run, Code::Typed);
  CHECK(m.track(s).stage == Stage::Armed);  // waits for the next {PRESS}
  d = m.onButton(Button::Short, true, g_now);
  m.typingFinished(d.run, Code::Typed);
  CHECK(m.track(s).stage == Stage::Finished && m.track(s).code == Code::Typed);
}

void blePairOp() {
  auto m = make();
  int opened = 0;
  m.awaitPresence(Op::BlePair, [&opened] { ++opened; return true; }, "s");
  CHECK(m.presence()->op == Op::BlePair);
  CHECK(m.indicator(true, true, false) == Indicator::AwaitPresence);
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Approve && d.op == Op::BlePair);
  m.commitFinished(d.commit());
  CHECK_EQ(opened, 1);
  CHECK(m.opResult()->op == Op::BlePair && m.opResult()->code == OpCode::Done);
  // Armed through a session: locking drops it like a Wi-Fi change.
  m.awaitPresence(Op::BlePair, [] { return true; }, "s");
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
  m.arm(req(3), "s");
  CHECK(m.indicator(true, true, true) == Indicator::Pending);  // a ready action wins
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(m.indicator(true, true, true) == Indicator::Typing);
  m.typingFinished(d.run, Code::NoHost);
  CHECK(m.indicator(true, true, true) == Indicator::Error);    // the result flash wins
  g_now += kFlashMs;
  CHECK(m.indicator(true, true, true) == Indicator::Pairing);
  m.awaitPresence(Op::Wifi, [] { return true; }, "s");
  CHECK(m.indicator(true, true, true) == Indicator::AwaitPresence);
}

void netOps() {
  auto m = make();
  // Changing the home network is armed through a session, so lock drops it.
  m.awaitPresence(Op::HomeWifi, [] { return true; }, "s");
  m.dropSessionItems();
  CHECK(!m.presence().has_value());
  CHECK(m.opResult()->op == Op::HomeWifi && m.opResult()->code == OpCode::Cancelled);
  // Trusting a browser is requested before any session exists: it never
  // displaces a pending action and survives a lock.
  m.arm(req(13), "s");
  CHECK(!m.tryAwaitPresence(Op::TrustBrowser, [] { return true; }).has_value());
  withdrew(m.cancel("s", nullptr));
  CHECK(m.tryAwaitPresence(Op::TrustBrowser, [] { return true; }).has_value());
  m.dropSessionItems();
  CHECK(m.presence() && m.presence()->op == Op::TrustBrowser);
  CHECK(std::string(opName(Op::HomeWifi)) == "home_wifi");
  CHECK(std::string(opName(Op::TrustBrowser)) == "trust_browser");
}

// SPEC §8.1: a press made while the host connects types once it is ready, within
// kConnectLatchMs; later, or after the action ended, it does nothing.
void pressWhileConnectingTypesWhenReady() {
  auto m = make();
  TypeRequest r = req(22);
  r.target = {Target::Kind::Ble, {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A}};
  m.arm(r, "s");
  CHECK(m.takeLatchedPress().effect == Effect::None);  // nothing pressed yet
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Blink);
  CHECK(m.takeLatchedPress().effect == Effect::None);  // still connecting
  g_now += kConnectLatchMs - 1;
  m.setLinkReady(true);
  Decision d = m.takeLatchedPress();
  CHECK(d.effect == Effect::Run && d.run.target == r.target);
  CHECK(m.takeLatchedPress().effect == Effect::None);  // once
  m.typingFinished(d.run, Code::Typed);

  // Too late: the press is forgotten and the action stays armed.
  m.setLinkReady(false);
  m.arm(r, "s");
  m.onButton(Button::Short, true, g_now);
  g_now += kConnectLatchMs + 1;
  m.setLinkReady(true);
  CHECK(m.takeLatchedPress().effect == Effect::None);
  CHECK(m.pending().has_value());

  // Cancelled meanwhile: nothing runs.
  m.setLinkReady(false);
  m.arm(r, "s");
  m.onButton(Button::Short, true, g_now);
  CHECK(withdrew(m.cancel("s", nullptr)));
  m.setLinkReady(true);
  CHECK(m.takeLatchedPress().effect == Effect::None);
}

// On demand: the action waits for its Bluetooth host before a press types.
void bluetoothTargetWaitsForLink() {
  auto m = make();
  TypeRequest r = req(21);
  r.target = {Target::Kind::Ble, {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A}};
  m.arm(r, "s");
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Blink);  // connecting: press ignored
  CHECK(m.pending().has_value());                                  // ... and still armed
  m.setLinkReady(true);
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Run && d.run.target == r.target);
  m.typingFinished(d.run, Code::Typed);

  // Never connected within 60 s: no_host, not a generic expiry.
  m.setLinkReady(false);
  m.arm(r, "s");
  g_now += kExpiryMs;
  CHECK(!m.pending().has_value());
  CHECK(m.last()->code == Code::NoHost);

  // Connected but nobody pressed: an ordinary expiry.
  m.arm(r, "s");
  m.setLinkReady(true);
  g_now += kExpiryMs;
  m.pending();
  CHECK(m.last()->code == Code::Expired);

  // USB targets never wait.
  m.setLinkReady(false);
  TypeRequest u = req(22);
  u.target = {Target::Kind::Usb, {}};
  m.arm(u, "s");
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Run);
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
  CHECK(std::string(codeName(Code::EntryChanged)) == "entry_changed");
}

// A type action is armed for one version of the account. Edited while it waits
// (even within the same second, when `updated` stays the same), the press
// types nothing; touch() after typing a sequence part is not an edit.
void armedActionPinsTheEntry() {
  auto rig = keyra::vault::test::Rig::ready();
  keyra::vault::Vault& v = *rig->v;
  keyra::vault::Entry e = keyra::vault::test::sample("Bank");
  CHECK(v.put(e) == keyra::vault::Status::Ok);
  keyra::vault::Entry armedAs;
  CHECK(v.get(e.id, armedAs) == keyra::vault::Status::Ok);
  TypeRequest r = req(e.id);
  r.revision = armedAs.revision;
  auto m = make();
  CHECK(m.arm(r, "s").has_value());
  keyra::vault::Entry now;
  CHECK(v.get(e.id, now) == keyra::vault::Status::Ok && !entryChanged(r, now.revision));
  CHECK(v.touch(e.id, 1800000000, false, nullptr) == keyra::vault::Status::Ok);
  CHECK(v.get(e.id, now) == keyra::vault::Status::Ok && !entryChanged(r, now.revision));
  e.password = "rm -rf ~\n";  // swapped while the user's press is awaited
  CHECK(v.put(e) == keyra::vault::Status::Ok);
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Run);
  CHECK(v.get(e.id, now) == keyra::vault::Status::Ok && entryChanged(d.run, now.revision));
  CHECK(now.updated == armedAs.updated);  // `updated` alone would not have told
  m.typingFinished(d.run, Code::EntryChanged);
  CHECK(m.last() && !m.last()->ok && m.last()->code == Code::EntryChanged);
  keyra::vault::wipe(armedAs);
  keyra::vault::wipe(now);
  keyra::vault::wipe(e);
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
  m.arm(usbReq(1), "s");
  m.setUsbMounted(true);  // same connection: nothing changes
  CHECK(m.pending().has_value());
  m.setUsbMounted(false);  // unplugged, suspended or re-enumerating
  CHECK(!m.pending().has_value());
  auto last = m.last();
  CHECK(last && !last->ok && last->code == Code::HostChanged && last->title == "Mail");
  CHECK(m.indicator(true, true, false) == Indicator::Error);
  m.setUsbMounted(true);  // another (or the same) computer: still nothing to type
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Blink);
  CHECK(std::string(codeName(Code::HostChanged)) == "host_changed");

  // Armed on the new connection, it types there.
  m.arm(usbReq(2), "s");
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Run);
}

void unboundActionsIgnoreUsbChanges() {
  auto m = make();
  m.arm(usbReq(1), "s");  // nothing plugged in: no host to bind (no_usb at the press)
  m.setUsbMounted(true);
  m.setUsbMounted(false);
  CHECK(m.pending().has_value());
  TypeRequest ble = req(2);
  ble.target.kind = Target::Kind::Ble;
  m.setUsbMounted(true);
  m.arm(ble, "s");  // a Bluetooth action is bound by address, not by USB
  m.setUsbMounted(false);
  CHECK(m.pending().has_value());
  // A presence op is not a typing action.
  m.setUsbMounted(true);
  m.awaitPresence(Op::Reveal, [] { return true; }, "s");
  m.setUsbMounted(false);
  CHECK(m.presence().has_value());
}

void revealOpsEndWithTheSession() {
  auto m = make();
  for (Op op : {Op::Reveal, Op::Backup, Op::Recovery, Op::Unprotect, Op::Update, Op::PasskeysBackupOn}) {
    m.awaitPresence(op, [] { return true; }, "s");
    m.dropSessionItems();
    CHECK(!m.presence().has_value());
    auto r = m.opResult();
    CHECK(r && r->op == op && r->code == OpCode::Cancelled);
  }
  CHECK(std::string(opName(Op::Reveal)) == "reveal");
  CHECK(std::string(opName(Op::Unprotect)) == "unprotect");
  CHECK(std::string(opName(Op::Update)) == "update");
}

// Deleting an account or a passkey waits for the press like the other
// destructive ops; their API names are stable.
void deleteOpsAreNamed() {
  CHECK(std::string(opName(Op::DeleteEntry)) == "delete_entry");
  CHECK(std::string(opName(Op::DeletePasskey)) == "delete_passkey");
  CHECK(parseOp("delete_entry") == Op::DeleteEntry);
  CHECK(parseOp("delete_passkey") == Op::DeletePasskey);
  CHECK(std::string(opName(Op::PasskeysBackupOn)) == "passkeys_backup_on");
  CHECK(parseOp("passkeys_backup_on") == Op::PasskeysBackupOn);
  auto m = make();
  for (Op op : {Op::DeleteEntry, Op::DeletePasskey}) {
    m.awaitPresence(op, [] { return true; }, "s");
    m.dropSessionItems();  // locking drops them unrun
    auto r = m.opResult();
    CHECK(r && r->op == op && r->code == OpCode::Cancelled);
  }
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
  int n = 0;
  Machine m([] { return g_now; }, [&n] { return "tok" + std::to_string(++n); });
  bool ran = false;
  const std::string tok = m.awaitPresence(Op::FactoryReset, [&] { ran = true; return true; }, "s")->cancel;
  CHECK(tok == "tok1");
  CHECK(!withdrew(m.cancelPresence(Op::Setup, tok)));           // another op: untouched
  CHECK(!withdrew(m.cancelPresence(Op::FactoryReset, "")));     // a stranger without the token
  CHECK(!withdrew(m.cancelPresence(Op::FactoryReset, "tok2")));
  CHECK(m.presence().has_value());
  CHECK(withdrew(m.cancelPresence(Op::FactoryReset, tok)));
  CHECK(!m.presence().has_value());
  const Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect != Effect::Approve);
  CHECK(!ran);
  // A new op gets a new token; the old one cancels nothing.
  m.awaitPresence(Op::FactoryReset, [] { return true; }, "s");
  CHECK(!withdrew(m.cancelPresence(Op::FactoryReset, tok)));
  CHECK(parseOp("factory_reset") == Op::FactoryReset);
  CHECK(!parseOp("nope"));
}


// The press approves only what the session that armed it asked for: another
// session cannot swap the waiting item (a second browser, a stolen session).
void pressIsBoundToItsRequester() {
  int n = 0;
  Machine m([] { return g_now; }, [&n] { return "tok" + std::to_string(++n); });
  int mine = 0, theirs = 0;
  const auto armed = m.awaitPresence(Op::Reveal, [&] { ++mine; return true; }, "alice");
  CHECK(armed && armed->cancel == "tok1" && armed->expiresIn == kExpiryMs);
  CHECK(!m.awaitPresence(Op::Restore, [&] { ++theirs; return true; }, "mallory"));
  CHECK(!m.arm(req(1), "mallory"));  // nor with a type action
  CHECK(!m.tryAwaitPresence(Op::FactoryReset, [&] { ++theirs; return true; }));
  CHECK(m.presence() && m.presence()->op == Op::Reveal);
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Approve && d.op == Op::Reveal);
  CHECK(d.commit());
  m.commitFinished(true);
  CHECK(mine == 1 && theirs == 0);

  // The same session may change its mind: the old item ends as cancelled.
  m.awaitPresence(Op::Backup, [] { return true; }, "alice");
  CHECK(m.awaitPresence(Op::Recovery, [] { return true; }, "alice"));
  CHECK(m.opResult() && m.opResult()->op == Op::Backup && m.opResult()->code == OpCode::Cancelled);
  CHECK(m.arm(req(2), "alice"));  // a type action of hers replaces her op too
  CHECK(m.opResult()->op == Op::Recovery && m.opResult()->code == OpCode::Cancelled);
  CHECK(!m.awaitPresence(Op::Backup, [] { return true; }, "mallory"));  // and is hers alone
  CHECK(!withdrew(m.cancel("mallory", nullptr)));  // nor can she withdraw it
  CHECK(withdrew(m.cancel("alice", nullptr)));

  // Items armed without a session (setup, factory reset, trust) belong to nobody.
  CHECK(m.tryAwaitPresence(Op::Setup, [] { return true; }));
  CHECK(!m.awaitPresence(Op::Reveal, [] { return true; }, "alice"));
  CHECK(!m.tryAwaitPresence(Op::FactoryReset, [] { return true; }));
  CHECK(m.presence()->op == Op::Setup);
}

// The actions task runs a commit (a restore: seconds of PBKDF2) before it reads
// the next press. A press made meanwhile, or before something was armed, must
// not approve what was armed after it (nor cancel it, nor lock).
void stalePressesDoNothing() {
  auto m = make();
  const int64_t early = g_now;
  g_now += 5;
  m.awaitPresence(Op::Backup, [] { return true; }, "s");
  CHECK(!m.pressedBeforeCommitEnd(early));  // arming alone does not touch the FIDO route
  CHECK(m.onButton(Button::Short, true, early).effect == Effect::None);
  CHECK(m.onButton(Button::Long, true, early).effect == Effect::None);
  CHECK(m.presence().has_value());  // still waiting for a real press
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Approve);
  // Pressed while the commit ran; read only after it finished.
  const int64_t during = ++g_now;
  g_now += 3000;
  CHECK(d.commit());
  m.commitFinished(true);
  CHECK(m.pressedBeforeCommitEnd(during) && !m.pressedBeforeCommitEnd(g_now));
  CHECK(m.onButton(Button::Long, true, during).effect == Effect::None);  // no lock
  g_now += 10;
  m.arm(req(9), "other");
  CHECK(m.onButton(Button::Short, true, during).effect == Effect::None);
  CHECK(m.pending().has_value());
  CHECK(m.onButton(Button::Short, true, g_now).effect == Effect::Run);
  // A sequence's next part waits for a press made after the part was typed.
  auto s = make();
  auto job = std::make_shared<SeqJob>();
  job->parts = 2;
  TypeRequest r = req(3, What::Sequence);
  r.seq = job;
  s.arm(r, "s");
  Decision run = s.onButton(Button::Short, true, g_now);
  CHECK(run.effect == Effect::Run);
  const int64_t whileTyping = ++g_now;
  g_now += 500;
  s.typingFinished(run.run, Code::Typed);
  CHECK(s.pending().has_value());
  CHECK(s.onButton(Button::Short, true, whileTyping).effect == Effect::None);
  CHECK(s.onButton(Button::Short, true, g_now).effect == Effect::Run);
}

// ---------- requestId (SPEC §12.6a) ----------

using W = Machine::Withdraw;
using Stage = Machine::Track::Stage;

// Distinct 32-hex ids, like the device's 128 random bits.
Machine makeIds() {
  return Machine([] { return g_now; }, [] {
    static unsigned n = 0;
    char b[33];
    std::snprintf(b, sizeof b, "%032x", ++n);
    return std::string(b);
  });
}

void requestIdsAreIssuedAndTracked() {
  auto m = makeIds();
  const auto p = m.arm(req(7), "s");
  CHECK(p && validRequestId(p->req.requestId));
  const auto t = m.trackRequest(p->req.requestId, "s");
  CHECK(t && t->stage == Stage::Armed && !t->presence && t->expiresInMs == kExpiryMs);
  CHECK(std::string(trackState(*t)) == "armed" && trackCode(*t) == nullptr);
  // Only for its owner: another session or a token learns nothing.
  CHECK(!m.trackRequest(p->req.requestId, "other").has_value());
  CHECK(!m.trackRequest(p->req.requestId, "").has_value());
  CHECK(!m.trackRequest("00000000000000000000000000000063", "s").has_value());  // unknown
  CHECK(!m.trackRequest("", "s").has_value());
  // Pressed and typed: it reports its own outcome.
  Decision d = m.onButton(Button::Short, true, g_now);
  CHECK(d.effect == Effect::Run && d.run.requestId == p->req.requestId);
  CHECK(m.trackRequest(p->req.requestId, "s")->stage == Stage::Running);
  m.typingFinished(d.run, Code::Typed);
  const auto done = m.trackRequest(p->req.requestId, "s");
  CHECK(done && done->stage == Stage::Finished && std::string(trackState(*done)) == "typed" &&
        std::string(trackCode(*done)) == "typed");
  // A presence op without an owner (setup) is tracked by whoever holds its id.
  const auto a = m.tryAwaitPresence(Op::Setup, [] { return true; });
  CHECK(a && validRequestId(a->requestId) && a->requestId != p->req.requestId);
  const auto ta = m.trackRequest(a->requestId, "");
  CHECK(ta && ta->presence && ta->stage == Stage::Armed);
  CHECK(m.trackRequest(a->requestId, "s").has_value());
  g_now += kExpiryMs;
  const auto te = m.trackRequest(a->requestId, "");
  CHECK(te && std::string(trackState(*te)) == "expired" && std::string(trackCode(*te)) == "expired");
}

void staleRequestIdIsSuperseded() {
  auto m = makeIds();
  const auto first = m.arm(req(1), "s");
  const auto second = m.arm(req(2), "s");  // the same session replaces its own action
  CHECK(first && second && first->req.requestId != second->req.requestId);
  const auto t = m.trackRequest(first->req.requestId, "s");
  CHECK(t && t->superseded && std::string(trackState(*t)) == "cancelled" &&
        std::string(trackCode(*t)) == "superseded");
  // Cancelling the old one does nothing to the new one.
  CHECK(m.cancel("s", nullptr, first->req.requestId) == W::Superseded);
  CHECK(m.pending() && m.pending()->req.id == 2);
  CHECK(m.cancel("s", nullptr, second->req.requestId) == W::Cancelled);
  CHECK(!m.pending().has_value());
  CHECK(m.cancel("s", nullptr, second->req.requestId) == W::NotCancelled);  // already gone, not superseded
  // A cancelled (not replaced) request is not "superseded".
  const auto t2 = m.trackRequest(second->req.requestId, "s");
  CHECK(t2 && !t2->superseded && std::string(trackCode(*t2)) == "cancelled");
}

void cancelByRequestIdIsForItsOwner() {
  auto m = makeIds();
  const auto foreign = [](const std::string& o) { return o.rfind("token:", 0) == 0; };
  const auto p = m.arm(req(3), "alice");
  CHECK(m.cancel("mallory", foreign, p->req.requestId) == W::NotCancelled);
  CHECK(m.pending().has_value());
  CHECK(m.cancel("alice", foreign, p->req.requestId) == W::Cancelled);
  // An action a token armed: any session may withdraw it, as without a requestId.
  const auto tp = m.arm(req(4), "token:9");
  CHECK(m.cancel("alice", foreign, tp->req.requestId) == W::Cancelled);
  // A token cancels only its own request by id.
  const auto tq = m.arm(req(5), "token:9");
  CHECK(m.cancelOwnedRequest("token:8", tq->req.requestId) == W::NotCancelled);
  CHECK(m.pending().has_value());
  CHECK(m.cancelOwnedRequest("token:9", tq->req.requestId) == W::Cancelled);
  CHECK(!m.pending().has_value());
}

void tokenRequestIdsAcrossTypeAndSave() {
  auto m = makeIds();
  const auto typing = m.arm(req(6), "token:1");
  const auto save = m.awaitPresence(Op::AgentSave, [] { return true; }, "token:1");  // replaces its typing
  CHECK(typing && save);
  CHECK(m.cancelOwnedRequest("token:1", typing->req.requestId) == W::Superseded);
  CHECK(m.presence() && m.presence()->awaiting);  // the save is untouched
  const auto ts = m.trackRequest(save->requestId, "token:1");
  CHECK(ts && ts->presence && ts->stage == Stage::Armed);
  CHECK(m.cancelOwnedRequest("token:1", save->requestId) == W::Cancelled);
  const auto done = m.trackRequest(save->requestId, "token:1");
  CHECK(done && std::string(trackState(*done)) == "cancelled" && !done->superseded);
}

void presenceCancelWithRequestId() {
  auto m = makeIds();
  const auto a = m.awaitPresence(Op::Reveal, [] { return true; }, "s");
  const auto b = m.awaitPresence(Op::Backup, [] { return true; }, "s");  // replaces the reveal
  CHECK(a && b && a->requestId != b->requestId);
  CHECK(m.cancelPresence(Op::Reveal, a->cancel, a->requestId) == W::Superseded);
  CHECK(m.cancelPresence(Op::Backup, b->cancel, a->requestId) == W::Superseded);  // wrong id: nothing
  CHECK(m.presence() && m.presence()->op == Op::Backup);
  CHECK(m.cancelPresence(Op::Backup, "wrong", b->requestId) == W::NotCancelled);  // the token still decides
  CHECK(m.cancelPresence(Op::Backup, b->cancel, "00000000000000000000000000000063") == W::NotCancelled);
  CHECK(m.cancelPresence(Op::Backup, b->cancel, b->requestId) == W::Cancelled);
  CHECK(!m.presence().has_value());
}

// Old clients send no requestId: everything works as before 0.4.1.
void oldClientsWithoutRequestId() {
  auto m = makeIds();
  m.arm(req(1), "s");
  CHECK(m.cancel("s", nullptr) == W::Cancelled);
  const auto a = m.awaitPresence(Op::Backup, [] { return true; }, "s");
  CHECK(m.cancelPresence(Op::Backup, a->cancel) == W::Cancelled);
  m.arm(req(2), "token:3");
  CHECK(m.cancelOwned("token:3"));
  // And a machine without an id generator issues none, tracks none.
  auto plain = make();
  const auto p = plain.arm(req(1), "s");
  CHECK(p && p->req.requestId.empty() && !plain.trackRequest("", "s").has_value());
}

void sequenceKeepsItsRequestId() {
  auto m = makeIds();
  auto job = std::make_shared<SeqJob>();
  job->parts = 2;
  TypeRequest r = req(9, What::Sequence);
  r.seq = job;
  const auto p = m.arm(std::move(r), "s");
  Decision d = m.onButton(Button::Short, true, g_now);
  m.typingFinished(d.run, Code::Typed);  // part 1 typed, part 2 armed again
  CHECK(m.pending() && m.pending()->req.part == 1);
  const auto t = m.trackRequest(p->req.requestId, "s");
  CHECK(t && t->stage == Stage::Armed);
  CHECK(m.cancel("s", nullptr, p->req.requestId) == W::Cancelled);
}

void requestIdShape() {
  CHECK(validRequestId("0123456789abcdef0123456789abcdef"));
  CHECK(!validRequestId("0123456789ABCDEF0123456789abcdef"));  // lowercase only
  CHECK(!validRequestId("0123456789abcdef0123456789abcde"));
  CHECK(!validRequestId("0123456789abcdef0123456789abcdeg"));
  CHECK(!validRequestId(""));
  Machine::Track t;
  CHECK(std::string(trackState(t)) == "none" && trackCode(t) == nullptr);
  t.stage = Stage::Connecting;
  CHECK(std::string(trackState(t)) == "waiting");
  t.stage = Stage::Finished;
  t.presence = true;
  t.opCode = OpCode::Done;
  CHECK(std::string(trackState(t)) == "done" && std::string(trackCode(t)) == "done");
  t.presence = false;
  t.code = Code::EntryChanged;
  CHECK(std::string(trackState(t)) == "failed" && std::string(trackCode(t)) == "entry_changed");
}

int main() {
  requestIdsAreIssuedAndTracked();
  staleRequestIdIsSuperseded();
  cancelByRequestIdIsForItsOwner();
  tokenRequestIdsAcrossTypeAndSave();
  presenceCancelWithRequestId();
  oldClientsWithoutRequestId();
  sequenceKeepsItsRequestId();
  requestIdShape();
  stalePressesDoNothing();
  tokenTracksItsOwnRequest();
  pendingCarriesPageHost();
  sequencePartStaysTracked();
  pressIsBoundToItsRequester();
  testCancelPresence();
  usbActionIsBoundToItsHost();
  unboundActionsIgnoreUsbChanges();
  revealOpsEndWithTheSession();
  deleteOpsAreNamed();
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
  pressWhileConnectingTypesWhenReady();
  names();
  armedActionPinsTheEntry();
  return KEYRA_TEST_RESULT();
}
