#pragma once
// The global signature counter (docs/FIDO.md "Counter"). NVS holds the last
// value issued; the vault holds a reservation R (Store::counterReserved) that
// is raised, a block at a time, before NVS may pass it. While NVS is intact
// its value lies in the newest block (R - kCounterBlock < nvs ≤ R). A lower
// NVS value — the partition was erased, or a restore raised R past it — is not
// trusted and the counter carries on from R, so it never goes back.
#include <cstdint>

#include "platform.hpp"

namespace keyra::fido {

// One vault write per this many signatures (keyra::vault::kFidoCounterBlock).
constexpr uint32_t kCounterBlock = 1024;

// The last value issued, from NVS's copy and the reservation (0 = none yet).
uint32_t lastIssued(uint32_t nvs, uint32_t reserved);

// Where NVS keeps its copy (absent → 0).
class CounterCell {
 public:
  virtual ~CounterCell() = default;
  virtual bool load(uint32_t& value) = 0;
  virtual bool store(uint32_t value) = 0;
};

// Counter over NVS + the vault reservation. Not thread-safe: the device wraps
// it in a mutex. Every call needs the vault unlocked.
class ReservedCounter final : public Counter {
 public:
  ReservedCounter(CounterCell& nvs, Store& store) : nvs_(nvs), store_(store) {}
  // Reserves first when the next value is past the reservation, then saves it to NVS.
  bool next(uint32_t& value) override;
  bool last(uint32_t& value);  // for backups
  // At unlock: a reservation that covers the last value, for a Keyra that has
  // none yet (updated from a version without it) or whose NVS ran past it.
  bool secure();

 private:
  bool read(uint32_t& nvs, uint32_t& reserved);
  bool reserveFrom(uint32_t value, uint32_t reserved);  // R ≥ value + kCounterBlock - 1
  CounterCell& nvs_;
  Store& store_;
};

}  // namespace keyra::fido
