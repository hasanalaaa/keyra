// GATT tables for keyra_ble. Written out by hand rather than through the IDF
// esp_hidd component: its NimBLE backend stores LED output reports without
// telling the application, and Caps Lock handling depends on them.
#include "gatt.hpp"

#include <atomic>

#include "host/ble_hs.h"
#include "report_map.hpp"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace keyra::ble::gatt {
namespace {

// What each access callback serves; passed as the attribute's `arg`.
enum class Attr : uintptr_t {
  ProtocolMode, ReportMap, HidInfo, ControlPoint, Input, Output, BootInput, BootOutput,
  InputRef, OutputRef, Manufacturer, PnpId, Battery,
};

// HID 1.11, no country code, flags = NormallyConnectable only: Keyra never
// asks to wake a sleeping host.
constexpr uint8_t kHidInfo[4] = {0x11, 0x01, 0x00, 0x02};
constexpr uint8_t kRefInput[2] = {kReportId, 0x01};   // Input
constexpr uint8_t kRefOutput[2] = {kReportId, 0x02};  // Output
// PnP ID: vendor ID source 2 = USB-IF, then VID/PID/version little-endian —
// the same identity the USB descriptor uses (keyra_hid/src/usb_desc.hpp).
constexpr uint8_t kPnpId[7] = {0x02, 0x3A, 0x30, 0x00, 0x80, 0x00, 0x01};
constexpr char kManufacturer[] = "Keyra";
constexpr uint8_t kBatteryLevel = 100;  // bus-powered or USB-powered: always full

constexpr uint8_t kProtoBoot = 0x00;
constexpr uint8_t kProtoReport = 0x01;

std::atomic<uint8_t> s_protocol{kProtoReport};
std::atomic<uint8_t> s_leds{0};
std::atomic<bool> s_ledsKnown{false};  // the host wrote its LED report on this link
// A host that was just handed off (ignoreWrites()): its last LED or protocol
// writes must not land on the state of the host that took over.
std::atomic<uint16_t> s_ignored{BLE_HS_CONN_HANDLE_NONE};

uint16_t s_hInput = 0, s_hBootInput = 0;

const ble_uuid16_t kUuidHid = BLE_UUID16_INIT(0x1812);
const ble_uuid16_t kUuidProtocolMode = BLE_UUID16_INIT(0x2A4E);
const ble_uuid16_t kUuidReportMap = BLE_UUID16_INIT(0x2A4B);
const ble_uuid16_t kUuidHidInfo = BLE_UUID16_INIT(0x2A4A);
const ble_uuid16_t kUuidControlPoint = BLE_UUID16_INIT(0x2A4C);
const ble_uuid16_t kUuidReport = BLE_UUID16_INIT(0x2A4D);
const ble_uuid16_t kUuidReportRef = BLE_UUID16_INIT(0x2908);
const ble_uuid16_t kUuidBootInput = BLE_UUID16_INIT(0x2A22);
const ble_uuid16_t kUuidBootOutput = BLE_UUID16_INIT(0x2A32);
const ble_uuid16_t kUuidDis = BLE_UUID16_INIT(0x180A);
const ble_uuid16_t kUuidManufacturer = BLE_UUID16_INIT(0x2A29);
const ble_uuid16_t kUuidPnpId = BLE_UUID16_INIT(0x2A50);
const ble_uuid16_t kUuidBas = BLE_UUID16_INIT(0x180F);
const ble_uuid16_t kUuidBatteryLevel = BLE_UUID16_INIT(0x2A19);

void* tag(Attr a) { return reinterpret_cast<void*>(static_cast<uintptr_t>(a)); }

int append(ble_gatt_access_ctxt* ctxt, const void* data, uint16_t len) {
  return os_mbuf_append(ctxt->om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// Reads exactly one byte from a write, or reports the ATT length error.
int readByte(ble_gatt_access_ctxt* ctxt, uint8_t& out) {
  if (OS_MBUF_PKTLEN(ctxt->om) != 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  return ble_hs_mbuf_to_flat(ctxt->om, &out, 1, nullptr) == 0 ? 0 : BLE_ATT_ERR_UNLIKELY;
}

int access(uint16_t conn, uint16_t, ble_gatt_access_ctxt* ctxt, void* arg) {
  const auto what = static_cast<Attr>(reinterpret_cast<uintptr_t>(arg));
  const bool write = ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR || ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC;
  const bool keep = conn != s_ignored.load();
  static constexpr uint8_t kZeroReport[kInputReportLen] = {};
  uint8_t b = 0;
  int rc = 0;
  switch (what) {
    case Attr::ProtocolMode:
      if (!write) {
        b = s_protocol.load();
        return append(ctxt, &b, 1);
      }
      if ((rc = readByte(ctxt, b)) != 0) return rc;
      if (b != kProtoBoot && b != kProtoReport) return BLE_ATT_ERR_UNLIKELY;
      if (keep) s_protocol.store(b);
      return 0;
    case Attr::ReportMap: return append(ctxt, kReportMap.data(), kReportMap.size());
    case Attr::HidInfo: return append(ctxt, kHidInfo, sizeof kHidInfo);
    case Attr::ControlPoint: return readByte(ctxt, b);  // suspend / exit suspend: nothing to save
    case Attr::Input:
    case Attr::BootInput:
      // Keystrokes only ever go out as notifications; a read sees "no key".
      return append(ctxt, kZeroReport, sizeof kZeroReport);
    case Attr::Output:
    case Attr::BootOutput:
      if (!write) {
        b = s_leds.load();
        return append(ctxt, &b, 1);
      }
      if ((rc = readByte(ctxt, b)) != 0) return rc;
      if (!keep) return 0;
      s_leds.store(b);  // bit 1 = Caps Lock, same layout as USB
      s_ledsKnown.store(true);
      return 0;
    case Attr::InputRef: return append(ctxt, kRefInput, sizeof kRefInput);
    case Attr::OutputRef: return append(ctxt, kRefOutput, sizeof kRefOutput);
    case Attr::Manufacturer: return append(ctxt, kManufacturer, sizeof kManufacturer - 1);
    case Attr::PnpId: return append(ctxt, kPnpId, sizeof kPnpId);
    case Attr::Battery: return append(ctxt, &kBatteryLevel, 1);
  }
  return BLE_ATT_ERR_UNLIKELY;
}

constexpr ble_gatt_chr_flags kRead = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC;
constexpr ble_gatt_chr_flags kWrite = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC;
// The CCCD (subscribe switch) stays writable before encryption, as in IDF's own
// HID profile: macOS writes it during discovery, before pairing, and never
// retries a refused write, so it paired but never received a keystroke.
// Subscribing reveals nothing; sendKey() only notifies a bonded, encrypted link.
constexpr ble_gatt_chr_flags kNotify = BLE_GATT_CHR_F_NOTIFY;
// The HID *description* (report map, HID info, report references, protocol
// mode) is public and readable before pairing, as in NimBLE's own HID service:
// Apple hosts read it first to decide the device is a keyboard and never retry
// after an "insufficient encryption" error, so they paired but never used HID.
// Keystrokes stay protected: notifications and report values need encryption,
// and Keyra only sends to a bonded, encrypted host.
constexpr ble_gatt_chr_flags kPublicRead = BLE_GATT_CHR_F_READ;
constexpr uint8_t kDscRead = BLE_ATT_F_READ;

// NimBLE tables are meant to leave unused fields zero (designated initializers).
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// NimBLE takes non-const descriptor arrays; each ends with a zeroed entry.
ble_gatt_dsc_def s_inputDscs[] = {
    {.uuid = &kUuidReportRef.u, .att_flags = kDscRead, .min_key_size = 0, .access_cb = access, .arg = tag(Attr::InputRef)},
    {},
};
ble_gatt_dsc_def s_outputDscs[] = {
    {.uuid = &kUuidReportRef.u, .att_flags = kDscRead, .min_key_size = 0, .access_cb = access, .arg = tag(Attr::OutputRef)},
    {},
};

const ble_gatt_chr_def kHidChrs[] = {
    {.uuid = &kUuidProtocolMode.u, .access_cb = access, .arg = tag(Attr::ProtocolMode),
     .flags = kPublicRead | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
    {.uuid = &kUuidReportMap.u, .access_cb = access, .arg = tag(Attr::ReportMap), .flags = kPublicRead},
    {.uuid = &kUuidHidInfo.u, .access_cb = access, .arg = tag(Attr::HidInfo), .flags = kPublicRead},
    {.uuid = &kUuidControlPoint.u, .access_cb = access, .arg = tag(Attr::ControlPoint),
     .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
    {.uuid = &kUuidReport.u, .access_cb = access, .arg = tag(Attr::Input), .descriptors = s_inputDscs,
     .flags = kRead | kNotify, .val_handle = &s_hInput},
    {.uuid = &kUuidReport.u, .access_cb = access, .arg = tag(Attr::Output), .descriptors = s_outputDscs,
     .flags = kRead | kWrite},
    {.uuid = &kUuidBootInput.u, .access_cb = access, .arg = tag(Attr::BootInput), .flags = kRead | kNotify,
     .val_handle = &s_hBootInput},
    {.uuid = &kUuidBootOutput.u, .access_cb = access, .arg = tag(Attr::BootOutput), .flags = kRead | kWrite},
    {},
};

const ble_gatt_chr_def kDisChrs[] = {
    {.uuid = &kUuidManufacturer.u, .access_cb = access, .arg = tag(Attr::Manufacturer), .flags = BLE_GATT_CHR_F_READ},
    {.uuid = &kUuidPnpId.u, .access_cb = access, .arg = tag(Attr::PnpId), .flags = BLE_GATT_CHR_F_READ},
    {},
};

const ble_gatt_chr_def kBasChrs[] = {
    {.uuid = &kUuidBatteryLevel.u, .access_cb = access, .arg = tag(Attr::Battery), .flags = BLE_GATT_CHR_F_READ},
    {},
};

const ble_gatt_svc_def kServices[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kUuidHid.u, .characteristics = kHidChrs},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kUuidDis.u, .characteristics = kDisChrs},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kUuidBas.u, .characteristics = kBasChrs},
    {},
};

}  // namespace

int registerServices() {
  ble_svc_gap_init();
  ble_svc_gatt_init();
  int rc = ble_gatts_count_cfg(kServices);
  if (rc == 0) rc = ble_gatts_add_svcs(kServices);
  return rc;
}

uint16_t inputHandle() { return s_hInput; }
uint16_t bootInputHandle() { return s_hBootInput; }
bool bootProtocol() { return s_protocol.load() == kProtoBoot; }
uint8_t leds() { return s_leds.load(); }
bool ledsKnown() { return s_ledsKnown.load(); }

void ignoreWrites(uint16_t conn) { s_ignored.store(conn); }

void resetLink() {
  s_protocol.store(kProtoReport);
  s_leds.store(0);
  s_ledsKnown.store(false);
}

}  // namespace keyra::ble::gatt
