// Per-computer operating systems (SPEC §10.5): names and the per-bond table.
#include "keyra_test.hpp"
#include "host_os.hpp"

using namespace keyra::api::hostos;
using keyra::ble::Addr;

namespace {

const Addr kMac = {0x60, 0x3E, 0x5F, 0x5D, 0x2C, 0xEE};
const Addr kPhone = {0xA4, 0xC1, 0x38, 0x0B, 0x7F, 0x3A};

void testNames() {
  Os os = Os::Mac;
  CHECK(parse("", os) && os == Os::Unknown);
  for (Os o : {Os::Mac, Os::Ios, Os::Windows, Os::Android, Os::Linux}) {
    Os back;
    CHECK(parse(name(o), back) && back == o);
  }
  CHECK(!parse("macos", os));
  CHECK(!parse("Mac", os));
}

void testTable() {
  std::string t;
  CHECK(get(t, kMac) == Os::Unknown);
  t = set(t, kMac, Os::Mac);
  CHECK(t == "60:3E:5F:5D:2C:EE=mac");
  t = set(t, kPhone, Os::Ios);
  CHECK(get(t, kMac) == Os::Mac);
  CHECK(get(t, kPhone) == Os::Ios);
  t = set(t, kMac, Os::Windows);  // replaces, never duplicates
  CHECK(get(t, kMac) == Os::Windows);
  CHECK(t == "A4:C1:38:0B:7F:3A=ios;60:3E:5F:5D:2C:EE=windows");
  t = set(t, kPhone, Os::Unknown);  // forgetting a bond removes its item
  CHECK(t == "60:3E:5F:5D:2C:EE=windows");
  t = set(t, kMac, Os::Unknown);
  CHECK(t.empty());
}

void testCorruptTableReadsUnknown() {
  CHECK(get("garbage", kMac) == Os::Unknown);
  CHECK(get("60:3E:5F:5D:2C:EE=beos", kMac) == Os::Unknown);
  CHECK(get(";;60:3E:5F:5D:2C:EE=mac;;", kMac) == Os::Mac);
  // set() drops what it cannot read.
  CHECK(set("junk;60:3E:5F:5D:2C:EE=beos", kPhone, Os::Android) == "A4:C1:38:0B:7F:3A=android");
}

void testSwitchRule() {
  CHECK(switchesWithCtrlSpace(Os::Mac));
  CHECK(switchesWithCtrlSpace(Os::Ios));
  CHECK(!switchesWithCtrlSpace(Os::Windows));
  CHECK(!switchesWithCtrlSpace(Os::Android));
  CHECK(!switchesWithCtrlSpace(Os::Unknown));
}

}  // namespace

int main() {
  testNames();
  testTable();
  testCorruptTableReadsUnknown();
  testSwitchRule();
  return KEYRA_TEST_RESULT();
}
