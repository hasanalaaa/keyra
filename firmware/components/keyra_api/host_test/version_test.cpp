// Update version rules (SPEC §14): parsing and the no-downgrade check.
#include "keyra_test.hpp"
#include "version.hpp"

using namespace keyra::api::version;

int main() {
  CHECK((parse("0.1.0") == std::array<int, 3>{0, 1, 0}));
  CHECK((parse("12.0.9999") == std::array<int, 3>{12, 0, 9999}));
  CHECK((parse("1.2.3-rc1") == std::array<int, 3>{1, 2, 3}));
  for (const char* bad : {"", "1", "1.2", "1.2.3.4", "1..3", "a.b.c", "1.2.3x", "+1.2.3", "1.2.-3", "10000.0.0", " 1.2.3"})
    CHECK(!parse(bad));

  CHECK(mayInstall("0.2.0", "0.1.0"));
  CHECK(mayInstall("0.1.0", "0.1.0"));    // reinstalling the same version is fine
  CHECK(mayInstall("1.0.0", "0.99.99"));
  CHECK(mayInstall("0.1.10", "0.1.9"));   // numeric, not text order
  CHECK(!mayInstall("0.1.0", "0.2.0"));   // no downgrades
  CHECK(!mayInstall("0.1.9", "0.1.10"));
  CHECK(!mayInstall("garbage", "0.1.0"));  // unknown versions are refused
  CHECK(!mayInstall("0.2.0", "dev"));
  CHECK(fromTag("v0.2.0") == "0.2.0");
  CHECK(fromTag("0.2.0") == "0.2.0");
  CHECK(fromTag("V1.0.0-rc2") == "1.0.0-rc2");
  CHECK(fromTag("latest").empty());
  CHECK(fromTag("vv0.2.0").empty());
  CHECK(fromTag("").empty());
  return KEYRA_TEST_RESULT();
}
