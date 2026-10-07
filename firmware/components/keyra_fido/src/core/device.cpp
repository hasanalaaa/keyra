#include "device.hpp"

#include <algorithm>

namespace keyra::fido {

Device::Device(Link& link, Crypto& c, Store& s, Counter& n, TouchGate& gate, hid::Ctaphid::Version v)
    : link_(link),
      store_(s),
      gate_(gate),
      hid_([&link](const uint8_t* p) { link.send(p); }, v, [&link] { link.wink(); }),
      auth_(c, s, n) {}

void Device::step(uint32_t waitMs) {
  uint8_t pkt[hid::kPacket];
  hid::Message m;
  if (link_.recv(pkt, waitMs) && hid_.feed(pkt, sizeof pkt, link_.nowMs(), m)) {
    hid_.begin(m.cid);
    const auto resp = m.cmd == hid::kCbor ? auth_.cbor(m.data.data(), m.data.size(), *this, link_.nowMs())
                                          : auth_.msg(m.data.data(), m.data.size(), *this);
    hid_.reply(m.cid, m.cmd, resp.data(), resp.size());
  }
  const int64_t now = link_.nowMs();
  hid_.tick(now);
  gate_.tick(now);
}

void Device::pump(uint32_t waitMs) {
  uint8_t pkt[hid::kPacket];
  hid::Message ignored;  // never filled while busy
  if (link_.recv(pkt, waitMs)) hid_.feed(pkt, sizeof pkt, link_.nowMs(), ignored);
  hid_.tick(link_.nowMs());
}

User::Answer Device::wait(TouchGate::Phase phase, uint8_t keepalive, const std::function<bool()>& done) {
  const int64_t deadline = link_.nowMs() + kPresenceTimeoutMs;
  int64_t nextKeepalive = link_.nowMs();
  gate_.begin(phase);
  Answer a = Answer::Timeout;
  for (;;) {
    const int64_t now = link_.nowMs();
    if (now >= nextKeepalive) {
      hid_.keepalive(keepalive);
      nextKeepalive = now + kKeepaliveMs;
    }
    if (hid_.takeCancel()) {
      a = Answer::Cancelled;
      break;
    }
    const TouchGate::Answer g = gate_.answer();
    if (g == TouchGate::Answer::Denied) {
      a = Answer::Denied;
      break;
    }
    if (done()) {
      a = Answer::Approved;
      break;
    }
    if (now >= deadline) break;
    pump(static_cast<uint32_t>(std::min<int64_t>({20, deadline - now, std::max<int64_t>(1, nextKeepalive - now)})));
  }
  gate_.end();
  return a;
}

User::Answer Device::waitUnlocked() {
  if (store_.unlocked()) return Answer::Approved;
  return wait(TouchGate::Phase::Unlock, hid::kProcessing, [this] { return store_.unlocked(); });
}

User::Answer Device::waitPresence() {
  return wait(TouchGate::Phase::Presence, hid::kUpNeeded,
              [this] { return gate_.answer() == TouchGate::Answer::Approved; });
}

bool Device::takePresence() { return gate_.takeLatched(link_.nowMs()); }

}  // namespace keyra::fido
