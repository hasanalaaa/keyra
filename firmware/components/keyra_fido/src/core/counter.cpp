#include "counter.hpp"

namespace keyra::fido {

uint32_t lastIssued(uint32_t nvs, uint32_t reserved) {
  if (reserved == 0) return nvs;  // no reservation yet: NVS is all there is
  return int64_t(nvs) > int64_t(reserved) - int64_t(kCounterBlock) ? nvs : reserved;
}

bool ReservedCounter::read(uint32_t& nvs, uint32_t& reserved) {
  return nvs_.load(nvs) && store_.counterReserved(reserved) == Store::Result::Ok;
}

// value - 1 = R - kCounterBlock < value: NVS holding `value` is inside the block.
bool ReservedCounter::reserveFrom(uint32_t value, uint32_t reserved) {
  const uint64_t want = uint64_t(value) + kCounterBlock - 1;
  const uint32_t r = want > UINT32_MAX ? UINT32_MAX : uint32_t(want);
  return r <= reserved || store_.reserveCounter(r) == Store::Result::Ok;
}

bool ReservedCounter::next(uint32_t& value) {
  uint32_t nvs = 0, reserved = 0;
  if (!read(nvs, reserved)) return false;
  const uint32_t last = lastIssued(nvs, reserved);
  if (last == UINT32_MAX) return false;  // never wraps
  const uint32_t v = last + 1;
  if (v > reserved && !reserveFrom(v, reserved)) return false;
  if (!nvs_.store(v)) return false;
  value = v;
  return true;
}

bool ReservedCounter::last(uint32_t& value) {
  uint32_t nvs = 0, reserved = 0;
  if (!read(nvs, reserved)) return false;
  value = lastIssued(nvs, reserved);
  return true;
}

bool ReservedCounter::secure() {
  uint32_t nvs = 0, reserved = 0;
  if (!read(nvs, reserved)) return false;
  const uint32_t last = lastIssued(nvs, reserved);
  if (reserved != 0 && last <= reserved) return true;
  return reserveFrom(last, reserved);
}

}  // namespace keyra::fido
