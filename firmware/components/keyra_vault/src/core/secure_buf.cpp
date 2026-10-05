#include "secure_buf.hpp"

#include <cstring>

namespace keyra::vault {

bool SecureBuf::alloc(size_t n) {
  clear();
  if (n == 0) return true;
  p_ = static_cast<uint8_t*>(mem::alloc(n));
  if (!p_) return false;
  std::memset(p_, 0, n);
  n_ = n;
  return true;
}

void SecureBuf::clear() {
  if (p_) {
    mem::zeroize(p_, n_);
    mem::free(p_);
  }
  p_ = nullptr;
  n_ = 0;
}

}  // namespace keyra::vault
