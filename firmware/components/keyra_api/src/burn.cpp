#include "burn.hpp"

namespace keyra::api::burn {

actions::Code typeCounted(bool counts, const std::function<vault::Status(bool* burned)>& takeUse,
                          const std::function<actions::Code()>& check, const std::function<actions::Code()>& type,
                          Counted& out) {
  out = Counted{};
  if (!counts) return type();
  // Nothing is used up by a typing that would be refused before its first key.
  if (const actions::Code c = check(); c != actions::Code::Typed) return c;
  bool burned = false;
  if (takeUse(&burned) != vault::Status::Ok) return actions::Code::Failed;
  out.used = true;
  out.burned = burned;
  return type();
}

}  // namespace keyra::api::burn
