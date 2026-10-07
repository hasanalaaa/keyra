// Descriptor consistency for both USB variants: walk the bytes the way a
// host does and check every count and length agrees.
#include <set>
#include <vector>

#include "check.hpp"
#include "usb_desc.hpp"

using namespace keyra::hid::desc;

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

struct Parsed {
  int interfaces = 0;
  int iads = 0;
  int hidDescs = 0;
  std::vector<uint8_t> itfNumbers;
  std::set<uint8_t> endpoints;
  std::vector<uint16_t> hidReportLens;  // per HID interface, in order
  bool ok = true;
};

template <size_t N>
Parsed walk(const std::array<uint8_t, N>& c) {
  Parsed p;
  CHECK_EQ(c[0], 9);
  CHECK_EQ(c[1], 0x02);
  CHECK_EQ(le16(&c[2]), N);  // wTotalLength
  size_t i = 0;
  int expectEps = 0;
  while (i < N) {
    const uint8_t len = c[i];
    const uint8_t type = c[i + 1];
    if (len < 2 || i + len > N) {
      p.ok = false;
      break;
    }
    switch (type) {
      case 0x04:  // interface
        CHECK_EQ(len, 9);
        CHECK_EQ(expectEps, 0);  // previous interface got all its endpoints
        ++p.interfaces;
        p.itfNumbers.push_back(c[i + 2]);
        expectEps = c[i + 4];
        CHECK(c[i + 8] < kStrCount);  // iInterface refers to a real string
        break;
      case 0x05:  // endpoint
        CHECK_EQ(len, 7);
        CHECK(expectEps > 0);
        --expectEps;
        CHECK(p.endpoints.insert(c[i + 2]).second);  // no duplicate addresses
        CHECK(le16(&c[i + 4]) <= 64);                // full-speed limits
        break;
      case 0x0B:  // IAD
        CHECK_EQ(len, 8);
        ++p.iads;
        CHECK_EQ(c[i + 2], kItfCdcComm);
        CHECK_EQ(c[i + 3], 2);
        break;
      case 0x21:  // HID
        CHECK_EQ(len, 9);
        ++p.hidDescs;
        p.hidReportLens.push_back(le16(&c[i + 7]));
        break;
      default:
        break;
    }
    i += len;
  }
  CHECK_EQ(i, N);
  CHECK_EQ(expectEps, 0);
  CHECK_EQ(static_cast<int>(c[4]), p.interfaces);  // bNumInterfaces
  for (size_t k = 0; k < p.itfNumbers.size(); ++k) CHECK_EQ(p.itfNumbers[k], k);  // 0..n-1, in order
  return p;
}

void checkReportDescriptor() {
  // Sum input/output bit widths: boot keyboard = 64 input bits, 8 output bits.
  int reportSize = 0, reportCount = 0, inBits = 0, outBits = 0, depth = 0;
  size_t i = 0;
  while (i < kHidReport.size()) {
    const uint8_t prefix = kHidReport[i];
    const int size = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
    const uint8_t tag = prefix & 0xFC;
    const int val = size >= 1 ? kHidReport[i + 1] : 0;
    switch (tag) {
      case 0x74: reportSize = val; break;
      case 0x94: reportCount = val; break;
      case 0x80: inBits += reportSize * reportCount; break;
      case 0x90: outBits += reportSize * reportCount; break;
      case 0xA0: ++depth; break;
      case 0xC0: --depth; break;
      default: break;
    }
    i += 1 + static_cast<size_t>(size);
  }
  CHECK_EQ(i, kHidReport.size());
  CHECK_EQ(depth, 0);
  CHECK_EQ(inBits, kHidReportLen * 8);
  CHECK_EQ(outBits, 8);
}

// FIDO report descriptor: usage page 0xF1D0 / usage 1, 64-byte input and output.
void checkFidoReport() {
  CHECK(kFidoReport[0] == 0x06 && kFidoReport[1] == 0xD0 && kFidoReport[2] == 0xF1);
  CHECK(kFidoReport[3] == 0x09 && kFidoReport[4] == 0x01);
  int reportSize = 0, reportCount = 0, inBits = 0, outBits = 0, depth = 0;
  size_t i = 0;
  while (i < kFidoReport.size()) {
    const uint8_t prefix = kFidoReport[i];
    const int size = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
    const uint8_t tag = prefix & 0xFC;
    const int val = size >= 1 ? kFidoReport[i + 1] : 0;
    switch (tag) {
      case 0x74: reportSize = val; break;
      case 0x94: reportCount = val; break;
      case 0x80: inBits += reportSize * reportCount; break;
      case 0x90: outBits += reportSize * reportCount; break;
      case 0xA0: ++depth; break;
      case 0xC0: --depth; break;
      default: break;
    }
    i += 1 + static_cast<size_t>(size);
  }
  CHECK_EQ(i, kFidoReport.size());
  CHECK_EQ(depth, 0);
  CHECK_EQ(inBits, 64 * 8);
  CHECK_EQ(outBits, 64 * 8);
}

