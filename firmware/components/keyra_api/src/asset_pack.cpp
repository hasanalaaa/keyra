#include "asset_pack.hpp"

#include <cstring>

namespace keyra::api::assets {
namespace {

constexpr char kMagic[4] = {'K', 'Y', 'A', '1'};

struct Reader {
  const uint8_t* p;
  size_t left;

  bool take(size_t n, const uint8_t** out) {
    if (n > left) return false;
    *out = p;
    p += n;
    left -= n;
    return true;
  }
  bool u8(uint8_t* v) {
    const uint8_t* b;
    if (!take(1, &b)) return false;
    *v = b[0];
    return true;
  }
  bool u16(uint16_t* v) {
    const uint8_t* b;
    if (!take(2, &b)) return false;
    *v = uint16_t(b[0] | b[1] << 8);
    return true;
  }
  bool u32(uint32_t* v) {
    const uint8_t* b;
    if (!take(4, &b)) return false;
    *v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    return true;
  }
};

}  // namespace

std::optional<File> find(const uint8_t* pack, size_t packLen, std::string_view path) {
  if (!pack || packLen < sizeof kMagic || std::memcmp(pack, kMagic, sizeof kMagic) != 0) return std::nullopt;
  Reader r{pack + sizeof kMagic, packLen - sizeof kMagic};
  while (r.left > 0) {
    uint16_t pathLen, typeLen;
    uint8_t flags;
    uint32_t size;
    const uint8_t *name, *type, *body;
    if (!r.u16(&pathLen) || !r.take(pathLen, &name) || !r.u16(&typeLen) || !r.take(typeLen, &type) ||
        !r.u8(&flags) || !r.u32(&size) || !r.take(size, &body))
      return std::nullopt;
    // The type must be one C string exactly filling its length (httpd keeps the pointer).
    if (typeLen < 2 || type[typeLen - 1] != '\0' || std::memchr(type, '\0', typeLen - 1)) return std::nullopt;
    if (std::string_view(reinterpret_cast<const char*>(name), pathLen) == path)
      return File{reinterpret_cast<const char*>(type), body, size, (flags & 1) != 0};
  }
  return std::nullopt;
}

}  // namespace keyra::api::assets
