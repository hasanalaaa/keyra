#pragma once
// CTAPHID framing (CTAP 2.1 §11.2): 64-byte reports, channel allocation,
// message reassembly and the transport-level commands (INIT, PING, WINK,
// CANCEL, errors). MSG and CBOR requests are handed to the caller, which
// marks the channel busy while it works and answers with reply().
// Pure C++, single-threaded: the owner feeds packets from one task.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace keyra::fido::hid {

constexpr size_t kPacket = 64;
constexpr size_t kInitData = kPacket - 7;  // CID(4) CMD(1) BCNT(2)
constexpr size_t kContData = kPacket - 5;  // CID(4) SEQ(1)
constexpr size_t kMaxMessage = kInitData + 128 * kContData;  // 7609
constexpr int64_t kContinuationTimeoutMs = 750;
constexpr uint32_t kBroadcast = 0xFFFFFFFF;

enum Cmd : uint8_t {
  kPing = 0x01, kMsg = 0x03, kLock = 0x04, kInit = 0x06, kWink = 0x08,
  kCbor = 0x10, kCancel = 0x11, kKeepalive = 0x3B, kError = 0x3F,
};
enum Err : uint8_t {
  kErrInvalidCmd = 0x01, kErrInvalidPar = 0x02, kErrInvalidLen = 0x03, kErrInvalidSeq = 0x04,
  kErrMsgTimeout = 0x05, kErrChannelBusy = 0x06, kErrInvalidChannel = 0x0B, kErrOther = 0x7F,
};
enum Keepalive : uint8_t { kProcessing = 1, kUpNeeded = 2 };

constexpr uint8_t kCapWink = 0x01, kCapCbor = 0x04;

struct Message {
  uint32_t cid = 0;
  uint8_t cmd = 0;  // kMsg or kCbor
  std::vector<uint8_t> data;
};

class Ctaphid {
 public:
  using Send = std::function<void(const uint8_t* packet)>;  // always kPacket bytes
  struct Version { uint8_t major, minor, build; };

  Ctaphid(Send send, Version v, std::function<void()> wink) : send_(std::move(send)), v_(v), wink_(std::move(wink)) {}

  // Feeds one received report. True when `out` holds a complete MSG/CBOR
  // request; the caller then calls begin(out.cid) … reply(). While a request
  // is in progress no further request is returned: other channels get
  // CHANNEL_BUSY, a CANCEL on the busy channel sets cancelled().
  bool feed(const uint8_t* packet, size_t len, int64_t nowMs, Message& out);
  // Times out a stalled reassembly (ERR_MSG_TIMEOUT).
  void tick(int64_t nowMs);

  void begin(uint32_t cid);  // request in progress on cid
  bool busy() const { return busy_; }
  // True once if the host cancelled (CANCEL, or INIT on that channel) the request in progress.
  bool takeCancel();
  void reply(uint32_t cid, uint8_t cmd, const uint8_t* data, size_t n);  // also ends the request
  void error(uint32_t cid, uint8_t code);
  void keepalive(uint8_t status);  // on the busy channel

 private:
  bool allocated(uint32_t cid) const { return cid != 0 && cid != kBroadcast && cid < next_; }
  bool dispatch(uint32_t cid, uint8_t cmd, int64_t now, Message& out);
  void handleInit(uint32_t cid, const uint8_t* nonce, size_t n);
  void write(uint32_t cid, uint8_t cmd, const uint8_t* data, size_t n);  // frames, no state change
  void fail(uint32_t cid, uint8_t code);  // transport error, no state change
  void abortAssembly() { assembling_ = false, buf_.clear(); }

  Send send_;
  Version v_;
  std::function<void()> wink_;
  uint32_t next_ = 1;  // channels 1 .. next_-1 are allocated (sequentially, from boot)
  // Reassembly
  bool assembling_ = false;
  uint32_t asmCid_ = 0;
  uint8_t asmCmd_ = 0;
  size_t asmLen_ = 0;
  uint8_t seq_ = 0;
  int64_t asmDeadline_ = 0;
  std::vector<uint8_t> buf_;
  // Request in progress
  bool busy_ = false;
  uint32_t busyCid_ = 0;
  bool cancelled_ = false;
  bool abandoned_ = false;  // INIT on the busy channel: drop its eventual reply
};

}  // namespace keyra::fido::hid