// The FIDO interface block: HID class, no boot protocol, OUT then IN, 64 bytes.
template <size_t N>
void checkFidoInterface(const std::array<uint8_t, N>& c) {
  const size_t at = kConfigLen + kHidBlockLen;
  CHECK_EQ(c[at + 1], 0x04);
  CHECK_EQ(c[at + 2], kItfFido);
  CHECK_EQ(c[at + 4], 2);
  CHECK(c[at + 5] == 0x03 && c[at + 6] == 0x00 && c[at + 7] == 0x00);
  CHECK_EQ(c[at + 8], kStrFidoItf);
  CHECK_EQ(le16(&c[at + 9 + 7]), kFidoReport.size());
  CHECK(c[at + 18 + 2] == kEpFidoOut && c[at + 18 + 3] == 0x03 && le16(&c[at + 18 + 4]) == 64);
  CHECK(c[at + 25 + 2] == kEpFidoIn && c[at + 25 + 3] == 0x03 && le16(&c[at + 25 + 4]) == 64);
}

}  // namespace

int main() {
  const Parsed hid = walk(kConfigHidOnly);
  CHECK(hid.ok);
  CHECK_EQ(hid.interfaces, 2);
  CHECK_EQ(hid.iads, 0);  // nothing about CDC in the HID-only variant
  CHECK_EQ(hid.hidDescs, 2);
  CHECK((hid.hidReportLens == std::vector<uint16_t>{kHidReport.size(), kFidoReport.size()}));
  CHECK((hid.endpoints == std::set<uint8_t>{kEpHidIn, kEpFidoOut, kEpFidoIn}));
  CHECK_EQ(kConfigHidOnly[9 + 5], 0x03);  // HID class
  CHECK_EQ(kConfigHidOnly[9 + 6], 0x01);  // boot subclass
  CHECK_EQ(kConfigHidOnly[9 + 7], 0x01);  // keyboard protocol

  const Parsed cdc = walk(kConfigHidCdc);
  CHECK(cdc.ok);
  CHECK_EQ(cdc.interfaces, 4);
  CHECK_EQ(cdc.iads, 1);
  CHECK_EQ(cdc.hidDescs, 2);
  CHECK((cdc.hidReportLens == std::vector<uint16_t>{kHidReport.size(), kFidoReport.size()}));
  CHECK((cdc.endpoints == std::set<uint8_t>{kEpHidIn, kEpFidoOut, kEpFidoIn, kEpCdcNotif, kEpCdcOut, kEpCdcIn}));
  // ESP32-S3 OTG budget: at most 4 IN endpoints besides EP0.
  int ins = 0;
  for (uint8_t ep : cdc.endpoints) ins += (ep & 0x80) ? 1 : 0;
  CHECK(ins <= 4);
  checkFidoInterface(kConfigHidOnly);
  checkFidoInterface(kConfigHidCdc);
  // The HID part (keyboard + FIDO) is byte-identical in both variants.
  for (size_t k = 9; k < kTotalHidOnly; ++k) CHECK_EQ(kConfigHidCdc[k], kConfigHidOnly[k]);

  // Device descriptors.
  for (const auto* d : {&kDeviceHidOnly, &kDeviceHidCdc}) {
    CHECK_EQ((*d)[0], 18);
    CHECK_EQ((*d)[1], 0x01);
    CHECK_EQ(le16(&(*d)[8]), kVid);
    CHECK_EQ(le16(&(*d)[10]), kPid);
    CHECK_EQ((*d)[14], kStrManufacturer);
    CHECK_EQ((*d)[15], kStrProduct);
    CHECK_EQ((*d)[16], kStrSerial);
    CHECK_EQ((*d)[17], 1);
  }
  CHECK_EQ(kDeviceHidOnly[4], 0x00);  // composite class only when composite
  CHECK_EQ(kDeviceHidCdc[4], 0xEF);
  CHECK_EQ(kDeviceHidCdc[5], 0x02);
  CHECK_EQ(kDeviceHidCdc[6], 0x01);
  CHECK(le16(&kDeviceHidOnly[12]) != le16(&kDeviceHidCdc[12]));  // distinct bcdDevice
  CHECK_EQ(kVid, 0x303A);

  checkReportDescriptor();
  checkFidoReport();
  CHECK(kStrCount <= 8);  // esp_tinyusb USB_STRING_DESCRIPTOR_ARRAY_SIZE
  TEST_MAIN_END();
}
