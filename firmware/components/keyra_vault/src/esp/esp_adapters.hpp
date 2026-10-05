// ESP-IDF implementations of the vault platform seams.
#pragma once

#include "core/platform.hpp"

namespace keyra::vault::esp {

// LittleFS on the data partition labelled "vault", mounted at /vault.
// The partition belongs to the vault: format() erases all of it.
class LittleFsStorage : public Storage {
 public:
  bool mount() override;
  Read read(const std::string& path, std::vector<uint8_t>& out) override;
  bool write(const std::string& path, const uint8_t* data, size_t n) override;
  bool rename(const std::string& from, const std::string& to) override;
  bool remove(const std::string& path) override;
  bool list(const std::string& dir, std::vector<std::string>& names) override;
  bool format() override;

 private:
  bool mounted_ = false;
};

// Unlock-failure counter in NVS namespace "keyra_v".
class NvsCounter : public Counter {
 public:
  bool load(uint32_t& value) override;
  bool store(uint32_t value) override;
};

// PSA Crypto API (TF-PSA-Crypto in ESP-IDF 6, hardware SHA/AES underneath).
class PsaCrypto : public Crypto {
 public:
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

class EspClock : public Clock {
 public:
  uint64_t monotonicMs() override;
};

}  // namespace keyra::vault::esp
