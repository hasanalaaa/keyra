// Host definitions of the platform hooks: secure heap with wipe-before-free
// accounting, and the instance behind the public free functions.
#include <openssl/crypto.h>

#include <cstdlib>
#include <cstring>

#include "core/secure_buf.hpp"
#include "core/vault_core.hpp"
#include "host_fakes.hpp"

namespace keyra::vault {

namespace {
// Size header in front of each block so free() can verify the wipe.
constexpr size_t kHeader = 16;
size_t gLive = 0, gDirty = 0;
}  // namespace

void* mem::alloc(size_t n) {
  auto* p = static_cast<uint8_t*>(std::malloc(n + kHeader));
  if (!p) return nullptr;
  std::memcpy(p, &n, sizeof n);
  ++gLive;
  return p + kHeader;
}

void mem::free(void* q) {
  if (!q) return;
  auto* p = static_cast<uint8_t*>(q) - kHeader;
  size_t n;
  std::memcpy(&n, p, sizeof n);
  for (size_t i = 0; i < n; ++i)
    if (p[kHeader + i]) {
      ++gDirty;
      break;
    }
  --gLive;
  std::free(p);
}

void mem::zeroize(void* p, size_t n) { OPENSSL_cleanse(p, n); }

namespace test {
HeapStats heapStats() { return {gLive, gDirty}; }
}  // namespace test

namespace detail {
Vault& instance() {
  static test::MemStorage storage;
  static test::MemCounter counter;
  static test::OpenSslCrypto crypto;
  static test::FakeClock clock;
  static Vault vault(Platform{storage, counter, crypto, clock}, Vault::Options{1000});
  return vault;
}
}  // namespace detail

}  // namespace keyra::vault
