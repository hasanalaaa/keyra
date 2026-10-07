// Device loop + TouchGate: keepalives while waiting for the button, cancel,
// timeout, refusal, busy channels, the U2F press latch, unlock wait.
#include <array>
#include <cstring>
#include <deque>
#include <functional>

#include "core/cbor.hpp"
#include "core/device.hpp"
#include "fakes.hpp"
#include "host_crypto.hpp"
#include "keyra_test.hpp"

using namespace keyra::fido;
using namespace keyra::fido::test;
using Pkt = std::array<uint8_t, hid::kPacket>;
using Bytes = std::vector<uint8_t>;

namespace {

// Simulated time: recv() without input advances the clock by waitMs.
class FakeLink final : public Link {
 public:
  int64_t now = 100;
  std::deque<std::pair<int64_t, Pkt>> in;  // (deliver at, packet)
  std::vector<std::pair<int64_t, Pkt>> out;
  std::function<void(int64_t)> onTime;  // e.g. press the button at some point
  int winks = 0;

  bool recv(uint8_t p[hid::kPacket], uint32_t waitMs) override {
    if (onTime) onTime(now);
    if (!in.empty() && in.front().first <= now + waitMs) {
      now = std::max(now, in.front().first);
      std::memcpy(p, in.front().second.data(), hid::kPacket);
      in.pop_front();
      return true;
    }
    now += waitMs;
    return false;
  }
  void send(const uint8_t p[hid::kPacket]) override {
    Pkt x;
    std::memcpy(x.data(), p, hid::kPacket);
    out.emplace_back(now, x);
  }
  void wink() override { ++winks; }
  int64_t nowMs() override { return now; }
  int64_t unixTime() override { return 1700000000; }
};

std::vector<Pkt> frame(uint32_t cid, uint8_t cmd, const Bytes& data) {
  std::vector<Pkt> v;
  Pkt p{};
  p[0] = cid >> 24, p[1] = cid >> 16, p[2] = cid >> 8, p[3] = cid;
  p[4] = cmd | 0x80;
  p[5] = data.size() >> 8, p[6] = data.size() & 0xFF;
  size_t off = std::min(data.size(), hid::kInitData);
  std::memcpy(p.data() + 7, data.data(), off);
  v.push_back(p);
  for (uint8_t seq = 0; off < data.size(); ++seq) {
    Pkt c{};
    std::memcpy(c.data(), p.data(), 4);
    c[4] = seq;
    const size_t n = std::min(hid::kContData, data.size() - off);
    std::memcpy(c.data() + 5, data.data() + off, n);
    off += n;
    v.push_back(c);
  }
  return v;
}

struct Rig {
  FakeLink link;
  OpenSslCrypto crypto;
  MemStore store;
  MemCounter counter;
  MemAttestation attestation;
  TouchGate gate;
  Device dev{link, crypto, store, counter, attestation, gate, {0, 1, 0}};
  uint32_t cid = 0;

