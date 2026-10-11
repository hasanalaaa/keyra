// ESP-IDF implementations of the vault platform seams.
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "core/platform.hpp"
#include "esp_partition.h"

namespace keyra::vault::esp {

// LittleFS on a data partition, mounted at `base`: "vault" at /vault, and on
// 16 MB boards the tail (flash_tail.cpp). The partition belongs to the vault:
// format() erases all of it. `dirs` are created at mount.
class LittleFsStorage : public Storage {
 public:
  LittleFsStorage(const char* label, const char* base, std::initializer_list<const char*> dirs)
      : label_(label), base_(base), dirs_(dirs) {}
  bool mount() override;
  Read read(const std::string& path, std::vector<uint8_t>& out) override;
  bool write(const std::string& path, const uint8_t* data, size_t n) override;
  bool rename(const std::string& from, const std::string& to) override;
  bool remove(const std::string& path) override;
  bool list(const std::string& dir, std::vector<std::string>& names) override;
  bool format() override;
  Failure lastFailure() const override { return lastFailure_; }

 private:
  std::string full(const std::string& path) const { return path.empty() ? base_ : base_ + "/" + path; }
  const esp_partition_t* partition() const;
  esp_err_t registerFs() const;

  const char* label_;
  std::string base_;
  std::vector<const char*> dirs_;
  bool mounted_ = false;
  Failure lastFailure_ = Failure::Other;
};

// The upper 8 MB of a flash of 16 MB or more, registered as a data partition
// labelled kTailLabel; null on smaller chips or when it cannot be used.
inline constexpr char kTailLabel[] = "vaulttail";
const esp_partition_t* registerTail();

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
  std::unique_ptr<Aead> gcmStream(const uint8_t key[32], const uint8_t iv[12], bool encrypt) override;
  bool hmac(Hash h, const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen,
            uint8_t* out, size_t* outLen) override;
};

class EspClock : public Clock {
 public:
  uint64_t monotonicMs() override;
};

}  // namespace keyra::vault::esp
