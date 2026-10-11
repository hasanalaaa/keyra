// GET /api/entries/{id} without the reveal press (SPEC §12.3): no password,
// 2FA secret or notes; with it, all of them.
#include <string>

#include "entry_fields.hpp"
#include "keyra_test.hpp"

using namespace keyra;

namespace {

bool has(const vault::Entry& e, bool revealed, const char* key) {
  for (const auto& [k, v] : api::entryStrings(e, revealed))
    if (std::string(k) == key) return true;
  return false;
}

void notesNeedThePressLikeThePassword() {
  vault::Entry e;
  e.title = "Bank";
  e.username = "ann";
  e.password = "pw";
  e.totp = "JBSWY3DPEHPK3PXP";
  e.notes = "recovery codes: 1234 5678";
  for (const char* key : {"title", "url", "username"}) {
    CHECK(has(e, false, key));
    CHECK(has(e, true, key));
  }
  for (const char* key : {"password", "totp", "notes"}) {
    CHECK(!has(e, false, key));
    CHECK(has(e, true, key));
  }
  for (const auto& [k, v] : api::entryStrings(e, true))
    if (std::string(k) == "notes") CHECK(*v == e.notes);
}

}  // namespace

int main() {
  notesNeedThePressLikeThePassword();
  return KEYRA_TEST_RESULT();
}
