// The signature counter over NVS + the vault reservation (core/counter.hpp):
// it never goes back, whether NVS is erased, a restore raised the reservation,
// or the Keyra was updated from a version that kept the counter in NVS only.
#include <cstdint>

#include "core/counter.hpp"
#include "fakes.hpp"
#include "keyra_test.hpp"

using namespace keyra::fido;
using namespace keyra::fido::test;

namespace {

struct Rig {
  MemCell nvs;
  MemStore store;
  ReservedCounter counter{nvs, store};
  uint32_t next() {
    uint32_t v = 0;
    CHECK(counter.next(v));
    return v;
  }
};

// 3ce814a kept 1007 in NVS and nothing in the vault.
void updatedKeyraCarriesOn() {
  Rig r;
  r.nvs.value = 1007;
  CHECK(r.next() == 1008);
  CHECK(r.store.reserved == 1008 + kCounterBlock - 1 && r.store.reserveWrites == 1);
  for (uint32_t want = 1009; want < 1008 + kCounterBlock; ++want) CHECK(r.next() == want);
  CHECK(r.store.reserveWrites == 1);  // one vault write for the whole block
  CHECK(r.next() == 1008 + kCounterBlock);
  CHECK(r.store.reserveWrites == 2);
}

// The bug: an erased NVS partition read as 0 and the counter started again.
void erasedNvsNeverGoesBack() {
  Rig r;
  r.nvs.value = 1007;
  uint32_t last = 0;
  for (int i = 0; i < 5; ++i) last = r.next();
  r.nvs.erase();
  const uint32_t after = r.next();
  CHECK(after > last);
  CHECK(after == r.store.reserved - kCounterBlock + 1);  // carried on from the reservation
  CHECK(r.next() == after + 1);                           // and NVS is trusted again
  r.nvs.erase();
  CHECK(r.next() > after + 1);
}

// Unlock reserves for a Keyra that has signed only with NVS so far, before NVS can be lost.
void secureAtUnlock() {
  Rig r;
  r.nvs.value = 1013;
  CHECK(r.counter.secure());
  CHECK(r.store.reserved >= 1013);
  CHECK(r.counter.secure() && r.store.reserveWrites == 1);  // nothing more to do
  uint32_t last = 0;
  CHECK(r.counter.last(last) && last == 1013);
  r.nvs.erase();
  CHECK(r.next() > 1013);

  Rig fresh;  // never signed: still reserves, and an erase then changes nothing
  CHECK(fresh.counter.secure());
  fresh.nvs.erase();
  CHECK(fresh.next() == 1);
}

// A restore raises the reservation past the backup's counter (the vault does
// it before writing the passkeys): the next signature is past it, whatever NVS says.
void restoreRaisesIt() {
  Rig r;
  r.nvs.value = 40;
  CHECK(r.next() == 41);
  CHECK(r.store.reserveCounter(5000 + 1000 + kCounterBlock) == Store::Result::Ok);
  const uint32_t v = r.next();
  CHECK(v > 6000);
  uint32_t last = 0;
  CHECK(r.counter.last(last) && last == v);

  Rig ahead;  // a local counter already past the backup's keeps going from itself
  ahead.nvs.value = 90000;
  CHECK(ahead.next() == 90001);
  CHECK(ahead.store.reserveCounter(500 + 1000 + kCounterBlock) == Store::Result::Ok);
  CHECK(ahead.next() == 90002);
}

void failuresSignNothing() {
  Rig r;
  r.store.failReserve = true;
  uint32_t v = 0;
  CHECK(!r.counter.next(v));
  CHECK(r.nvs.value == 0);  // NVS never runs past what the vault holds
  r.store.failReserve = false;
  r.nvs.failStore = true;
  CHECK(!r.counter.next(v));
  r.nvs.failStore = false;
  r.store.open = false;  // locked
  CHECK(!r.counter.next(v) && !r.counter.secure());
  r.store.open = true;
  r.nvs.value = UINT32_MAX;  // never wraps
  CHECK(!r.counter.next(v));
}

void lastIssuedRule() {
  CHECK(lastIssued(7, 0) == 7);
  CHECK(lastIssued(0, 2047) == 2047);
  CHECK(lastIssued(1024, 2047) == 1024);
  CHECK(lastIssued(1023, 2047) == 2047);
  CHECK(lastIssued(0, kCounterBlock - 1) == 0);  // reserved before anything was signed
}

}  // namespace

int main() {
  updatedKeyraCarriesOn();
  erasedNvsNeverGoesBack();
  secureAtUnlock();
  restoreRaisesIt();
  failuresSignNothing();
  lastIssuedRule();
  return KEYRA_TEST_RESULT();
}