  Rig() {
    link.in.emplace_back(0, frame(hid::kBroadcast, hid::kInit, {1, 2, 3, 4, 5, 6, 7, 8})[0]);
    dev.step(0);
    const Pkt& r = link.out.back().second;
    cid = uint32_t(r[15]) << 24 | r[16] << 16 | r[17] << 8 | r[18];
    link.out.clear();
  }
  void send(int64_t at, uint32_t c, uint8_t cmd, const Bytes& data) {
    for (const auto& p : frame(c, cmd, data)) link.in.emplace_back(at, p);
  }
  // Runs until a non-keepalive answer for `c` arrives (or 40 simulated s pass).
  Bytes run(uint32_t c, uint8_t cmd, int* keepalives = nullptr, uint8_t* lastStatus = nullptr) {
    const int64_t stop = link.now + 40000;
    size_t seen = link.out.size();  // only answers produced from now on
    while (link.now < stop) {
      dev.step(10);
      for (; seen < link.out.size(); ++seen) {
        const Pkt& p = link.out[seen].second;
        const uint32_t pc = uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
        if (pc != c) continue;
        if (p[4] == (hid::kKeepalive | 0x80)) {
          if (keepalives) ++*keepalives;
          if (lastStatus) *lastStatus = p[7];
          continue;
        }
        if (p[4] == (cmd | 0x80) || p[4] == (hid::kError | 0x80)) {
          const size_t n = size_t(p[5]) << 8 | p[6];
          Bytes data(p.begin() + 7, p.begin() + 7 + std::min(n, hid::kInitData));
          for (size_t k = seen + 1; data.size() < n && k < link.out.size(); ++k)
            data.insert(data.end(), link.out[k].second.begin() + 5,
                        link.out[k].second.begin() + 5 + std::min(hid::kContData, n - data.size()));
          if (p[4] == (hid::kError | 0x80)) data.insert(data.begin(), 0xEE);  // marks a transport error
          return data;
        }
      }
    }
    return {};
  }
};

Bytes makeCredential() {
  cbor::Writer w;
  w.map(4);
  w.uint(1), w.bytes(Bytes(32, 7));
  w.uint(2), w.map(1), w.text("id"), w.text("example.com");
  w.uint(3), w.map(1), w.text("id"), w.bytes(Bytes{1});
  w.uint(4), w.array(1), w.map(2), w.text("alg"), w.integer(-7), w.text("type"), w.text("public-key");
  w.out.insert(w.out.begin(), 0x01);
  return w.out;
}

void pressAfterKeepalives() {
  Rig r;
  r.link.onTime = [&](int64_t now) {
    if (now >= 1100 && r.gate.awaiting()) r.gate.press(true, now);
  };
  r.send(150, r.cid, hid::kCbor, makeCredential());  // 2 packets
  int ka = 0;
  uint8_t st = 0;
  const Bytes resp = r.run(r.cid, hid::kCbor, &ka, &st);
  CHECK(!resp.empty() && resp[0] == 0x00);
  CHECK(st == hid::kUpNeeded);
  CHECK(ka >= 8 && ka <= 11);  // ~ every 100 ms for ~1 s
  CHECK(!r.gate.awaiting());
  // Keepalive spacing never exceeds ~100 ms.
  int64_t last = -1;
  for (const auto& [t, p] : r.link.out) {
    if (p[4] != (hid::kKeepalive | 0x80)) continue;
    if (last >= 0) CHECK(t - last <= 150);
    last = t;
  }
}

void cancelTimeoutRefuse() {
  {
    Rig r;
    r.send(150, r.cid, hid::kCbor, makeCredential());
    r.send(600, r.cid, hid::kCancel, {});
    const Bytes resp = r.run(r.cid, hid::kCbor);
    CHECK(resp == Bytes{0x2D});
    CHECK(r.link.now < 1000 && !r.gate.awaiting());
  }
  {
    Rig r;
    r.send(150, r.cid, hid::kCbor, makeCredential());
    const Bytes resp = r.run(r.cid, hid::kCbor);
    CHECK(resp == Bytes{0x2F});
    CHECK(r.link.now >= 150 + kPresenceTimeoutMs);
  }
  {
    Rig r;
    r.link.onTime = [&](int64_t now) {
      if (now >= 500 && r.gate.awaiting()) r.gate.press(false, now);
    };
    r.send(150, r.cid, hid::kCbor, makeCredential());
    CHECK(r.run(r.cid, hid::kCbor) == Bytes{0x27});
  }
}

void busyOtherChannelAndPing() {
  Rig r;
  // Second channel.
  r.link.in.emplace_back(120, frame(hid::kBroadcast, hid::kInit, {9, 9, 9, 9, 9, 9, 9, 9})[0]);
  r.dev.step(50);
  const Pkt& init = r.link.out.back().second;
  const uint32_t other = uint32_t(init[15]) << 24 | init[16] << 16 | init[17] << 8 | init[18];
  CHECK(other != r.cid);
  r.link.out.clear();
  r.link.onTime = [&](int64_t now) {
    if (now >= 2000 && r.gate.awaiting()) r.gate.press(true, now);
  };
  r.send(300, r.cid, hid::kCbor, makeCredential());
  r.send(800, other, hid::kPing, {1, 2, 3});
  const Bytes busy = r.run(other, hid::kPing);
  CHECK(busy == Bytes({0xEE, hid::kErrChannelBusy}));
  // The CBOR request on the first channel still completes (after the press).
  bool done = false;
  for (int i = 0; i < 1000 && !done; ++i) {
    r.dev.step(10);
    for (const auto& [t, p] : r.link.out)
      if (p[3] == (r.cid & 0xFF) && p[4] == (hid::kCbor | 0x80)) done = p[7] == 0x00;
  }
  CHECK(done);
  r.send(r.link.now + 10, other, hid::kPing, {1, 2, 3});
  CHECK(r.run(other, hid::kPing) == Bytes({1, 2, 3}));
  r.send(r.link.now + 10, other, hid::kWink, {});
  CHECK(r.run(other, hid::kWink).empty() == true && r.link.winks == 1);
}

void unlockWaitThenPress() {
  Rig r;
  r.store.open = false;
  r.link.onTime = [&](int64_t now) {
    if (now >= 2000) r.store.open = true;  // the phone unlocked Keyra
    if (now >= 3000 && r.gate.awaiting()) r.gate.press(true, now);
  };
  r.send(150, r.cid, hid::kCbor, makeCredential());
  bool sawProcessing = false;
  size_t seen = 0;
  Bytes resp;
  while (resp.empty() && r.link.now < 10000) {
    r.dev.step(10);
    for (; seen < r.link.out.size(); ++seen) {
      const Pkt& p = r.link.out[seen].second;
      if (p[4] == (hid::kKeepalive | 0x80) && p[7] == hid::kProcessing) sawProcessing = true;
      if (p[4] == (hid::kCbor | 0x80)) resp.assign(p.begin() + 7, p.begin() + 8);
    }
  }
  CHECK(sawProcessing);
  CHECK(resp == Bytes{0x00});
  CHECK(r.link.now >= 3000);
}

void u2fLatchThroughDevice() {
  Rig r;
  Bytes reg = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x40};
  reg.insert(reg.end(), 64, 0x42);
  r.send(150, r.cid, hid::kMsg, reg);
  CHECK(r.run(r.cid, hid::kMsg) == Bytes({0x69, 0x85}));
  CHECK(r.gate.awaiting());
  r.gate.press(true, r.link.now);
  CHECK(!r.gate.awaiting());  // LED back to normal; the press waits for the next poll
  r.send(r.link.now + 200, r.cid, hid::kMsg, reg);
  const Bytes ok = r.run(r.cid, hid::kMsg);
  CHECK(ok.size() > 2 && ok[0] == 0x05 && ok[ok.size() - 2] == 0x90 && ok.back() == 0x00);
  // The press was used up.
  r.send(r.link.now + 200, r.cid, hid::kMsg, reg);
  CHECK(r.run(r.cid, hid::kMsg) == Bytes({0x69, 0x85}));
}

