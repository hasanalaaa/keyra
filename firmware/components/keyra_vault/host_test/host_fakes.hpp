// Host stand-ins for the platform seams: in-memory files with power-cut
// injection, a RAM counter, a manual clock and OpenSSL 3 crypto.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/platform.hpp"

namespace keyra::vault::test {

class MemStorage : public Storage {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  bool mountOk = true;
  // Mutating operations (write/rename/remove/format) that succeed before a
  // simulated power cut; -1 = never. After the cut every call fails until reboot().
  int crashAfter = -1;
  bool dead = false;
  // Like crashAfter, but only that one operation fails (a transient I/O error).
  int failAfter = -1;

  void reboot() {
    dead = false;
    crashAfter = -1;
  }

  bool mount() override { return !dead && mountOk; }
  Read read(const std::string& path, std::vector<uint8_t>& out) override;
  bool write(const std::string& path, const uint8_t* data, size_t n) override;
  bool rename(const std::string& from, const std::string& to) override;
  bool remove(const std::string& path) override;
  bool list(const std::string& dir, std::vector<std::string>& names) override;
  bool format() override;

 private:
  bool step();
};

class MemCounter : public Counter {
 public:
  uint32_t value = 0;
  bool failStore = false;
  bool load(uint32_t& v) override {
    v = value;
    return true;
  }
  bool store(uint32_t v) override {
    if (failStore) return false;
    value = v;
    return true;
  }
};

class FakeClock : public Clock {
 public:
  uint64_t now = 1000;
  uint64_t monotonicMs() override { return now; }
};

class OpenSslCrypto : public Crypto {
 public:
  int pbkdf2Calls = 0;
  std::function<void()> onPbkdf2;  // observe state at the moment the KDF starts

  bool random(uint8_t* out, size_t n) override;
  bool pbkdf2Sha256(const std::string& pass, const uint8_t* salt, size_t saltLen,
                    uint32_t iterations, uint8_t* out, size_t outLen) override;
  bool gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
               const uint8_t* in, size_t n, uint8_t* out) override;
  Open gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad, size_t aadLen,
               const uint8_t* in, size_t n, uint8_t* out) override;
  bool hmac(Hash h, const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen,
            uint8_t* out, size_t* outLen) override;
};

// Host secure-heap accounting (host_platform.cpp): every block must be all
// zeros when freed, which proves SecureBuf/ZeroingAllocator wipe before free.
struct HeapStats {
  size_t liveBlocks;
  size_t dirtyFrees;  // blocks freed with non-zero bytes (must stay 0)
};
HeapStats heapStats();

}  // namespace keyra::vault::test
