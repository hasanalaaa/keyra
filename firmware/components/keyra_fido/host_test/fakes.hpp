#pragma once
// In-memory Store / Counter and a scripted User for the CTAP tests.
#include <array>
#include <deque>
#include <functional>
#include <map>

#include "core/attest.hpp"
#include "core/platform.hpp"

namespace keyra::fido::test {

class MemStore final : public Store {
 public:
  bool open = true;
  uint8_t key[32] = {1, 2, 3};
  std::vector<std::array<uint8_t, 32>> restored;  // more keys, as after restoring a backup
  std::map<uint32_t, std::vector<uint8_t>> recs;
  uint32_t nextId = 1;

  bool unlocked() override { return open; }
  Result wrapKeys(WrapKeys& out) override {
    out.clear();
    if (!open) return Result::Locked;
    std::copy(key, key + 32, out.key[out.count++]);
    for (const auto& k : restored) std::copy(k.begin(), k.end(), out.key[out.count++]);
    return Result::Ok;
  }
  Result list(std::vector<Record>& out) override {
    out.clear();
    if (!open) return Result::Locked;
    for (auto& [id, d] : recs) out.push_back(Record{id, d});
    return Result::Ok;
  }
  Result put(uint32_t& id, const std::vector<uint8_t>& d) override {
    if (!open) return Result::Locked;
    if (id == 0) {
      if (recs.size() >= 50) return Result::Full;
      id = nextId++;
    } else if (!recs.count(id)) {
      return Result::NotFound;
    }
    recs[id] = d;
    return Result::Ok;
  }
  Result remove(uint32_t id) override { return recs.erase(id) ? Result::Ok : Result::NotFound; }
  Result reset() override {
    recs.clear();
    key[0] ^= 0xFF;  // new wrapping key
    return Result::Ok;
  }
};

class MemAttestation final : public AttestationStore {
 public:
  bool has = false;
  uint8_t priv[32] = {};
  std::vector<uint8_t> cert;
  int saves = 0;
  bool load(uint8_t out[32], std::vector<uint8_t>& c) override {
    if (!has) return false;
    std::copy(priv, priv + 32, out);
    c = cert;
    return true;
  }
  bool save(const uint8_t in[32], const std::vector<uint8_t>& c) override {
    std::copy(in, in + 32, priv);
    cert = c;
    has = true;
    ++saves;
    return true;
  }
};

class MemCounter final : public Counter {
 public:
  uint32_t value = 0;
  bool next(uint32_t& v) override {
    v = ++value;
    return true;
  }
};

class ScriptUser final : public User {
 public:
  std::deque<Answer> presence;  // empty → Approved
  Answer unlock = Answer::Approved;
  bool latched = true;          // takePresence() result
  int presenceCalls = 0, unlockCalls = 0, takeCalls = 0;
  int64_t uptime = 1000, unix = 1700000000;

  Answer waitUnlocked() override {
    ++unlockCalls;
    if (open && *open) return Answer::Approved;
    if (unlock == Answer::Approved && open) *open = true;
    return unlock;
  }
  std::function<void()> onPresence;  // runs while "waiting for the touch" (e.g. the vault locks)
  Answer waitPresence() override {
    ++presenceCalls;
    if (onPresence) onPresence();
    if (presence.empty()) return Answer::Approved;
    const Answer a = presence.front();
    presence.pop_front();
    return a;
  }
  bool takePresence() override {
    ++takeCalls;
    return latched;
  }
  int64_t uptimeMs() override { return uptime; }
  int64_t unixTime() override { return unix; }
  bool* open = nullptr;  // MemStore::open, flipped when the "user unlocks"
};

}  // namespace keyra::fido::test
