// Owned byte buffer for secrets: allocated from the platform's preferred heap
// (PSRAM when present on device) and zeroised before it is freed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace keyra::vault {

// Defined per platform (esp/mem_esp.cpp, host_test/host_platform.cpp).
namespace mem {
void* alloc(size_t n);  // nullptr on exhaustion
void free(void* p);
void zeroize(void* p, size_t n);  // must not be optimised away
}  // namespace mem

class SecureBuf {
 public:
  SecureBuf() = default;
  ~SecureBuf() { clear(); }
  SecureBuf(const SecureBuf&) = delete;
  SecureBuf& operator=(const SecureBuf&) = delete;
  SecureBuf(SecureBuf&& o) noexcept : p_(o.p_), n_(o.n_) { o.p_ = nullptr, o.n_ = 0; }
  SecureBuf& operator=(SecureBuf&& o) noexcept {
    if (this != &o) {
      clear();
      p_ = o.p_, n_ = o.n_;
      o.p_ = nullptr, o.n_ = 0;
    }
    return *this;
  }

  // Replaces any previous content with n zero bytes. False when out of memory.
  bool alloc(size_t n);
  void clear();
  uint8_t* data() { return p_; }
  const uint8_t* data() const { return p_; }
  size_t size() const { return n_; }

 private:
  uint8_t* p_ = nullptr;
  size_t n_ = 0;
};

// For growable plaintext (backup JSON): every buffer the container drops while
// growing is zeroised too, which a plain std::string cannot guarantee.
// Out of memory aborts, matching operator new with exceptions disabled.
template <class T>
struct ZeroingAllocator {
  using value_type = T;
  ZeroingAllocator() = default;
  template <class U>
  ZeroingAllocator(const ZeroingAllocator<U>&) {}
  T* allocate(size_t n) {
    void* p = mem::alloc(n * sizeof(T));
    if (!p) std::abort();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t n) {
    mem::zeroize(p, n * sizeof(T));
    mem::free(p);
  }
  template <class U>
  bool operator==(const ZeroingAllocator<U>&) const { return true; }
  template <class U>
  bool operator!=(const ZeroingAllocator<U>&) const { return false; }
};

using SecureString = std::basic_string<char, std::char_traits<char>, ZeroingAllocator<char>>;

// Also clears the inline (small-string) storage, which the allocator never sees.
inline void wipe(SecureString& s) {
  s.resize(s.capacity());
  mem::zeroize(&s[0], s.size());
  s.clear();
}

}  // namespace keyra::vault
