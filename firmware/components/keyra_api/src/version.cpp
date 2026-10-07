#include "version.hpp"

namespace keyra::api::version {

std::optional<std::array<int, 3>> parse(std::string_view v) {
  if (const size_t dash = v.find('-'); dash != std::string_view::npos) v = v.substr(0, dash);
  std::array<int, 3> out{};
  for (size_t part = 0; part < 3; ++part) {
    size_t digits = 0;
    int n = 0;
    while (!v.empty() && v.front() >= '0' && v.front() <= '9') {
      if (++digits > 4) return std::nullopt;
      n = n * 10 + (v.front() - '0');
      v.remove_prefix(1);
    }
    if (digits == 0) return std::nullopt;
    out[part] = n;
    if (part < 2) {
      if (v.empty() || v.front() != '.') return std::nullopt;
      v.remove_prefix(1);
    }
  }
  if (!v.empty()) return std::nullopt;
  return out;
}

bool mayInstall(std::string_view next, std::string_view running) {
  const auto n = parse(next), r = parse(running);
  return n && r && *n >= *r;
}

std::string fromTag(std::string_view tag) {
  if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) tag.remove_prefix(1);
  return parse(tag) ? std::string(tag) : std::string();
}

}  // namespace keyra::api::version
