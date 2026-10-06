#pragma once
// Trusted browsers (SPEC §8.2): a browser that reaches Keyra through the home
// network must be approved once with the button before it can unlock. Only a
// SHA-256 of each browser's `kt` cookie token is kept. Plain C++, host-tested;
// hashing, randomness and NVS live in trusted.cpp.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keyra::api::trust {

constexpr size_t kMaxBrowsers = 8;
constexpr size_t kMaxName = 32;
using Digest = std::array<uint8_t, 32>;

struct Browser {
  uint32_t id = 0;  // random, non-zero; the {id} of DELETE /api/trusted/{id}
  Digest hash{};
  std::string name;
  int64_t created = 0, lastSeen = 0;  // unix seconds, 0 = clock not set
};

// Unlock gate: the device AP is the physical-proximity channel and never asks;
// the home network asks unless the browser presents a known token.
inline bool needsApproval(bool viaHome, bool knownBrowser) { return viaHome && !knownBrowser; }

class Store {
 public:
  const std::vector<Browser>& all() const { return list_; }
  // Index of the browser with this token hash. Compares against every record so
  // timing does not reveal which one matched.
  std::optional<size_t> find(const Digest& hash) const;
  // Adds a browser; when 8 are already trusted the least recently seen one is
  // dropped and its id returned (its sessions must end too), else 0.
  uint32_t add(const Browser& b);
  bool remove(uint32_t id);
  bool contains(uint32_t id) const;
  void touch(size_t index, int64_t now);

  // Versioned NVS blob. parse() rejects anything malformed rather than guessing.
  std::vector<uint8_t> serialize() const;
  static std::optional<Store> parse(const uint8_t* data, size_t len);

 private:
  std::vector<Browser> list_;
};

// "Safari on iPhone"-style label from a User-Agent; printable ASCII, ≤ kMaxName.
std::string browserName(std::string_view userAgent);

}  // namespace keyra::api::trust
