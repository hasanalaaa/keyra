#include "usb_dev.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "class/cdc/cdc.h"  // CDC constants for the template check even when CDC is compiled out
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "usb_desc.hpp"

#if CFG_TUD_CDC > 0
#include "hal/usb_serial_jtag_ll.h"
#include "soc/rtc_cntl_reg.h"
#include "tinyusb_cdc_acm.h"
#endif

namespace keyra::hid {
namespace {

constexpr const char* TAG = "keyra_usb";

// ---- Descriptor cross-check against TinyUSB's own templates -------------
// Our bytes are hand-built (so host tests can parse them); here the compiler
// proves they equal what TinyUSB's macros would produce for the same layout.
#if CFG_TUD_HID < 1
#error "keyra_hid needs CONFIG_TINYUSB_HID_COUNT >= 1"
#endif

template <size_t N, size_t M>
constexpr bool sameBytes(const std::array<uint8_t, N>& a, const uint8_t (&b)[M]) {
  if (N != M) return false;
  for (size_t i = 0; i < N; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

constexpr uint8_t kRefHidOnly[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(desc::kItfHid, desc::kStrHidItf, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc::kHidReport),
                       desc::kEpHidIn, desc::kHidReportLen, desc::kHidPollMs),
};
static_assert(desc::kTotalHidOnly == TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, "HID-only length");
static_assert(sameBytes(desc::kConfigHidOnly, kRefHidOnly), "HID-only config differs from TinyUSB template");

constexpr uint8_t kRefHidCdc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(desc::kItfHid, desc::kStrHidItf, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc::kHidReport),
                       desc::kEpHidIn, desc::kHidReportLen, desc::kHidPollMs),
    TUD_CDC_DESCRIPTOR(desc::kItfCdcComm, desc::kStrCdcItf, desc::kEpCdcNotif, desc::kCdcNotifLen, desc::kEpCdcOut,
                       desc::kEpCdcIn, desc::kCdcDataLen),
};
static_assert(desc::kTotalHidCdc == TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN, "HID+CDC length");
static_assert(sameBytes(desc::kConfigHidCdc, kRefHidCdc), "HID+CDC config differs from TinyUSB template");
static_assert(sizeof(tusb_desc_device_t) == desc::kDeviceHidOnly.size(), "device descriptor size");

tusb_desc_device_t s_device;  // TinyUSB keeps the pointer: static lifetime
char s_serial[13];            // 12 hex digits of the base MAC
const char* s_strings[desc::kStrCount];

#if CFG_TUD_CDC > 0
// ---- Dev console: log mirror ---------------------------------------------
vprintf_like_t s_uartVprintf = nullptr;
thread_local bool t_inMirror = false;  // CDC driver may log → avoid recursion

int mirrorVprintf(const char* fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);
  const int n = s_uartVprintf(fmt, args);  // UART console stays the primary sink
  if (!t_inMirror) {
    t_inMirror = true;
    char buf[256];
    const int len = vsnprintf(buf, sizeof buf, fmt, copy);
    if (len > 0) {
      const size_t out = static_cast<size_t>(len) < sizeof buf ? static_cast<size_t>(len) : sizeof buf - 1;
      // Non-blocking: when no terminal holds DTR the FIFO just overwrites.
      tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, reinterpret_cast<const uint8_t*>(buf), out);
      tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    }
    t_inMirror = false;
  }
  va_end(copy);
  return n;
}

// ---- 1200-baud touch → ROM download mode -----------------------------------
// tools/devctl.py opens the CDC port at 1200 bps and drops DTR. Hosts order
// SET_LINE_CODING and SET_CONTROL_LINE_STATE differently, so either callback
// may complete the condition (rate == 1200 && !DTR).
constexpr uint32_t kTouchBaud = 1200;
volatile uint32_t s_baud = 0;
volatile bool s_dtr = false;

[[noreturn]] void rebootToRomDownload() {
  ESP_LOGW(TAG, "1200-baud touch: rebooting into ROM download mode");
  // Let the host see the composite device leave before the PHY changes hands.
  tud_disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));
  // esp_restart() only resets the CPUs; the RTC-domain PHY mux survives. Hand
  // the internal PHY back to USB-Serial/JTAG, the port the ROM downloads on.
  usb_serial_jtag_ll_phy_enable_external(false);
  usb_serial_jtag_ll_phy_enable_pad(true);
  // ROM checks this bit on the next boot, enters download mode, clears it.
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

void maybeTouch() {
  if (s_baud == kTouchBaud && !s_dtr) rebootToRomDownload();
}

void onLineCoding(int, cdcacm_event_t* ev) {
  s_baud = ev->line_coding_changed_data.p_line_coding->bit_rate;
  maybeTouch();
}

void onLineState(int, cdcacm_event_t* ev) {
  s_dtr = ev->line_state_changed_data.dtr;
  maybeTouch();
}

esp_err_t startCdc() {
  tinyusb_config_cdcacm_t acm = {};
  acm.cdc_port = TINYUSB_CDC_ACM_0;
  acm.callback_line_state_changed = onLineState;
  acm.callback_line_coding_changed = onLineCoding;
  ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm), TAG, "cdcacm init");
  s_uartVprintf = esp_log_set_vprintf(mirrorVprintf);
  return ESP_OK;
}
#endif  // CFG_TUD_CDC > 0

void onUsbEvent(tinyusb_event_t* ev, void*) {
  if (ev->id == TINYUSB_EVENT_DETACHED) forgetHostLeds();
}

}  // namespace

bool usbStart(bool devCdc) {
#if CFG_TUD_CDC > 0
  const bool cdc = devCdc;
#else
  const bool cdc = false;
  if (devCdc) {
    ESP_LOGE(TAG, "dev CDC requested but CONFIG_TINYUSB_CDC_ENABLED is off — enumerating HID only "
                  "(no log mirror, no 1200-baud download hook)");
  }
#endif

  const auto& dev = cdc ? desc::kDeviceHidCdc : desc::kDeviceHidOnly;
  std::memcpy(&s_device, dev.data(), sizeof s_device);

  uint8_t mac[6] = {};
  ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BASE));
  snprintf(s_serial, sizeof s_serial, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  static const char kLangEnUs[] = {0x09, 0x04};
  s_strings[desc::kStrLang] = kLangEnUs;
  s_strings[desc::kStrManufacturer] = desc::kManufacturer;
  s_strings[desc::kStrProduct] = desc::kProduct;
  s_strings[desc::kStrSerial] = s_serial;
  s_strings[desc::kStrHidItf] = desc::kHidItfName;
  s_strings[desc::kStrCdcItf] = desc::kCdcItfName;

  tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(onUsbEvent);
  cfg.descriptor.device = &s_device;
  cfg.descriptor.string = s_strings;
  cfg.descriptor.string_count = desc::kStrCount;
  cfg.descriptor.full_speed_config = cdc ? desc::kConfigHidCdc.data() : desc::kConfigHidOnly.data();
  ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));

#if CFG_TUD_CDC > 0
  if (cdc) ESP_ERROR_CHECK(startCdc());
#endif
  ESP_LOGI(TAG, "USB up: %s, serial %s", cdc ? "HID keyboard + CDC console" : "HID keyboard", s_serial);
  return cdc;
}

}  // namespace keyra::hid
