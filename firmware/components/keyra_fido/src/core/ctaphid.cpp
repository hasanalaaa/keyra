#include "ctaphid.hpp"

#include <algorithm>
#include <cstring>

namespace keyra::fido::hid {
namespace {

uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}

}  // namespace

bool Ctaphid::feed(const uint8_t* pkt, size_t len, int64_t now, Message& out) {
  if (len != kPacket) return false;  // hosts always send full reports; anything else is noise
  const uint32_t cid = be32(pkt);
  const bool init = (pkt[4] & 0x80) != 0;
  if (cid == 0) {
    fail(cid, kErrInvalidChannel);
    return false;
  }

  if (!init) {
    // Continuation packet. Spurious ones (no reassembly on that channel) are ignored.
    if (!assembling_) return false;
    if (cid != asmCid_) {
      fail(cid, kErrChannelBusy);
      return false;
    }
    if (pkt[4] != seq_) {
      abortAssembly();
      fail(cid, kErrInvalidSeq);
      return false;
    }
    ++seq_;
    const size_t take = std::min(kContData, asmLen_ - buf_.size());
    buf_.insert(buf_.end(), pkt + 5, pkt + 5 + take);
    asmDeadline_ = now + kContinuationTimeoutMs;
    if (buf_.size() < asmLen_) return false;
    assembling_ = false;
    return dispatch(asmCid_, asmCmd_, now, out);
  }

  const uint8_t cmd = pkt[4] & 0x7F;
  const size_t bcnt = size_t(pkt[5]) << 8 | pkt[6];

  if (cmd == kInit) {
    // INIT is a single packet and may arrive at any time; on an allocated
    // channel it resynchronises it (dropping whatever that channel was doing).
    if (cid != kBroadcast && !allocated(cid)) {
      fail(cid, kErrInvalidChannel);
      return false;
    }
    if (assembling_ && asmCid_ == cid) abortAssembly();
    if (busy_ && busyCid_ == cid) cancelled_ = abandoned_ = true;
    if (bcnt != 8) {
      fail(cid, kErrInvalidLen);
      return false;
    }
    handleInit(cid, pkt + 7, bcnt);
    return false;
  }
  if (cid == kBroadcast || !allocated(cid)) {
    fail(cid, kErrInvalidChannel);
    return false;
  }
  if (cmd == kCancel) {
    if (busy_ && busyCid_ == cid) cancelled_ = true;
    return false;  // never answered
  }
  if (busy_) {
    fail(cid, kErrChannelBusy);
    return false;
  }
  if (assembling_) {
    if (cid == asmCid_) abortAssembly();
    fail(cid, cid == asmCid_ ? kErrInvalidSeq : kErrChannelBusy);
    return false;
  }
  if (bcnt > kMaxMessage) {
    fail(cid, kErrInvalidLen);
    return false;
  }
  buf_.assign(pkt + 7, pkt + 7 + std::min(bcnt, kInitData));
  if (bcnt <= kInitData) return dispatch(cid, cmd, now, out);
  assembling_ = true;
  asmCid_ = cid;
  asmCmd_ = cmd;
  asmLen_ = bcnt;
  seq_ = 0;
  asmDeadline_ = now + kContinuationTimeoutMs;
  buf_.reserve(bcnt);
  return false;
}

void Ctaphid::tick(int64_t now) {
  if (assembling_ && now >= asmDeadline_) {
    const uint32_t cid = asmCid_;
    abortAssembly();
    fail(cid, kErrMsgTimeout);
  }
}

bool Ctaphid::dispatch(uint32_t cid, uint8_t cmd, int64_t, Message& out) {
  switch (cmd) {
    case kPing:
      write(cid, kPing, buf_.data(), buf_.size());
      break;
    case kWink:
      if (!buf_.empty()) {
        fail(cid, kErrInvalidLen);
        break;
      }
      if (wink_) wink_();
      write(cid, kWink, nullptr, 0);
      break;
    case kMsg:
    case kCbor:
      if (buf_.empty()) {
        fail(cid, kErrInvalidLen);
        break;
      }
      out.cid = cid;
      out.cmd = cmd;
      out.data.swap(buf_);
      buf_.clear();
      return true;
    default:  // LOCK is optional and not offered; KEEPALIVE/ERROR only flow device → host
      fail(cid, kErrInvalidCmd);
      break;
  }
  buf_.clear();
  return false;
}

void Ctaphid::handleInit(uint32_t cid, const uint8_t* nonce, size_t n) {
  uint8_t r[17];
  std::memcpy(r, nonce, n);
  uint32_t assigned = cid;
  if (cid == kBroadcast) {
    if (next_ == kBroadcast) next_ = 1;  // 4 billion INITs since boot: wrap rather than fail
    assigned = next_++;
  }
  put32(r + 8, assigned);
  r[12] = 2;  // CTAPHID protocol version
  r[13] = v_.major;
  r[14] = v_.minor;
  r[15] = v_.build;
  r[16] = kCapWink | kCapCbor;  // MSG is supported, so no NMSG
  write(cid, kInit, r, sizeof r);
}

void Ctaphid::begin(uint32_t cid) {
  busy_ = true;
  busyCid_ = cid;
  cancelled_ = abandoned_ = false;
}

bool Ctaphid::takeCancel() {
  const bool c = cancelled_;
  cancelled_ = false;
  return c;
}

void Ctaphid::reply(uint32_t cid, uint8_t cmd, const uint8_t* data, size_t n) {
  const bool drop = busy_ && cid == busyCid_ && abandoned_;
  if (busy_ && cid == busyCid_) busy_ = cancelled_ = abandoned_ = false;
  // The host re-INITed the channel meanwhile: a late answer would be read as
  // the reply to its next request.
  if (!drop) write(cid, cmd, data, n);
}

void Ctaphid::fail(uint32_t cid, uint8_t code) { write(cid, kError, &code, 1); }

void Ctaphid::write(uint32_t cid, uint8_t cmd, const uint8_t* data, size_t n) {
  uint8_t pkt[kPacket] = {};
  put32(pkt, cid);
  pkt[4] = static_cast<uint8_t>(cmd | 0x80);
  pkt[5] = static_cast<uint8_t>(n >> 8);
  pkt[6] = static_cast<uint8_t>(n);
  size_t off = std::min(n, kInitData);
  if (off) std::memcpy(pkt + 7, data, off);
  send_(pkt);
  for (uint8_t seq = 0; off < n; ++seq) {
    std::memset(pkt, 0, sizeof pkt);
    put32(pkt, cid);
    pkt[4] = seq;
    const size_t take = std::min(kContData, n - off);
    std::memcpy(pkt + 5, data + off, take);
    off += take;
    send_(pkt);
  }
}

void Ctaphid::error(uint32_t cid, uint8_t code) { reply(cid, kError, &code, 1); }

void Ctaphid::keepalive(uint8_t status) {
  if (!busy_) return;
  uint8_t pkt[kPacket] = {};
  put32(pkt, busyCid_);
  pkt[4] = kKeepalive | 0x80;
  pkt[6] = 1;
  pkt[7] = status;
  send_(pkt);
}

}  // namespace keyra::fido::hid
