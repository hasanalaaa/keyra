// The vault instance behind vault::* over storage the test can fill and dump
// (the components' host_platform.cpp hides its storage in a function). Only
// keyra_vault internals here: see flash() in common.hpp.
#include <openssl/crypto.h>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "core/secure_buf.hpp"
#include "core/vault_core.hpp"
#include "host_fakes.hpp"

namespace keyra::vault {
namespace {
test::MemStorage g_storage;
test::MemCounter g_failures;
}  // namespace

void* mem::alloc(size_t n) { return std::calloc(1, n ? n : 1); }
void mem::free(void* p) { std::free(p); }
void mem::zeroize(void* p, size_t n) { OPENSSL_cleanse(p, n); }

namespace detail {
Vault& instance() {
  static test::OpenSslCrypto crypto;
  static test::FakeClock clock;
  // Iterations only matter for a new setup; an existing vault keeps what its meta says.
  static Vault vault(Platform{g_storage, g_failures, crypto, clock}, Vault::Options{1000});
  return vault;
}
}  // namespace detail

}  // namespace keyra::vault

namespace upgrade {
std::map<std::string, std::vector<uint8_t>>& flash() { return keyra::vault::g_storage.files; }
uint32_t& vaultFailures() { return keyra::vault::g_failures.value; }
}  // namespace upgrade
