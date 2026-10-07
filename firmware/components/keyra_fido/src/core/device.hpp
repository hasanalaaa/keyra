#pragma once
// The FIDO task's loop: packets in → CTAPHID → CTAP2/U2F → packets out. While
// a request waits for the person it keeps pumping packets (so a CANCEL or a
// second channel's INIT is answered) and sends KEEPALIVE every 100 ms.
#include <cstdint>
#include <functional>

#include "ctap.hpp"
#include "ctaphid.hpp"
#include "platform.hpp"
#include "touch.hpp"

namespace keyra::fido {

constexpr int64_t kPresenceTimeoutMs = 30000;
constexpr int64_t kKeepaliveMs = 100;

class Link {
 public:
  virtual ~Link() = default;
  // Waits up to waitMs for one host report (64 bytes); false on timeout.
  virtual bool recv(uint8_t packet[hid::kPacket], uint32_t waitMs) = 0;
  virtual void send(const uint8_t packet[hid::kPacket]) = 0;
  virtual void wink() = 0;
  virtual int64_t nowMs() = 0;  // monotonic, since boot
  virtual int64_t unixTime() = 0;  // seconds, 0 = unknown
};

class Device final : public User {
 public:
  Device(Link& link, Crypto& c, Store& s, Counter& n, AttestationStore& a, TouchGate& gate,
         hid::Ctaphid::Version v);

  void step(uint32_t waitMs);  // handle at most one incoming report

  // User
  Answer waitUnlocked() override;
  Answer waitPresence() override;
  bool takePresence() override;
  int64_t uptimeMs() override { return link_.nowMs(); }
  int64_t unixTime() override { return link_.unixTime(); }

 private:
  void pump(uint32_t waitMs);  // while a request is in progress
  Answer wait(TouchGate::Phase phase, uint8_t keepalive, const std::function<bool()>& done);

  Link& link_;
  Store& store_;
  TouchGate& gate_;
  hid::Ctaphid hid_;
  Authenticator auth_;
};

}  // namespace keyra::fido
