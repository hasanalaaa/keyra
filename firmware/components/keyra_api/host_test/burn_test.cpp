// Delete after typing (SPEC §16, burn.cpp): the use is taken from the vault
// before any key is sent, and a vault that cannot record it means nothing is typed.
#include "burn.hpp"
#include "keyra_test.hpp"
#include "rig.hpp"

using keyra::actions::Code;
namespace burn = keyra::api::burn;
namespace vault = keyra::vault;
using vault::Status;
using vault::test::Rig;

namespace {

uint32_t addEntry(Rig& rig, uint8_t burnAfter) {
  vault::Entry e = vault::test::sample("One-time code");
  e.burnAfter = burnAfter;
  CHECK(rig->put(e) == Status::Ok);
  return e.id;
}

uint8_t storedBurnAfter(Rig& rig, uint32_t id) {
  vault::Entry e;
  if (rig->get(id, e) != Status::Ok) return 0xFF;  // gone
  const uint8_t n = e.burnAfter;
  vault::wipe(e);
  return n;
}

struct Run {
  Code code;
  burn::Counted counted;
  int typed = 0;
  uint8_t storedWhileTyping = 0;  // burnAfter in the vault when the first key went out
};

Run run(Rig& rig, uint32_t id, bool counts, Code check = Code::Typed) {
  Run r;
  r.code = burn::typeCounted(
      counts, [&](bool* burned) { return rig->touch(id, 1700000100, true, burned); }, [&] { return check; },
      [&] {
        ++r.typed;
        r.storedWhileTyping = storedBurnAfter(rig, id);
        return Code::Typed;
      },
      r.counted);
  return r;
}

void usedBeforeTheFirstKey() {
  auto rig = Rig::ready();
  const uint32_t id = addEntry(*rig, 2);
  Run r = run(*rig, id, true);
  CHECK(r.code == Code::Typed && r.typed == 1 && r.counted.used && !r.counted.burned);
  CHECK(r.storedWhileTyping == 1);  // already saved when typing started
  r = run(*rig, id, true);
  CHECK(r.code == Code::Typed && r.counted.burned);
  CHECK(r.storedWhileTyping == 0xFF);  // the last use: deleted before typing
  CHECK(storedBurnAfter(*rig, id) == 0xFF);
}

// The fix: a failed save or delete stops the typing (it used to type, then lose the error).
void failedSaveTypesNothing() {
  auto rig = Rig::ready();
  const uint32_t id = addEntry(*rig, 2);
  rig->storage.failAfter = 0;  // the next write fails
  Run r = run(*rig, id, true);
  CHECK(r.code == Code::Failed && r.typed == 0 && !r.counted.used);
  CHECK(storedBurnAfter(*rig, id) == 2);

  const uint32_t last = addEntry(*rig, 1);
  rig->storage.failAfter = 0;  // the delete fails
  r = run(*rig, last, true);
  CHECK(r.code == Code::Failed && r.typed == 0 && !r.counted.burned);
  CHECK(storedBurnAfter(*rig, last) == 1);
}

void refusedOrUncountedUsesNothing() {
  auto rig = Rig::ready();
  const uint32_t id = addEntry(*rig, 1);
  Run r = run(*rig, id, true, Code::UnsupportedChar);
  CHECK(r.code == Code::UnsupportedChar && r.typed == 0 && !r.counted.used);
  CHECK(storedBurnAfter(*rig, id) == 1);
  r = run(*rig, id, false);  // a username, a 2FA code, or an entry without burnAfter
  CHECK(r.code == Code::Typed && r.typed == 1 && !r.counted.used);
  CHECK(storedBurnAfter(*rig, id) == 1);
}

}  // namespace

int main() {
  usedBeforeTheFirstKey();
  failedSaveTypesNothing();
  refusedOrUncountedUsesNothing();
  return KEYRA_TEST_RESULT();
}
