// Seams between the pure vault core and the platform (ESP-IDF on device,
// in-memory/OpenSSL in host tests). Kept to what the core actually needs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace keyra::vault {

// Raw file primitives. Paths are relative ("meta.bin", "e/0000002a.bin").
// Atomicity (tmp + rename) is done by the core on top of these so it can be
// tested with injected crashes.
class Storage {
 public:
  enum class Read { Ok, NotFound, Error };
  virtual ~Storage() = default;
  virtual bool mount() = 0;  // false: unusable (never formats a partition holding data)
  virtual Read read(const std::string& path, std::vector<uint8_t>& out) = 0;
  // Creates/truncates, writes, flushes and closes.
  virtual bool write(const std::string& path, const uint8_t* data, size_t n) = 0;
  // Must atomically replace `to` if it exists.
  virtual bool rename(const std::string& from, const std::string& to) = 0;
  virtual bool remove(const std::string& path) = 0;  // true if gone (incl. never existed)
  // Plain file names inside dir ("" = root, "e" = entries); missing dir → empty.
  virtual bool list(const std::string& dir, std::vector<std::string>& names) = 0;
  // Destroys every byte and leaves an empty, mounted filesystem. Factory reset only.
  virtual bool format() = 0;
};

// The persisted unlock-failure counter (survives reboots and factory-reset clears it).
class Counter {
 public:
  virtual ~Counter() = default;
  virtual bool load(uint32_t& value) = 0;  // absent → 0
  virtual bool store(uint32_t value) = 0;  // 0 may erase the key
};

enum class Hash { Sha1, Sha256, Sha512 };

class Crypto {
 public:
  enum class Open { Ok, AuthFailed, Error };
  virtual ~Crypto() = default;
  virtual bool random(uint8_t* out, size_t n) = 0;
  virtual bool pbkdf2Sha256(const std::string& pass, const uint8_t* salt, size_t saltLen,
                            uint32_t iterations, uint8_t* out, size_t outLen) = 0;
  // AES-256-GCM, 12-byte IV. out = ciphertext || 16-byte tag (n + 16 bytes).
  virtual bool gcmSeal(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad,
                       size_t aadLen, const uint8_t* in, size_t n, uint8_t* out) = 0;
  // in = ciphertext || tag (n >= 16); out receives n - 16 bytes.
  virtual Open gcmOpen(const uint8_t key[32], const uint8_t iv[12], const uint8_t* aad,
                       size_t aadLen, const uint8_t* in, size_t n, uint8_t* out) = 0;
  // out must hold 64 bytes; returns digest length via outLen.
  virtual bool hmac(Hash h, const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen,
                    uint8_t* out, size_t* outLen) = 0;
};

class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint64_t monotonicMs() = 0;  // since boot; never goes backwards
};

struct Platform {
  Storage& storage;
  Counter& counter;
  Crypto& crypto;
  Clock& clock;
};

}  // namespace keyra::vault
