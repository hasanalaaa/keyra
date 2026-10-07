// The FIDO core as a process: 64-byte HID reports on stdin → reports on
// stdout. Passkeys go into the real vault core (host platform, in RAM),
// crypto is OpenSSL, the U2F attestation key is made on first use, and the "button" is pressed automatically
// KEYRA_HARNESS_PRESS_MS (default 300) after a request starts waiting, so
// hosts see real keepalives. tools/fido_harness.py drives it with python-fido2.
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "core/device.hpp"
#include "fakes.hpp"
#include "host_crypto.hpp"
#include "keyra/vault.hpp"
#include "vault_store.hpp"

using namespace keyra::fido;

namespace {

class StdioLink final : public Link {
 public:
  TouchGate& gate;
  int64_t pressMs;
  int64_t waitingSince = -1;
  explicit StdioLink(TouchGate& g, int64_t press) : gate(g), pressMs(press) {}

  bool recv(uint8_t p[hid::kPacket], uint32_t waitMs) override {
    autoPress();
    pollfd fd{0, POLLIN, 0};
    if (::poll(&fd, 1, static_cast<int>(waitMs)) <= 0) return false;
    size_t got = 0;
    while (got < hid::kPacket) {
      const ssize_t n = ::read(0, p + got, hid::kPacket - got);
      if (n <= 0) std::exit(0);  // host closed the pipe
      got += static_cast<size_t>(n);
    }
    return true;
  }
  void send(const uint8_t p[hid::kPacket]) override {
    std::fwrite(p, 1, hid::kPacket, stdout);
    std::fflush(stdout);
  }
  void wink() override { std::fprintf(stderr, "harness: wink\n"); }
  int64_t nowMs() override {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - start).count();
  }
  int64_t unixTime() override { return static_cast<int64_t>(std::time(nullptr)); }

 private:
  void autoPress() {
    if (!gate.awaiting()) {
      waitingSince = -1;
      return;
    }
    if (waitingSince < 0) waitingSince = nowMs();
    if (nowMs() - waitingSince >= pressMs) {
      gate.press(true, nowMs());
      waitingSince = -1;
    }
  }
};

class MemCounter final : public Counter {
 public:
  uint32_t v = 0;
  bool next(uint32_t& out) override {
    out = ++v;
    return true;
  }
};

}  // namespace

int main() {
  namespace vault = keyra::vault;
  if (vault::init() != vault::Status::Ok || vault::setup("harness passphrase") != vault::Status::Ok) {
    std::fprintf(stderr, "harness: vault setup failed\n");
    return 1;
  }
  const char* press = std::getenv("KEYRA_HARNESS_PRESS_MS");
  TouchGate gate;
  StdioLink link(gate, press ? std::atoll(press) : 300);
  test::OpenSslCrypto crypto;
  VaultStore store;
  MemCounter counter;
  test::MemAttestation attestation;  // a fresh attestation key per run
  Device dev(link, crypto, store, counter, attestation, gate, {0, 1, 0});
  for (;;) dev.step(50);
}
