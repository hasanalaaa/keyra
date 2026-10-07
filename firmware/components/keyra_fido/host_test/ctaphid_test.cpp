// CTAPHID framing: exact packets for INIT/PING/WINK/errors, reassembly,
// sequence and timeout errors, channel rules, busy handling and CANCEL.
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "core/ctaphid.hpp"
#include "keyra_test.hpp"

using namespace keyra::fido::hid;
using Pkt = std::array<uint8_t, kPacket>;

namespace {

struct Rig {
  std::vector<Pkt> sent;
  int winks = 0;
  Ctaphid h{[this](const uint8_t* p) {
              Pkt x;
              std::memcpy(x.data(), p, kPacket);
              sent.push_back(x);
            },
            {1, 2, 3}, [this] { ++winks; }};
  int64_t now = 0;
  Message m;

  bool feed(const Pkt& p) { return h.feed(p.data(), p.size(), now, m); }
};

Pkt initPkt(uint32_t cid, uint8_t cmd, const std::vector<uint8_t>& data, size_t bcnt = SIZE_MAX) {
  Pkt p{};
  p[0] = cid >> 24, p[1] = cid >> 16, p[2] = cid >> 8, p[3] = cid;
  p[4] = cmd | 0x80;
  const size_t n = bcnt == SIZE_MAX ? data.size() : bcnt;
  p[5] = n >> 8, p[6] = n & 0xFF;
  std::memcpy(p.data() + 7, data.data(), std::min(data.size(), kInitData));
  return p;
}

Pkt contPkt(uint32_t cid, uint8_t seq, const uint8_t* data, size_t n) {
  Pkt p{};
  p[0] = cid >> 24, p[1] = cid >> 16, p[2] = cid >> 8, p[3] = cid;
  p[4] = seq;
  std::memcpy(p.data() + 5, data, n);
  return p;
}

uint32_t cidOf(const Pkt& p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool isError(const Pkt& p, uint32_t cid, uint8_t code) {
  return cidOf(p) == cid && p[4] == (kError | 0x80) && p[5] == 0 && p[6] == 1 && p[7] == code;
}

uint32_t allocate(Rig& r) {
  const std::vector<uint8_t> nonce = {1, 2, 3, 4, 5, 6, 7, 8};
  r.sent.clear();
  r.feed(initPkt(kBroadcast, kInit, nonce));
  CHECK_EQ(r.sent.size(), 1u);
  const Pkt p = r.sent[0];
  CHECK_EQ(cidOf(p), kBroadcast);
  CHECK_EQ(p[4], kInit | 0x80);
  CHECK_EQ(p[6], 17);
  CHECK(std::memcmp(p.data() + 7, nonce.data(), 8) == 0);
  CHECK_EQ(p[19], 2);                        // protocol version
  CHECK(p[20] == 1 && p[21] == 2 && p[22] == 3);  // device version
  CHECK_EQ(p[23], kCapWink | kCapCbor);
  r.sent.clear();
  return uint32_t(p[15]) << 24 | p[16] << 16 | p[17] << 8 | p[18];
}

void initAllocatesDistinctChannels() {
  Rig r;
  const uint32_t a = allocate(r), b = allocate(r);
  CHECK(a != 0 && a != kBroadcast && b != a);
  // INIT on an allocated channel keeps it.
  r.feed(initPkt(a, kInit, {9, 9, 9, 9, 9, 9, 9, 9}));
  CHECK(r.sent.size() == 1 && cidOf(r.sent[0]) == a);
  CHECK(r.sent[0][15] == (a >> 24) && r.sent[0][18] == (a & 0xFF));
  // Wrong INIT length.
  r.sent.clear();
  r.feed(initPkt(kBroadcast, kInit, {1, 2, 3}));
  CHECK(r.sent.size() == 1 && isError(r.sent[0], kBroadcast, kErrInvalidLen));
}

void channelRules() {
  Rig r;
  r.feed(initPkt(0, kPing, {1}));
  CHECK(r.sent.size() == 1 && isError(r.sent[0], 0, kErrInvalidChannel));
  r.sent.clear();
  r.feed(initPkt(0x12345678, kPing, {1}));  // never allocated
  CHECK(r.sent.size() == 1 && isError(r.sent[0], 0x12345678, kErrInvalidChannel));
  r.sent.clear();
  r.feed(initPkt(kBroadcast, kPing, {1}));  // broadcast is for INIT only
  CHECK(r.sent.size() == 1 && isError(r.sent[0], kBroadcast, kErrInvalidChannel));
  const uint32_t c = allocate(r);
  r.feed(initPkt(c, kLock, {5}));
  CHECK(r.sent.size() == 1 && isError(r.sent[0], c, kErrInvalidCmd));
  r.sent.clear();
  r.feed(initPkt(c, 0x55, {}));
  CHECK(r.sent.size() == 1 && isError(r.sent[0], c, kErrInvalidCmd));
  r.sent.clear();
  r.feed(initPkt(c, kCbor, {}));  // CBOR needs a command byte
  CHECK(r.sent.size() == 1 && isError(r.sent[0], c, kErrInvalidLen));
  r.sent.clear();
  r.feed(initPkt(c, kPing, {}, kMaxMessage + 1));
  CHECK(r.sent.size() == 1 && isError(r.sent[0], c, kErrInvalidLen));
  // Reports that are not 64 bytes are ignored.
  r.sent.clear();
  const Pkt p = initPkt(c, kPing, {1});
  CHECK(!r.h.feed(p.data(), 63, 0, r.m) && r.sent.empty());
}

void pingReassemblyAndFraming() {
  Rig r;
  const uint32_t c = allocate(r);
  std::vector<uint8_t> data(200);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
  CHECK(!r.feed(initPkt(c, kPing, data)));
  CHECK(r.sent.empty());
  size_t off = kInitData;
  for (uint8_t seq = 0; off < data.size(); ++seq) {
    const size_t n = std::min(kContData, data.size() - off);
    r.feed(contPkt(c, seq, data.data() + off, n));
    off += n;
  }
  // 200 = 57 + 59 + 59 + 25 → 4 packets back, same framing
  CHECK_EQ(r.sent.size(), 4u);
  std::vector<uint8_t> echo;
  CHECK(r.sent[0][4] == (kPing | 0x80) && r.sent[0][5] == 0 && r.sent[0][6] == 200);
  echo.insert(echo.end(), r.sent[0].begin() + 7, r.sent[0].end());
  for (size_t i = 1; i < 4; ++i) {
    CHECK(cidOf(r.sent[i]) == c && r.sent[i][4] == i - 1);
    echo.insert(echo.end(), r.sent[i].begin() + 5, r.sent[i].end());
  }
  echo.resize(200);
  CHECK(echo == data);
  CHECK(std::all_of(r.sent[3].begin() + 5 + 25, r.sent[3].end(), [](uint8_t b) { return b == 0; }));
}

void sequenceTimeoutAndBusy() {
  Rig r;
  const uint32_t c = allocate(r), other = allocate(r);
  std::vector<uint8_t> data(100, 0xAB);
  r.feed(initPkt(c, kCbor, data));
  r.feed(contPkt(c, 1, data.data(), 43));  // seq 1 instead of 0
  CHECK(r.sent.size() == 1 && isError(r.sent[0], c, kErrInvalidSeq));
  r.sent.clear();
  // Continuation with nothing to continue: ignored.
  r.feed(contPkt(c, 0, data.data(), 43));
  CHECK(r.sent.empty());

  // Another channel while reassembling → busy; timeout after 750 ms.
  r.now = 1000;
  r.feed(initPkt(c, kCbor, data));
  r.feed(initPkt(other, kPing, {1}));
  CHECK(r.sent.size() == 1 && isError(r.sent[0], other, kErrChannelBusy));
  r.sent.clear();
  r.h.tick(1000 + kContinuationTimeoutMs - 1);
  CHECK(r.sent.empty());
  r.h.tick(1000 + kContinuationTimeoutMs);
  CHECK(r.sent.size() == 1 && isError(r.sent[0], c, kErrMsgTimeout));
  r.sent.clear();

  // A complete CBOR request is handed out; while busy, others get CHANNEL_BUSY.
  CHECK(r.feed(initPkt(c, kCbor, {0x04})));
  CHECK(r.m.cid == c && r.m.cmd == kCbor && r.m.data == std::vector<uint8_t>{0x04});
  r.h.begin(c);
  CHECK(!r.feed(initPkt(other, kCbor, {0x04})));
  CHECK(!r.feed(initPkt(c, kCbor, {0x04})));
  CHECK(r.sent.size() == 2 && isError(r.sent[0], other, kErrChannelBusy) && isError(r.sent[1], c, kErrChannelBusy));
  CHECK(r.h.busy());
  r.sent.clear();
  // Keepalive goes to the busy channel.
  r.h.keepalive(kUpNeeded);
  CHECK(r.sent.size() == 1 && cidOf(r.sent[0]) == c && r.sent[0][4] == (kKeepalive | 0x80) && r.sent[0][7] == 2);
  r.sent.clear();
  // CANCEL from another channel is ignored, from the busy one it registers; never answered.
  r.feed(initPkt(other, kCancel, {}));
  CHECK(!r.h.takeCancel());
  r.feed(initPkt(c, kCancel, {}));
  CHECK(r.sent.empty());
  CHECK(r.h.takeCancel() && !r.h.takeCancel());
  const uint8_t resp[] = {0x2D};
  r.h.reply(c, kCbor, resp, 1);
  CHECK(!r.h.busy() && r.sent.size() == 1 && r.sent[0][7] == 0x2D);
}

void initResyncDropsBusyReply() {
  Rig r;
  const uint32_t c = allocate(r);
  CHECK(r.feed(initPkt(c, kCbor, {0x04})));
  r.h.begin(c);
  r.feed(initPkt(c, kInit, {1, 1, 1, 1, 1, 1, 1, 1}));
  CHECK(r.sent.size() == 1 && r.sent[0][4] == (kInit | 0x80));
  CHECK(r.h.takeCancel());
  r.sent.clear();
  const uint8_t resp[] = {0x00};
  r.h.reply(c, kCbor, resp, 1);  // stale: must not reach the host
  CHECK(r.sent.empty() && !r.h.busy());
}

void winkAndMsg() {
  Rig r;
  const uint32_t c = allocate(r);
  r.feed(initPkt(c, kWink, {}));
  CHECK(r.winks == 1 && r.sent.size() == 1 && r.sent[0][4] == (kWink | 0x80) && r.sent[0][6] == 0);
  r.sent.clear();
  r.feed(initPkt(c, kWink, {1}));
  CHECK(r.winks == 1 && isError(r.sent[0], c, kErrInvalidLen));
  CHECK(r.feed(initPkt(c, kMsg, {0, 3, 0, 0})) && r.m.cmd == kMsg && r.m.data.size() == 4);
}

}  // namespace

int main() {
  initAllocatesDistinctChannels();
  channelRules();
  pingReassemblyAndFraming();
  sequenceTimeoutAndBusy();
  initResyncDropsBusyReply();
  winkAndMsg();
  return KEYRA_TEST_RESULT();
}