void gateRules() {
  TouchGate g;
  CHECK(!g.awaiting());
  g.press(true, 0);  // nothing to approve
  CHECK(g.answer() == TouchGate::Answer::None);
  g.begin(TouchGate::Phase::Unlock);
  CHECK(g.awaiting());
  g.press(true, 10);  // short press cannot stand in for an unlock
  CHECK(g.answer() == TouchGate::Answer::None && g.awaiting());
  g.press(false, 20);
  CHECK(g.answer() == TouchGate::Answer::Denied && !g.awaiting());
  g.end();

  // U2F: the prompt lapses when the host stops polling; a stale press is not used.
  CHECK(!g.takeLatched(1000));
  CHECK(g.awaiting());
  g.tick(1000 + TouchGate::kPollGapMs + 1);
  CHECK(!g.awaiting());
  CHECK(!g.takeLatched(10000));
  g.press(true, 10000);
  CHECK(!g.takeLatched(10000 + TouchGate::kLatchMs + 1));  // too late: asks again instead
  CHECK(g.awaiting());
  g.press(true, 20000);
  CHECK(g.takeLatched(20100));
  CHECK(!g.awaiting());
  // A U2F poll never hijacks a blocking CTAP2 wait.
  g.begin(TouchGate::Phase::Presence);
  CHECK(!g.takeLatched(30000));
  g.press(true, 30001);
  CHECK(g.answer() == TouchGate::Answer::Approved);
  g.end();
}

}  // namespace

int main() {
  pressAfterKeepalives();
  cancelTimeoutRefuse();
  busyOtherChannelAndPing();
  unlockWaitThenPress();
  u2fLatchThroughDevice();
  gateRules();
  return KEYRA_TEST_RESULT();
}
