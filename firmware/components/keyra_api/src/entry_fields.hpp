#pragma once
// The text fields of one account as GET /api/entries/{id} returns them
// (SPEC §5, §12.3). Plain C++ so the reveal rule runs in host tests.
#include <string>
#include <utility>
#include <vector>

#include "keyra/vault.hpp"

namespace keyra::api {

// Name → value of each text field this answer carries. Without the reveal
// press (`revealed` false) the password, 2FA secret and notes stay out: notes
// often hold recovery codes or PINs, so they are treated like the password.
inline std::vector<std::pair<const char*, const std::string*>> entryStrings(const vault::Entry& e, bool revealed) {
  std::vector<std::pair<const char*, const std::string*>> out = {
      {"title", &e.title}, {"url", &e.url}, {"username", &e.username}};
  if (revealed) {
    out.push_back({"password", &e.password});
    out.push_back({"totp", &e.totp});
    out.push_back({"notes", &e.notes});
  }
  return out;
}

}  // namespace keyra::api
